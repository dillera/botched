#include "binary_tcp_host.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace botched::server {
namespace {
bool nonblock(int fd) {
    int f = fcntl(fd, F_GETFL, 0);
    return f >= 0 && fcntl(fd, F_SETFL, f | O_NONBLOCK) == 0;
}
} // namespace
BinaryTcpHost::BinaryTcpHost(GameServer &s, const Clock &c, Options o)
    : server_(s), bridge_(s, c), opt_(std::move(o)) {
    if (opt_.port < 0 || opt_.port > 65535 || !opt_.max_clients || opt_.max_output_bytes < 242 ||
        !opt_.read_budget || !opt_.write_budget || opt_.incomplete_frame_ms <= 0)
        throw std::invalid_argument("binary host limits");
}
BinaryTcpHost::~BinaryTcpHost() {
    for (auto &c : conns_)
        close(c);
    if (listener_ >= 0)
        ::close(listener_);
}
void BinaryTcpHost::close(Conn &c) {
    if (c.fd >= 0) {
        bridge_.disconnect(c.id);
        ::close(c.fd);
        c.fd = -1;
    }
}
bool BinaryTcpHost::listen(std::string &error) {
    if (listener_ >= 0) {
        error = "already listening";
        return false;
    }
    listener_ = socket(AF_INET, SOCK_STREAM, 0);
    auto fail = [&] {
        error = std::strerror(errno);
        if (listener_ >= 0)
            ::close(listener_);
        listener_ = -1;
        return false;
    };
    if (listener_ < 0)
        return fail();
    int one = 1;
    setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(opt_.port);
    if (inet_pton(AF_INET, opt_.bind_address.c_str(), &a.sin_addr) != 1) {
        errno = EINVAL;
        return fail();
    }
    if (bind(listener_, (sockaddr *)&a, sizeof a) || ::listen(listener_, 16) || !nonblock(listener_))
        return fail();
    socklen_t n = sizeof a;
    if (getsockname(listener_, (sockaddr *)&a, &n))
        return fail();
    port_ = ntohs(a.sin_port);
    return true;
}
void BinaryTcpHost::accept_new() {
    // Bound work even under a continuous connection flood.
    for (size_t i = 0; i < 32; ++i) {
        int fd = accept(listener_, nullptr, nullptr);
        if (fd < 0)
            return;
        if (conns_.size() >= opt_.max_clients || !next_connection_ || !nonblock(fd)) {
            ::close(fd);
            continue;
        }
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        if (opt_.send_buffer_bytes > 0)
            setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &opt_.send_buffer_bytes, sizeof(int));
        Conn c{};
        c.fd = fd;
        c.id = next_connection_++;
        c.output.reserve(opt_.max_output_bytes);
        conns_.push_back(std::move(c));
    }
}
size_t BinaryTcpHost::queued_bytes() const {
    size_t n = 0;
    for (const auto &c : conns_)
        n += c.output.size() - c.sent;
    return n;
}
void BinaryTcpHost::read(Conn &c) {
    size_t budget = opt_.read_budget;
    while (budget && c.fd >= 0) {
        auto want = std::min(budget, c.expected - c.used);
        ssize_t n = recv(c.fd, c.input.data() + c.used, want, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                close(c);
            return;
        }
        if (!n) {
            close(c);
            return;
        }
        if (!c.used)
            c.started = std::chrono::steady_clock::now();
        c.used += n;
        budget -= n;
        if (c.used != c.expected)
            continue;
        if (c.expected == 2) {
            unsigned length = c.input[0] | unsigned(c.input[1]) << 8;
            if (length < 3 || length > c.payload) {
                close(c);
                return;
            }
            c.expected = length + 2;
            continue;
        }
        // Reserve worst-case response space before dispatching a potentially mutating request.
        if (c.output.size() - c.sent + 242 > opt_.max_output_bytes) {
            close(c);
            return;
        }
        if (c.sent) {
            c.output.erase(c.output.begin(), c.output.begin() + c.sent);
            c.sent = 0;
        }
        auto answer = bridge_.handle(c.id, {c.input.data() + 2, c.used - 2});
        if (answer[0] == 0x81)
            c.payload = answer[5] | unsigned(answer[6]) << 8;
        c.output.push_back(answer.size() & 255);
        c.output.push_back(answer.size() >> 8);
        c.output.insert(c.output.end(), answer.begin(), answer.end());
        c.used = 0;
        c.expected = 2;
    }
}
void BinaryTcpHost::flush(Conn &c) {
    size_t budget = opt_.write_budget;
    while (c.sent < c.output.size() && budget) {
        auto want = std::min(budget, c.output.size() - c.sent);
        ssize_t n = send(c.fd, c.output.data() + c.sent, want, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                close(c);
            return;
        }
        if (!n) {
            close(c);
            return;
        }
        c.sent += n;
        budget -= n;
    }
    if (c.sent == c.output.size()) {
        c.output.clear();
        c.sent = 0;
    }
}
void BinaryTcpHost::run_once(int timeout_ms) {
    server_.poll();
    bridge_.expire();
    std::vector<pollfd> fds{{listener_, POLLIN, 0}};
    for (auto &c : conns_)
        fds.push_back({c.fd, short(POLLIN | (c.output.empty() ? 0 : POLLOUT)), 0});
    int result = poll(fds.data(), fds.size(), std::clamp(timeout_ms, 0, 100));
    size_t count = conns_.size();
    if (result > 0 && fds[0].revents & POLLIN)
        accept_new();
    auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < count; ++i) {
        auto &c = conns_[i];
        auto ev = fds[i + 1].revents;
        if (c.used && now - c.started >= std::chrono::milliseconds(opt_.incomplete_frame_ms)) {
            close(c);
            continue;
        }
        if (result > 0) {
            if (ev & POLLIN)
                read(c);
            if (c.fd >= 0 && !c.output.empty())
                flush(c);
            if (c.fd >= 0 && (ev & (POLLERR | POLLNVAL | POLLHUP)))
                close(c);
        }
    }
    std::erase_if(conns_, [](const Conn &c) { return c.fd < 0; });
    server_.poll();
}
} // namespace botched::server

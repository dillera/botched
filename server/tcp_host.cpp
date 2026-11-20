#include "tcp_host.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace botched::server {

namespace {
bool set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
}  // namespace

TcpHost::TcpHost(GameServer& server, Options options) : server_(server), opt_(std::move(options)) {}

TcpHost::~TcpHost() {
    for (auto& c : conns_) if (c.fd >= 0) ::close(c.fd);
    if (listen_fd_ >= 0) ::close(listen_fd_);
}

bool TcpHost::listen(std::string& error) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) { error = std::string("socket: ") + std::strerror(errno); return false; }
    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(opt_.port));
    if (::inet_pton(AF_INET, opt_.bind_address.c_str(), &addr.sin_addr) != 1) {
        error = "bad bind address " + opt_.bind_address;
        return false;
    }
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        error = std::string("bind: ") + std::strerror(errno);
        return false;
    }
    if (::listen(listen_fd_, 16) != 0 || !set_nonblocking(listen_fd_)) {
        error = std::string("listen: ") + std::strerror(errno);
        return false;
    }
    socklen_t len = sizeof addr;
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    return true;
}

void TcpHost::accept_new() {
    for (;;) {
        int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) return;  // EAGAIN: no more pending connections
        if (static_cast<int>(conns_.size()) >= opt_.max_clients || !set_nonblocking(fd)) {
            ::close(fd);
            continue;
        }
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        conns_.emplace_back(fd);
    }
}

void TcpHost::close_conn(Conn& c) {
    if (c.fd >= 0) ::close(c.fd);
    c.fd = -1;
    c.dead = true;
}

void TcpHost::flush(Conn& c) {
    while (!c.out.empty() && !c.dead) {
        ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
        if (n > 0) { c.out.erase(0, static_cast<std::size_t>(n)); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n < 0 && errno == EINTR) continue;
        close_conn(c);
        return;
    }
    if (c.out.size() > opt_.max_output_bytes) close_conn(c);  // reader too slow
}

void TcpHost::read_from(Conn& c) {
    char buf[4096];
    ssize_t n = ::recv(c.fd, buf, sizeof buf, 0);
    if (n == 0) { close_conn(c); return; }
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) close_conn(c);
        return;
    }
    std::vector<LineBuffer::Line> lines;
    c.in.feed(buf, static_cast<std::size_t>(n), lines);
    for (auto& line : lines) {
        if (line.too_long) {
            c.out += make_error("REQUEST_TOO_LARGE", "request line is too long").dump();
        } else if (line.text.empty()) {
            continue;
        } else {
            c.out += server_.handle_text(line.text).dump();
        }
        c.out += '\n';
    }
    flush(c);
}

void TcpHost::run_once(int timeout_ms) {
    std::vector<pollfd> fds;
    fds.push_back({listen_fd_, POLLIN, 0});
    for (const auto& c : conns_) fds.push_back({c.fd, static_cast<short>(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});
    int r = ::poll(fds.data(), fds.size(), timeout_ms);
    if (r > 0) {
        const std::size_t existing = conns_.size();
        if (fds[0].revents & POLLIN) accept_new();
        for (std::size_t i = 0; i < existing; ++i) {
            Conn& c = conns_[i];
            const short ev = fds[i + 1].revents;
            if (c.dead) continue;
            if (ev & POLLIN) read_from(c);
            if (!c.dead && (ev & POLLOUT)) flush(c);
            if (!c.dead && (ev & (POLLERR | POLLNVAL)) ) close_conn(c);
            if (!c.dead && (ev & POLLHUP) && !(ev & POLLIN)) close_conn(c);
        }
        for (std::size_t i = conns_.size(); i-- > 0;)
            if (conns_[i].dead) conns_.erase(conns_.begin() + static_cast<long>(i));
    }
    server_.poll();  // planning deadlines advance even when nobody sends anything
}

void TcpHost::run(const std::atomic<bool>& stop, int tick_ms) {
    while (!stop.load()) run_once(tick_ms);
}

}  // namespace botched::server

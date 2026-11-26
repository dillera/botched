#include "binary_connection.hpp"
#include <netdb.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>

namespace botched::client {
using namespace binary;
namespace {
uint32_t word(const Bytes &p, size_t off) {
    Reader r{std::span<const uint8_t>(p).subspan(off)};
    return r.i();
}
unsigned xid(const Bytes &p) { return p[1] | unsigned(p[2]) << 8; }
} // namespace
void BinaryConnection::close_socket() {
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
}
void BinaryConnection::wait(short events, Time deadline) {
    for (;;) {
        auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                .count();
        if (left <= 0)
            throw CodecError("binary I/O timed out");
        pollfd p{fd_, events, 0};
        int n = poll(&p, 1, (int)std::min<int64_t>(left, INT32_MAX));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            throw CodecError("binary poll failed");
        if (n > 0) {
            if (p.revents & events)
                return;
            throw CodecError("binary connection closed");
        }
    }
}
void BinaryConnection::connect_socket(Time deadline) {
    close_socket();
    addrinfo hints{}, *addresses = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    if (getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &addresses))
        throw CodecError("binary host lookup failed");
    for (auto a = addresses; a; a = a->ai_next) {
        fd_ = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd_ < 0)
            continue;
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            close_socket();
            continue;
        }
        int result = connect(fd_, a->ai_addr, a->ai_addrlen);
        try {
            if (result < 0) {
                if (errno != EINPROGRESS)
                    throw CodecError("connect");
                wait(POLLOUT, deadline);
                int error = 0;
                socklen_t n = sizeof error;
                if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &n) || error)
                    throw CodecError("connect");
            }
            break;
        } catch (const CodecError &) {
            close_socket();
        }
    }
    freeaddrinfo(addresses);
    if (fd_ < 0)
        throw CodecError("cannot connect binary endpoint");
    int one = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}
void BinaryConnection::transfer(uint8_t *data, size_t size, bool writing, Time deadline) {
    size_t off = 0;
    while (off < size) {
        wait(writing ? POLLOUT : POLLIN, deadline);
        ssize_t n =
            writing ? send(fd_, data + off, size - off, MSG_NOSIGNAL) : recv(fd_, data + off, size - off, 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (n <= 0)
            throw CodecError("binary connection lost");
        off += n;
    }
}
Bytes BinaryConnection::frame(const Bytes &request, Time deadline) {
    Bytes framed{uint8_t(request.size()), uint8_t(request.size() >> 8)};
    framed.insert(framed.end(), request.begin(), request.end());
    transfer(framed.data(), framed.size(), true, deadline);
    uint8_t prefix[2];
    transfer(prefix, 2, false, deadline);
    unsigned n = prefix[0] | unsigned(prefix[1]) << 8;
    if (n < 3 || n > payload_)
        throw CodecError("invalid binary frame length");
    Bytes reply(n);
    transfer(reply.data(), n, false, deadline);
    if (!bw_validate_packet(reply.data(), reply.size(), payload_) || xid(reply) != xid(request))
        throw CodecError("invalid binary response");
    return reply;
}
void BinaryConnection::hello(bool resume, Time deadline) {
    Fields f;
    f.b(1).b(1).b(0).h(payload_).b(3).i(63).b(resume);
    uint8_t zero[16]{};
    bw_write_bytes(&f.w, resume ? secret_.data() : zero, 16);
    auto reply = frame(packet(1, 0, f.span()), deadline);
    if (reply[0] != 0x81) {
        poisoned_ = resume;
        throw CodecError(resume ? "binary session expired or rejected; pending outcome uncertain, do not "
                                  "repeat lobby mutation"
                                : "binary HELLO refused");
    }
    unsigned selected = reply[5] | unsigned(reply[6]) << 8;
    if (reply[7] != 3 || selected > payload_ || (resume && selected != payload_))
        throw CodecError("binary negotiation changed profile or capacity");
    payload_ = selected;
    Bytes epoch(reply.begin() + 12, reply.begin() + 28);
    unsigned last = reply[44] | unsigned(reply[45]) << 8;
    if (resume) {
        if (epoch != epoch_ || !std::equal(secret_.begin(), secret_.end(), reply.begin() + 28))
            throw CodecError("binary recovery identity changed");
        unsigned expected = pending_.empty() ? next_ - 1 : xid(pending_);
        if (last != expected && (!pending_.empty() ? last + 1 != expected : true))
            throw CodecError("binary recovery exchange mismatch");
    } else {
        epoch_ = epoch;
        secret_.assign(reply.begin() + 28, reply.begin() + 44);
        next_ = 1;
        bw_stream_init(&stream_, stream_.ack_version);
    }
}
bool BinaryConnection::open(const std::string &host, int port, std::string &error, int timeout) {
    close_socket();
    host_ = host;
    port_ = port;
    secret_.clear();
    epoch_.clear();
    pending_.clear();
    binding_ = {};
    event_match_.clear();
    poisoned_ = false;
    bw_stream_init(&stream_, 0);
    try {
        if (payload_ < 128 || payload_ > 240)
            throw CodecError("binary payload range");
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
        connect_socket(end);
        hello(false, end);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        close_socket();
        return false;
    }
}
Bytes BinaryConnection::exchange(const Bytes &request, Time deadline) {
    if (poisoned_)
        throw CodecError("binary recovery required; previous outcome uncertain");
    if (!pending_.empty() && pending_ != request)
        throw CodecError("binary exchange still unresolved");
    pending_ = request;
    Bytes reply;
    try {
        if (fd_ < 0) {
            connect_socket(deadline);
            hello(true, deadline);
        }
        auto first = std::chrono::steady_clock::now();
        first += (deadline - first) / 2;
        reply = frame(pending_, first);
    } catch (const CodecError &) {
        close_socket();
        if (poisoned_)
            throw;
        // One bounded reconnect attempt, reusing the exact request and recovery secret.
        connect_socket(deadline);
        hello(true, deadline);
        reply = frame(pending_, deadline);
    }
    if (reply[0] == 0xff && (reply[3] == 4 || reply[3] == 5 || reply[3] == 6 || reply[3] == 3)) {
        // BAD_BINDING can be a consumed operation refusal, so only sequencing/expiry are fatal here.
        if (reply[3] != 3) {
            poisoned_ = true;
            throw CodecError("binary exchange recovery refused");
        }
    }
    ++next_;
    pending_.clear();
    return reply;
}
Json BinaryConnection::collect(Bytes p, Time deadline) {
    if (p[0] == 0xff)
        throw CodecError("binary adapter fault " + std::to_string(p[3]));
    if (p[0] != 0x82)
        throw CodecError("expected binary PAGE");
    unsigned origin = p[19];
    bool error = p[20] & 2;
    auto high = word(p, 15);
    bw_stream candidate = stream_;
    Bytes data;
    data.reserve(word(p, 11));
    for (;;) {
        int apply = 0;
        if (!bw_stream_accept(&candidate, p.data(), p.size(), &apply))
            throw CodecError("invalid binary page stream");
        data.insert(data.end(), p.begin() + 22, p.end());
        if (p[20] & 1)
            break;
        if (next_ > 65535)
            throw CodecError("binary exchange exhausted during transfer; retry the read in a new session");
        Fields f;
        f.i(word(p, 3)).i(word(p, 7) + p[21]);
        p = exchange(packet(3, next_, f.span()), deadline);
        if (p[0] != 0x82)
            throw CodecError("binary transfer unavailable");
    }
    auto json = decode_response(data, origin, high, error);
    stream_ = candidate;
    return json;
}
void BinaryConnection::rotate(Time deadline) {
    if (next_ <= 65535)
        return;
    if (!pending_.empty())
        throw CodecError("cannot rotate unresolved exchange");
    connect_socket(deadline);
    hello(false, deadline);
    if (binding_.player) {
        Fields f;
        f.b(1).str(binding_.match).i(binding_.player).str(binding_.credential);
        collect(exchange(packet(2, next_, f.span()), deadline), deadline);
    }
}
bool BinaryConnection::bind(const std::string &match, int player, const std::string &token,
                            std::string &error, int timeout) {
    try {
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
        rotate(end);
        Fields f;
        f.b(1).str(match).i(player).str(token);
        collect(exchange(packet(2, next_, f.span()), end), end);
        binding_.match = match;
        binding_.player = player;
        binding_.credential = token;
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        close_socket();
        return false;
    }
}
std::optional<Json> BinaryConnection::request(const Json &j, std::string &error, int timeout) {
    try {
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
        rotate(end);
        auto type = j.get_string("type").value_or("");
        auto match = j.get_string("match_id").value_or("");
        if (type == "GET_EVENTS" && event_match_ != match) {
            event_match_ = match;
            bw_stream_init(&stream_, 0);
        }
        if (type == "SUBMIT_PROGRAM" &&
            (match != binding_.match ||
             (j.find("session") && j.get_string("session") != binding_.credential)))
            throw CodecError("submission does not match bound seat credential");
        auto result = collect(exchange(encode_request(j, next_), end), end);
        if (result.get_string("type") == "JOINED") {
            binding_.match = result.get_string("match_id").value();
            binding_.player = result.get_int("player_id").value();
            binding_.credential = result.get_string("session").value();
        }
        return result;
    } catch (const std::exception &e) {
        error = e.what();
        close_socket();
        return std::nullopt;
    }
}
} // namespace botched::client

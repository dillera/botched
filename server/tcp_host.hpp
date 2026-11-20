// Minimal POSIX TCP host for GameServer: one thread, one poll() loop, JSON lines in and out.
// It adds no game or protocol logic: every line goes to GameServer::handle_text() and the
// response goes back on the same connection. Planning deadlines advance on every loop pass,
// with or without incoming requests.
//
// Reference host for LAN play: no TLS, no authentication beyond the game's session tokens.
#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "line_buffer.hpp"
#include "server.hpp"

namespace botched::server {

class TcpHost {
public:
    struct Options {
        std::string bind_address = "127.0.0.1";
        int port = 7878;                   // 0 = pick a free port (see port())
        int max_clients = 32;              // extra connections are closed at once
        std::size_t max_output_bytes = 256 * 1024;  // slow readers beyond this are disconnected
    };

    TcpHost(GameServer& server, Options options);
    ~TcpHost();
    TcpHost(const TcpHost&) = delete;
    TcpHost& operator=(const TcpHost&) = delete;

    bool listen(std::string& error);
    int port() const { return port_; }  // the bound port, valid after listen()
    std::size_t client_count() const { return conns_.size(); }

    // One pass: wait up to timeout_ms for I/O, serve it, then apply planning deadlines.
    void run_once(int timeout_ms);
    // Loops run_once() until `stop` is set; the idle tick bounds deadline latency.
    void run(const std::atomic<bool>& stop, int tick_ms = 100);

private:
    struct Conn {
        int fd;
        LineBuffer in;
        std::string out;
        bool dead = false;
        Conn(int f) : fd(f), in(kMaxRequestBytes + 2) {}
    };

    void accept_new();
    void read_from(Conn& c);
    void flush(Conn& c);
    void close_conn(Conn& c);

    GameServer& server_;
    Options opt_;
    int listen_fd_ = -1;
    int port_ = 0;
    std::vector<Conn> conns_;
};

}  // namespace botched::server

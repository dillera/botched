#pragma once
#include "binary_bridge.hpp"
#include <array>
#include <chrono>

namespace botched::server {
// Separate binary listener; call on the same thread as the JSON host and GameServer.
class BinaryTcpHost {
  public:
    struct Options {
        std::string bind_address = "127.0.0.1";
        int port = 7879;
        size_t max_clients = 32, max_output_bytes = 4096;
        size_t read_budget = 4096, write_budget = 4096;
        int incomplete_frame_ms = 10000;
        int send_buffer_bytes = 0; // optional OS send buffer limit
    };
    BinaryTcpHost(GameServer &, const Clock &, Options);
    ~BinaryTcpHost();
    BinaryTcpHost(const BinaryTcpHost &) = delete;
    BinaryTcpHost &operator=(const BinaryTcpHost &) = delete;
    bool listen(std::string &);
    int port() const { return port_; }
    size_t client_count() const { return conns_.size(); }
    size_t queued_bytes() const;
    void run_once(int timeout_ms);

  private:
    struct Conn {
        int fd;
        binary::Bridge::Connection id;
        std::array<uint8_t, 242> input{};
        size_t used = 0, expected = 2;
        unsigned payload = 240;
        binary::Bytes output;
        size_t sent = 0;
        std::chrono::steady_clock::time_point started{};
    };
    GameServer &server_;
    binary::Bridge bridge_;
    Options opt_;
    int listener_ = -1, port_ = 0;
    uint64_t next_connection_ = 1;
    std::vector<Conn> conns_;
    void close(Conn &);
    void accept_new();
    void read(Conn &);
    void flush(Conn &);
};
} // namespace botched::server

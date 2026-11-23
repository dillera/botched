#pragma once
#include "binary_codec.hpp"
#include <chrono>
#include <optional>

namespace botched::client {
// POSIX reference transport. Host-side JSON reconstruction is intentionally not a retro mirror.
// One synchronous caller, one outstanding exchange. Never repeats an uncertain lobby mutation
// after lease loss. close_socket() keeps recovery state; open() starts a new logical connection.
class BinaryConnection {
  public:
    explicit BinaryConnection(unsigned payload = 240) : payload_(payload) { bw_stream_init(&stream_, 0); }
    ~BinaryConnection() { close_socket(); }
    BinaryConnection(const BinaryConnection &) = delete;
    BinaryConnection &operator=(const BinaryConnection &) = delete;
    bool open(const std::string &, int, std::string &, int timeout_ms = 10000);
    std::optional<Json> request(const Json &, std::string &, int timeout_ms = 10000);
    bool bind(const std::string &, int, const std::string &, std::string &, int timeout_ms = 10000);
    void close_socket();
    uint32_t acknowledged_version() const { return stream_.ack_version; }

  private:
    using Time = std::chrono::steady_clock::time_point;
    int fd_ = -1, port_ = 0;
    std::string host_, event_match_;
    unsigned payload_, next_ = 1;
    binary::Bytes secret_, epoch_, pending_;
    binary::Binding binding_;
    bw_stream stream_{};
    bool poisoned_ = false;
    void connect_socket(Time);
    void wait(short, Time);
    void transfer(uint8_t *, size_t, bool, Time);
    binary::Bytes frame(const binary::Bytes &, Time);
    void hello(bool, Time);
    binary::Bytes exchange(const binary::Bytes &, Time);
    Json collect(binary::Bytes, Time);
    void rotate(Time);
};
} // namespace botched::client

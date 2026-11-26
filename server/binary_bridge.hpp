#pragma once
#include "binary_codec.hpp"
#include "server.hpp"
#include <array>
#include <map>
#include <memory>

namespace botched::binary {
// B2 only: complete payloads in/out. No sockets, stream framing, host threads or firmware.
// Hosts must serialize calls and continue polling GameServer independently (B3).
class Bridge {
  public:
    using Connection = std::uint64_t;
    struct Limits {
        std::size_t sessions = 64, transfer_bytes = BW_MAX_TRANSFER, global_bytes = 64u << 20;
    };
    Bridge(server::GameServer &, const server::Clock &);
    Bridge(server::GameServer &, const server::Clock &, Limits);
    ~Bridge();
    Bytes handle(Connection, std::span<const std::uint8_t>);
    void disconnect(Connection); // retains resumable session, binding, last exchange and transfer
    void expire();
    std::size_t transfer_storage() const;
    std::size_t session_count() const;

  private:
    struct Session;
    server::GameServer &server_;
    const server::Clock &clock_;
    Limits limits_;
    std::array<std::uint8_t, 16> epoch_{};
    std::vector<std::unique_ptr<Session>> sessions_;
    std::map<std::string, std::uint32_t> map_handles_;
    std::uint32_t next_map_ = 1;
    std::uint32_t map_handle(const std::string &);
    Bytes hello(Connection, std::span<const std::uint8_t>);
    Bytes dispatch(Session &, std::span<const std::uint8_t>);
    Bytes page(const Session &, unsigned exchange, std::uint32_t offset) const;
};
} // namespace botched::binary

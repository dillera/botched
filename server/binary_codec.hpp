#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>
#include "botched/json.hpp"
#include "botched/map.hpp"
#include "botched/wire/v1.h"

namespace botched::binary {
using Bytes = std::vector<std::uint8_t>;
struct CodecError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct Reader {
    bw_reader r;
    explicit Reader(std::span<const std::uint8_t> s) { bw_reader_init(&r, s.data(), s.size()); }
    unsigned b() { return bw_read_u8(&r); }
    unsigned h() { return bw_read_u16(&r); }
    std::uint32_t i() { return bw_read_u32(&r); }
    std::int32_t j() { return bw_read_i32(&r); }
    int coord() {
        auto v = b();
        return v < 128 ? static_cast<int>(v) : static_cast<int>(v) - 256;
    }
    std::int64_t q();
    std::string str();
};
// An output stream is capped before each append. No over-limit response is partially published.
class Records {
  public:
    explicit Records(std::size_t limit = BW_MAX_TRANSFER) : limit_(limit) {
        bytes_.reserve(limit < 4096 ? limit : 4096);
    }
    void add(std::uint8_t tag, std::span<const std::uint8_t> body);
    void text(unsigned field, const std::string &value);
    const Bytes &bytes() const { return bytes_; }
    Bytes take() { return std::move(bytes_); }

  private:
    Bytes bytes_;
    std::size_t limit_;
};
struct Fields {
    std::uint8_t data[99]{};
    bw_writer w;
    Fields() { reset(); }
    Fields(const Fields &) = delete;
    Fields &operator=(const Fields &) = delete;
    void reset() { bw_writer_init(&w, data, sizeof data); }
    Fields &b(unsigned v);
    Fields &h(unsigned v);
    Fields &i(std::uint32_t v);
    Fields &q(std::int64_t v);
    Fields &str(const std::string &s);
    Fields &key(const std::string &s);
    std::span<const std::uint8_t> span() const;
};
struct Binding {
    std::string match, credential;
    int player = 0;
    std::uint32_t next_key = 0;
    bool exhausted = false;
};
struct Command {
    Json json;
    std::string match;
    unsigned max_versions = 16;
};
Command decode_request(std::span<const std::uint8_t> packet, const Binding &binding);
Bytes encode_request(const Json &request, std::uint16_t exchange, bool bound_scope = false);
void encode_event(Records &, const Json &event);
Json decode_event(std::span<const std::uint8_t> record, const std::string &match);
std::uint32_t profile_bytes(const Map &);
void encode_map_meta(Records &, const Map &, std::uint32_t handle);
void encode_response(Records &, const Json &, const Map *, std::uint32_t map_handle,
                     const Binding *binding = nullptr,
                     const std::function<std::uint32_t(const std::string &)> &map_id = {});
// Host/reference reconstruction of a complete validated stream. Not an 8-bit allocation API.
Json decode_response(std::span<const std::uint8_t> stream, unsigned origin, std::uint32_t high_water,
                     bool error = false);
Bytes packet(unsigned opcode, unsigned exchange, std::span<const std::uint8_t> body);
const char *request_name(unsigned opcode);
} // namespace botched::binary

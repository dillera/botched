#pragma once
// Host acceptance helper only. Portable production state lives in bn_client, not this heap-backed decoder.
#include "botched/client/nio.h"
#include "binary_codec.hpp"
#include <chrono>
#include <thread>
#include <stdexcept>
namespace botched::test {
inline uint32_t nio_now() {
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
inline void ensure(bool yes, const std::string &message) {
    if (!yes)
        throw std::runtime_error(message);
}
struct NioPeer {
    bn_client c{};
    std::string url;
    size_t pages = 0, bytes = 0, max_payload = 0;
    NioPeer(const bn_ops *ops, void *ctx, std::string endpoint, unsigned payload = 128)
        : url(std::move(endpoint)) {
        ensure(bn_init(&c, ops, ctx, url.c_str(), payload, 3, nio_now(), 30000), "NIO init");
        pump(BN_READY);
    }
    ~NioPeer() { bn_dispose(&c); }
    void pump(int expected) {
        int rc;
        do {
            rc = bn_step(&c, nio_now());
            if (rc == BN_WAIT)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (rc == BN_WAIT);
        ensure(rc == expected,
               "NIO failure stage=" + std::to_string(c.phase) + " error=" + std::to_string(c.error));
    }
    binary::Bytes exchange(const binary::Bytes &p) {
        ensure(bn_request(&c, p.data(), p.size(), nio_now()), "NIO request refused");
        pump(BN_RESPONSE);
        return {c.reply + 2, c.reply + 2 + c.reply_size};
    }
    Json call(const Json &q) {
        return collect(exchange(binary::encode_request(q, (uint16_t)c.next_exchange)));
    }
    Json collect(binary::Bytes p) {
        ensure(p[0] == 0x82, "expected NIO PAGE");
        unsigned origin = p[19];
        bool error = p[20] & 2;
        binary::Reader r(std::span<const uint8_t>(p).subspan(15));
        auto high = r.i();
        binary::Bytes data;
        for (;;) {
            ++pages;
            bytes += p.size();
            max_payload = std::max(max_payload, p.size());
            data.insert(data.end(), p.begin() + 22, p.end());
            if (p[20] & 1)
                break;
            binary::Reader reader(std::span<const uint8_t>(p).subspan(3));
            auto id = reader.i(), offset = reader.i();
            binary::Fields f;
            f.i(id).i(offset + p[21]);
            p = exchange(binary::packet(3, c.next_exchange, f.span()));
            ensure(p[0] == 0x82, "NIO FETCH");
        }
        return binary::decode_response(data, origin, high, error);
    }
};
} // namespace botched::test

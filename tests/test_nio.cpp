#include "nio_peer.hpp"
#include "binary_bridge.hpp"
#include "doctest.h"
#include <map>
#include <cstring>
#include <iostream>
using namespace botched;
using namespace botched::binary;
namespace {
Json query(const char *name, const std::string &match = "") {
    auto j = Json::object();
    j.set("type", name);
    if (!match.empty())
        j.set("match_id", match);
    return j;
}
struct Service {
    server::ManualClock clock;
    server::GameServer server{server::ServerConfig{}, clock};
    Bridge bridge{server, clock};
    struct Stream {
        Bytes input, output, cache;
        uint32_t tx = 0, rx = 0, cached_offset = 0;
        uint16_t cached_size = 0;
    };
    std::map<uint16_t, Stream> streams;
    uint16_t next = 1;
    unsigned calls = 0, opens = 0, closes = 0, replays = 0, again = 0, partial_read = 0, partial_write = 0;
    uint8_t drop_write = 0;
    bool expire_on_drop = false;
    bool drop_read = false, broken = false, bad_count = false, close_busy = false;
    unsigned max_chunk = 0;
    static Service &get(void *p) { return *static_cast<Service *>(p); }
    bool busy() {
        if (++calls % 7 == 0) {
            ++again;
            return true;
        }
        return false;
    }
    static uint8_t open(void *p, const char *, uint16_t *handle) {
        auto &s = get(p);
        *handle = 0;
        if (s.busy())
            return BN_IO_AGAIN;
        *handle = s.next++;
        s.streams.emplace(*handle, Stream{});
        ++s.opens;
        return BN_IO_OK;
    }
    static uint8_t info(void *p, uint16_t, uint8_t *flags) {
        auto &s = get(p);
        if (s.busy())
            return BN_IO_AGAIN;
        *flags = BN_INFO_READY;
        return BN_IO_OK;
    }
    static uint8_t close(void *p, uint16_t h) {
        auto &s = get(p);
        if (s.close_busy) {
            s.close_busy = false;
            return BN_IO_AGAIN;
        }
        s.bridge.disconnect(h);
        s.streams.erase(h);
        ++s.closes;
        return BN_IO_OK;
    }
    static uint8_t write(void *p, uint16_t h, uint32_t offset, const uint8_t *data, uint16_t wanted,
                         uint16_t *count) {
        auto &s = get(p);
        *count = 0;
        s.max_chunk = std::max<unsigned>(s.max_chunk, wanted);
        if (s.busy())
            return BN_IO_AGAIN;
        auto &stream = s.streams.at(h);
        CHECK(offset == stream.tx);
        if (s.broken) {
            s.broken = false;
            return BN_IO_LOST;
        }
        *count = std::min<uint16_t>(wanted, 3);
        if (*count < wanted)
            ++s.partial_write;
        stream.tx += *count;
        stream.input.insert(stream.input.end(), data, data + *count);
        if (stream.input.size() >= 2) {
            size_t n = stream.input[0] | unsigned(stream.input[1]) << 8;
            if (stream.input.size() == n + 2) {
                auto op = stream.input[2];
                auto reply = s.bridge.handle(h, {stream.input.data() + 2, n});
                stream.output.push_back(reply.size());
                stream.output.push_back(0);
                stream.output.insert(stream.output.end(), reply.begin(), reply.end());
                stream.input.clear();
                if (s.drop_write == op) {
                    s.drop_write = 0;
                    if (s.expire_on_drop)
                        s.clock.advance_ms(1800001);
                    *count = 0;
                    return BN_IO_LOST;
                }
            }
        }
        return BN_IO_OK;
    }
    static uint8_t read(void *p, uint16_t h, uint32_t offset, uint8_t *data, uint16_t wanted, uint16_t *count,
                        uint8_t *flags) {
        auto &s = get(p);
        *count = 0;
        *flags = 0;
        s.max_chunk = std::max<unsigned>(s.max_chunk, wanted);
        if (s.busy())
            return BN_IO_AGAIN;
        if (s.bad_count) {
            *count = wanted + 1;
            s.bad_count = false;
            return BN_IO_OK;
        }
        auto &stream = s.streams.at(h);
        if (offset < stream.rx) {
            CHECK(offset == stream.cached_offset);
            CHECK(wanted == stream.cached_size);
            ++s.replays;
            *count = stream.cache.size();
            std::memcpy(data, stream.cache.data(), *count);
            return BN_IO_OK;
        }
        CHECK(offset == stream.rx);
        if (stream.output.empty())
            return BN_IO_AGAIN;
        *count = std::min<size_t>(std::min<uint16_t>(wanted, 5), stream.output.size());
        if (*count < wanted)
            ++s.partial_read;
        stream.cache.assign(stream.output.begin(), stream.output.begin() + *count);
        stream.cached_size = wanted;
        stream.cached_offset = offset;
        stream.output.erase(stream.output.begin(), stream.output.begin() + *count);
        stream.rx += *count;
        if (s.drop_read) {
            s.drop_read = false;
            *count = 0;
            return BN_IO_LOST;
        }
        std::memcpy(data, stream.cache.data(), *count);
        return BN_IO_OK;
    }
    static const bn_ops ops;
};
const bn_ops Service::ops{Service::open, Service::info, Service::write, Service::read, Service::close};
} // namespace
TEST_CASE("B4: bounded C adapter handles partial counts busy read replay and ambiguous writes") {
    Service service;
    test::NioPeer peer(&Service::ops, &service, "tcp://test:7879");
    service.drop_read = true;
    auto maps = peer.call(query("LIST_MAPS"));
    CHECK(maps == service.server.handle(query("LIST_MAPS")));
    CHECK(service.replays == 1);
    service.drop_write = 0x11;
    auto create = query("CREATE_MATCH");
    create.set("player_count", 2);
    auto made = peer.call(create);
    CHECK(made.get_string("match_id") == "m1");
    service.drop_write = 0x12;
    auto seat = peer.call(query("JOIN_MATCH", "m1"));
    CHECK(seat.get_int("player_id") == 1);
    CHECK(service.server.handle(query("JOIN_MATCH", "m1")).get_int("player_id") == 2);
    service.drop_read = true;
    CHECK(peer.call(query("GET_SNAPSHOT", "m1")) == service.server.handle(query("GET_SNAPSHOT", "m1")));
    auto events = query("GET_EVENTS", "m1");
    events.set("since_version", 0);
    CHECK(peer.call(events) == service.server.handle(events));
    auto ack = peer.c.stream.ack_version;
    REQUIRE(ack > 0);
    service.broken = true;
    CHECK(peer.call(query("GET_MATCH_STATUS", "m1")) ==
          service.server.handle(query("GET_MATCH_STATUS", "m1")));
    CHECK(peer.c.stream.ack_version == ack);
    events.set("since_version", INT32_MAX);
    peer.call(events);
    CHECK(peer.c.stream.ack_version == ack);
    CHECK(service.max_chunk <= 32);
    CHECK(service.partial_read > 0);
    CHECK(service.partial_write > 0);
    CHECK(service.again > 0);
    CHECK(service.opens == 4);
    service.close_busy = true;
    CHECK_FALSE(bn_dispose(&peer.c));
    CHECK(peer.c.handle != 0);
    CHECK(bn_dispose(&peer.c));
    CHECK(service.streams.empty());
    CHECK(service.opens == service.closes);
}
TEST_CASE("B4: malformed service counts timeout and cursor exhaustion cannot advance acknowledgement") {
    Service s;
    test::NioPeer peer(&Service::ops, &s, "tcp://test:7879");
    peer.c.stream.ack_version = 100;
    auto command = encode_request(query("LIST_MAPS"), peer.c.next_exchange);
    auto before = peer.c;
    CHECK_FALSE(bn_request(&peer.c, command.data(), 2, test::nio_now()));
    CHECK(std::memcmp(&before, &peer.c, sizeof before) == 0);
    REQUIRE(bn_request(&peer.c, command.data(), command.size(), test::nio_now()));
    s.bad_count = true;
    int rc = BN_WAIT;
    for (unsigned i = 0; i < 1000 && rc == BN_WAIT; ++i)
        rc = bn_step(&peer.c, test::nio_now());
    CHECK(rc == BN_FAILED);
    CHECK(peer.c.error == BN_ERROR_PROTOCOL);
    CHECK(peer.c.stream.ack_version == 100);
    CHECK_FALSE(bn_reconnect(&peer.c, test::nio_now()));
    REQUIRE(bn_dispose(&peer.c));
    REQUIRE(bn_init(&peer.c, &Service::ops, &s, peer.url.c_str(), 128, 3, UINT32_MAX - 10, 20));
    CHECK(bn_step(&peer.c, 15) == BN_FAILED);
    CHECK(peer.c.error == BN_ERROR_TIMEOUT);
    CHECK(peer.c.handle == 0);
}
TEST_CASE("B4: URL bounds and retained request ownership are explicit") {
    bn_client c{};
    Service s;
    std::string long_url = "tcp://" + std::string(225, 'x');
    CHECK_FALSE(bn_init(&c, &Service::ops, &s, long_url.c_str(), 240, 3, 0, 100));
    CHECK_FALSE(bn_init(&c, &Service::ops, &s, "http://wrong", 240, 3, 0, 100));
    CHECK_FALSE(bn_init(&c, &Service::ops, &s, "tcp://host:123", 127, 3, 0, 100));
    test::NioPeer peer(&Service::ops, &s, "tcp://test:7879");
    auto command = encode_request(query("LIST_MAPS"), 1);
    REQUIRE(bn_request(&peer.c, command.data(), command.size(), test::nio_now()));
    auto original = command;
    command[0] = 0x7f;
    CHECK(std::memcmp(peer.c.request + 2, original.data(), original.size()) == 0);
    CHECK_FALSE(bn_request(&peer.c, command.data(), command.size(), test::nio_now()));
    peer.pump(BN_RESPONSE);
    CHECK(peer.c.reply[2] == 0x82);
    peer.c.next_exchange = 65536;
    CHECK_FALSE(bn_request(&peer.c, original.data(), original.size(), test::nio_now()));
    std::cout << "B4 host allocation: bn_client=" << sizeof(bn_client) << " request=" << sizeof(c.request)
              << " reply=" << sizeof(c.reply) << " hello=" << sizeof(c.hello) << "\n";
}
TEST_CASE("B4: stream cursor overflow closes and resumes instead of wrapping") {
    for (bool write : {true, false}) {
        Service s;
        test::NioPeer peer(&Service::ops, &s, "tcp://test:7879");
        peer.c.stream.ack_version = 100;
        if (write)
            peer.c.write_cursor = UINT32_MAX - 1;
        else
            peer.c.read_cursor = UINT32_MAX - 1;
        CHECK(peer.call(query("LIST_MAPS")) == s.server.handle(query("LIST_MAPS")));
        CHECK(peer.c.stream.ack_version == 100);
        CHECK(s.opens == 2);
        CHECK(s.closes == 1);
    }
}

TEST_CASE("B4: expired recovery never repeats an ambiguous JOIN") {
    Service s;
    test::NioPeer peer(&Service::ops, &s, "tcp://test:7879");
    auto create = query("CREATE_MATCH");
    create.set("player_count", 2);
    peer.call(create);
    s.drop_write = 0x12;
    s.expire_on_drop = true;
    CHECK_THROWS(peer.call(query("JOIN_MATCH", "m1")));
    CHECK(peer.c.error == BN_ERROR_SESSION);
    CHECK(peer.c.pending == 1);
    CHECK_FALSE(bn_reconnect(&peer.c, test::nio_now()));
    CHECK(s.server.handle(query("GET_MATCH_STATUS", "m1")).find("status")->get_int("joined") == 1);
    CHECK(s.server.handle(query("JOIN_MATCH", "m1")).get_int("player_id") == 2);
}

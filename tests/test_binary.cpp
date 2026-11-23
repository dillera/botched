#include "binary_bridge.hpp"
#include "pool_config.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"
#include <fstream>
#include <sstream>
#include <set>
#include <random>
#include <cstring>
#include <iostream>

using namespace botched;
using namespace botched::binary;
namespace {
Json request(const char *type, const std::string &id = "") {
    Json j = Json::object();
    j.set("type", type);
    if (!id.empty())
        j.set("match_id", id);
    return j;
}
Json with(Json j, const char *key, Json value) {
    j.set(key, std::move(value));
    return j;
}
Bytes hex(const std::string &s) {
    std::istringstream in(s);
    unsigned n;
    Bytes b;
    while (in >> std::hex >> n)
        b.push_back((uint8_t)n);
    return b;
}
Json fixtures() {
    std::ifstream in(BOTCHED_WIRE_FIXTURES);
    std::string s((std::istreambuf_iterator<char>(in)), {});
    auto j = Json::parse(s);
    REQUIRE(j.has_value());
    return *j;
}
std::uint32_t u32(const Bytes &b, size_t off) {
    Reader r{std::span<const uint8_t>(b).subspan(off)};
    return r.i();
}
unsigned xid(const Bytes &p) { return p[1] | (unsigned)p[2] << 8; }
struct Client {
    Bridge &bridge;
    Bridge::Connection connection;
    uint16_t next = 1;
    unsigned capacity = 240, profile = 3;
    Bytes secret;
    size_t measured_pages = 0, measured_bytes = 0, measured_max = 0;
    bw_stream tracker{};
    explicit Client(Bridge &b, unsigned c, unsigned p = 240, unsigned prof = 3)
        : bridge(b), connection(c), capacity(p), profile(prof) {
        bw_stream_init(&tracker, 0);
        hello();
    }
    Bytes hello(bool resume = false) {
        Fields f;
        f.b(1).b(1).b(0).h(capacity).b(profile).i(63).b(resume);
        uint8_t zero[16]{};
        bw_write_bytes(&f.w, resume ? secret.data() : zero, 16);
        auto p = bridge.handle(connection, packet(1, 0, f.span()));
        REQUIRE(p[0] == 0x81);
        secret.assign(p.begin() + 28, p.begin() + 44);
        return p;
    }
    Bytes raw(const Bytes &p) { return bridge.handle(connection, p); }
    Json gather(Bytes p) {
        REQUIRE(p[0] == 0x82);
        measured_pages = measured_bytes = measured_max = 0;
        unsigned origin = p[19];
        bool error = p[20] & 2;
        auto h = u32(p, 15);
        Bytes data;
        for (;;) {
            ++measured_pages;
            measured_bytes += p.size();
            measured_max = std::max(measured_max, p.size());
            int apply = 0;
            REQUIRE(bw_stream_accept(&tracker, p.data(), p.size(), &apply));
            data.insert(data.end(), p.begin() + 22, p.end());
            if (p[20] & 1)
                break;
            Fields f;
            f.i(u32(p, 3)).i(u32(p, 7) + p[21]);
            p = raw(packet(3, next++, f.span()));
            REQUIRE(p[0] == 0x82);
        }
        return decode_response(data, origin, h, error);
    }
    Json call(const Json &j) { return gather(raw(encode_request(j, next++))); }
    Json bind(const std::string &m, int player, const std::string &token) {
        Fields f;
        f.b(1).str(m).i(player).str(token);
        return gather(raw(packet(2, next++, f.span())));
    }
};
server::ServerConfig config() {
    server::ServerConfig c;
    c.deterministic_token_seed = 42;
    return c;
}
Json program(const std::string &m, int p, int round, int rev, bool ready, const std::string &key = "") {
    auto j = request("SUBMIT_PROGRAM", m);
    j.set("player_id", p);
    j.set("round", round);
    j.set("revision", rev);
    j.set("ready", ready);
    Json a = Json::array();
    a.push("WAIT");
    a.push("WAIT");
    a.push("WAIT");
    j.set("orders", a);
    if (!key.empty())
        j.set("request_id", key);
    return j;
}
} // namespace
TEST_CASE("binary: frozen fixtures, atomic bounded encoders and every truncation") {
    auto f = fixtures();
    std::set<unsigned> events, requests;
    size_t max_packet = 0, max_record = 0;
    for (const auto &e : f.find("examples")->items()) {
        auto name = e.get_string("name").value();
        auto kind = e.get_string("kind").value();
        auto b = hex(e.get_string("hex").value());
        CAPTURE(name);
        CHECK(b.size() == e.get_int("bytes").value());
        if (kind == "record") {
            REQUIRE(bw_validate_record(b.data(), b.size()));
            max_record = std::max(max_record, b.size());
            if (b[0] == 21) {
                events.insert(b[2]);
                Records r;
                encode_event(r, decode_event(b, "m1"));
                CHECK(r.bytes() == b);
            }
            uint8_t out[256];
            std::memset(out, 0xa5, sizeof out);
            size_t written = 999;
            REQUIRE(bw_encode_record(b[0], b.data() + 2, b.size() - 2, out, sizeof out, &written));
            CHECK(Bytes(out, out + written) == b);
            for (size_t n = 0; n < b.size(); ++n)
                CHECK_FALSE(bw_validate_record(b.data(), n));
            std::memset(out, 0xa5, sizeof out);
            CHECK_FALSE(bw_encode_record(b[0], b.data() + 2, b.size() - 2, out, b.size() - 1, &written));
            CHECK(out[0] == 0xa5);
        } else if (kind == "payload") {
            REQUIRE(bw_validate_packet(b.data(), b.size(), 240));
            max_packet = std::max(max_packet, b.size());
            if (b[0] >= 16 && b[0] <= 24)
                requests.insert(b[0]);
            uint8_t out[240];
            size_t written = 0;
            REQUIRE(bw_encode_packet(b[0], xid(b), b.data() + 3, b.size() - 3, out, sizeof out, &written));
            CHECK(Bytes(out, out + written) == b);
            for (size_t n = 0; n < b.size(); ++n)
                CHECK_FALSE(bw_validate_packet(b.data(), n, 240));
            auto longer = b;
            longer.push_back(0);
            CHECK_FALSE(bw_validate_packet(longer.data(), longer.size(), 240));
        } else if (kind == "invalid")
            CHECK_FALSE(bw_validate_packet(b.data(), b.size(), 240));
    }
    CHECK(events.size() == 30);
    CHECK(requests.size() == 9);
    CHECK(max_record == 101);
    CHECK(max_packet <= 240);
}
TEST_CASE("binary: exchange replay recovers lost JOIN without dispatch or new seat") {
    server::ManualClock clock;
    server::GameServer s(config(), clock);
    Bridge b(s, clock);
    Client c(b, 1);
    auto create = request("CREATE_MATCH");
    create.set("player_count", 2);
    create.set("planning_seconds", 0);
    auto made = c.call(create);
    auto m = made.get_string("match_id").value();
    auto join = encode_request(request("JOIN_MATCH", m), c.next++);
    auto original = c.raw(join);
    REQUIRE(original[0] == 0x82);
    auto changed = join;
    changed.back() = '2';
    CHECK(c.raw(changed)[3] == 5);
    CHECK(c.raw(join) == original);
    b.disconnect(1);
    c.connection = 2;
    auto hello = c.hello(true);
    CHECK((hello[44] | hello[45] << 8) == xid(join));
    CHECK(c.raw(join) == original);
    auto joined = c.gather(original);
    CHECK(joined.get_int("player_id") == 1);
    auto status = s.handle(request("GET_MATCH_STATUS", m));
    CHECK(status.find("status")->get_int("joined") == 1);
    CHECK(b.handle(1, join)[3] == 3); // taken-over/disconnected connection no longer owns session
    auto badseq = encode_request(request("GET_MATCH_STATUS", m), c.next + 1);
    CHECK(c.raw(badseq)[3] == 6);
    auto bad = Bytes{0x7f, (uint8_t)c.next, 0};
    ++c.next;
    CHECK(c.raw(bad)[3] == 2);
    CHECK(c.raw(bad)[3] == 2);
    CHECK(c.call(request("GET_MATCH_STATUS", m)).find("status")->get_int("joined") == 1);
}
TEST_CASE("binary: mixed JSON submissions share H3 and frozen replies") {
    server::ManualClock clock;
    server::GameServer s(config(), clock);
    Bridge b(s, clock);
    Client c(b, 1);
    auto create = request("CREATE_MATCH");
    create.set("player_count", 2);
    create.set("planning_seconds", 0);
    auto made = c.call(create);
    auto m = made.get_string("match_id").value();
    auto j1 = c.call(request("JOIN_MATCH", m));
    auto token = j1.get_string("session").value();
    s.handle(request("JOIN_MATCH", m));
    auto submit = program(m, 1, 1, 1, false, "b:7");
    auto first = c.call(submit);
    REQUIRE(first.get_bool("ok").value());
    auto ver = first.get_int("state_version");
    submit.set("session", token);
    auto retry = s.handle(submit);
    CHECK(retry == first);
    CHECK(c.call(submit) == first);
    CHECK(s.find_match(m)->state_version() == ver);
    submit.set("revision", 2);
    CHECK(c.call(submit).get_string("code") == "REQUEST_ID_REUSED");
    submit.set("request_id", "b:4294967295");
    CHECK(c.call(submit).get_bool("ok").value());
    c.bind(m, 1, token);
    Fields fields;
    fields.b(1).str(m).i(1).str(token);
    auto response = c.raw(packet(2, c.next++, fields.span()));
    REQUIRE(response[0] == 0x82);
    CHECK(response[response.size() - 1] == 1); // exhausted hint
}
TEST_CASE("binary: immutable snapshot pages and eventless/future watermark never regress") {
    server::ManualClock clock;
    server::GameServer s(config(), clock);
    Bridge b(s, clock);
    Client c(b, 1, 128);
    auto create = request("CREATE_MATCH");
    create.set("player_count", 2);
    create.set("planning_seconds", 0);
    auto m = c.call(create).get_string("match_id").value();
    auto token = c.call(request("JOIN_MATCH", m)).get_string("session").value();
    auto other = s.handle(request("JOIN_MATCH", m)).get_string("session").value();
    auto snapshot_expected = s.handle(request("GET_SNAPSHOT", m));
    auto snap = c.raw(encode_request(request("GET_SNAPSHOT", m), c.next++));
    REQUIRE(!(snap[20] & 1));
    auto submit = program(m, 2, 1, 1, false);
    submit.set("session", other);
    s.handle(submit);
    CHECK(c.gather(snap) == snapshot_expected); // pages reflect pre-submission submitted/ready sets
    auto events = request("GET_EVENTS", m);
    events.set("since_version", 0);
    auto all = c.call(events);
    auto ack = c.tracker.ack_version;
    CHECK(ack == (uint32_t)s.find_match(m)->state_version());
    submit.set("revision", 2);
    s.handle(submit);
    events.set("since_version", (int)ack);
    auto empty = c.call(events);
    CHECK(empty.find("events")->items().empty());
    CHECK(c.tracker.ack_version == ack + 1);
    auto future = events;
    future.set("since_version", 2147483647);
    CHECK(c.call(future).find("events")->items().empty());
    CHECK(c.tracker.ack_version == ack + 1);
    // A stale immutable event window captured before a JSON mutation must not lower the ack.
    events.set("since_version", 0);
    auto old = c.raw(encode_request(events, c.next++));
    auto before = c.tracker.ack_version;
    c.gather(old);
    CHECK(c.tracker.ack_version == before);
    auto old_copy = c.tracker;
    int apply = 1;
    REQUIRE(bw_stream_accept(&c.tracker, old.data(), old.size(), &apply));
    CHECK(!apply);
    CHECK(c.tracker.ack_version == before);
    CHECK(c.tracker.next_offset == old_copy.next_offset);
    // Successful status/snapshot transfers with older/no event watermark cannot change it either.
    c.call(request("GET_MATCH_STATUS", m));
    c.call(request("GET_SNAPSHOT", m));
    CHECK(c.tracker.ack_version == before);
}
TEST_CASE("binary: all maps snapshots statuses and full matches equal JSON") {
    for (const char *map : {"training-01", "foundry-01", "laserworks-01", "trapworks-01", "turretworks-01",
                            "tarworks-01", "shuntworks-01", "crossroads-01", "factory-01"}) {
        CAPTURE(map);
        server::ManualClock clock;
        server::GameServer left(config(), clock), right(config(), clock);
        Bridge bridge(right, clock);
        Client c(bridge, 1, 128);
        CHECK(c.call(request("LIST_MAPS")) == left.handle(request("LIST_MAPS")));
        auto create = request("CREATE_MATCH");
        create.set("map_id", map);
        create.set("player_count", 4);
        create.set("bots", 3);
        create.set("planning_seconds", 0);
        create.set("rounds", 3);
        auto made = left.handle(create);
        CHECK(c.call(create) == made);
        auto m = made.get_string("match_id").value();
        CHECK(c.call(request("GET_MATCH_STATUS", m)) == left.handle(request("GET_MATCH_STATUS", m)));
        auto joined = left.handle(request("JOIN_MATCH", m));
        CHECK(c.call(request("JOIN_MATCH", m)) == joined);
        CHECK(c.call(request("GET_MAP", m)) == left.handle(request("GET_MAP", m)));
        for (int round = 1; round <= 3; ++round) {
            CHECK(c.call(request("GET_SNAPSHOT", m)) == left.handle(request("GET_SNAPSHOT", m)));
            CHECK(c.call(request("GET_MATCH_STATUS", m)) == left.handle(request("GET_MATCH_STATUS", m)));
            auto q = program(m, 1, round, 1, true, "r:" + std::to_string(round));
            q.set("session", joined.get_string("session").value());
            auto expected = left.handle(q);
            CHECK(c.call(q) == expected);
            CHECK(right.find_match(m)->state_hash() == left.find_match(m)->state_hash());
        }
        auto want = left.handle(with(request("GET_EVENTS", m), "since_version", 0));
        Json combined = Json::array();
        uint32_t since = 0;
        while (since < (uint32_t)right.find_match(m)->state_version()) {
            auto e =
                c.call(with(with(request("GET_EVENTS", m), "since_version", (int)since), "max_versions", 1));
            for (const auto &item : e.find("events")->items())
                combined.push(item);
            since = (uint32_t)e.get_int("state_version").value();
        }
        CHECK(combined == *want.find("events"));
        CHECK(c.call(request("GET_SNAPSHOT", m)) == left.handle(request("GET_SNAPSHOT", m)));
        CHECK(c.call(request("GET_MATCH_STATUS", m)) == left.handle(request("GET_MATCH_STATUS", m)));
    }
}
TEST_CASE("binary: expiry profile refusal and reserved capacity have no lobby effects") {
    server::ManualClock clock;
    server::GameServer s(config(), clock);
    Bridge bridge(s, clock);
    Client low(bridge, 1, 128, 1);
    auto create = request("CREATE_MATCH");
    auto p = low.raw(encode_request(create, low.next++));
    CHECK(p[0] == 255);
    CHECK(p[3] == 10);
    CHECK(s.find_match("m1") == nullptr);
    Client c(bridge, 2);
    auto m = c.call(create).get_string("match_id").value();
    CHECK(m == "m1");
    auto page = c.raw(encode_request(request("GET_MAP", m), c.next++));
    Fields f;
    f.i(u32(page, 3)).i(1);
    CHECK(c.raw(packet(3, c.next++, f.span()))[3] == 7);
    f.reset();
    f.i(u32(page, 3));
    CHECK(c.raw(packet(4, c.next++, f.span()))[0] == 0x83);
    f.reset();
    f.i(u32(page, 3)).i(0);
    CHECK(c.raw(packet(3, c.next++, f.span()))[3] == 8);
    bridge.disconnect(2);
    clock.advance_ms(1800000);
    Fields h;
    h.b(1).b(1).b(0).h(240).b(3).i(63).b(1);
    bw_write_bytes(&h.w, c.secret.data(), 16);
    CHECK(bridge.handle(3, packet(1, 0, h.span()))[3] == 4);
    Bridge limited(s, clock, Bridge::Limits{64, 2048, 2048});
    Client d(limited, 4);
    CHECK(d.raw(encode_request(create, d.next++))[3] == 9);
    CHECK(s.handle(request("GET_MATCH_STATUS", "m2")).get_string("code") == "UNKNOWN_MATCH");
}
TEST_CASE("binary: seeded malformed packets cannot expose partially validated records") {
    auto fs = fixtures();
    std::mt19937 rng(43);
    size_t visited = 0;
    auto callback = [](void *p, const uint8_t *, size_t) { ++*(size_t *)p; };
    for (const auto &e : fs.find("examples")->items())
        if (e.get_string("kind") == "payload") {
            auto b = hex(e.get_string("hex").value());
            for (int i = 0; i < 100; ++i) {
                auto mut = b;
                mut[rng() % mut.size()] ^= (uint8_t)(1 + rng() % 255);
                bool good = bw_validate_packet(mut.data(), mut.size(), 240);
                if (!good) {
                    visited = 0;
                    CHECK_FALSE(bw_visit_page(mut.data(), mut.size(), callback, &visited));
                    CHECK(visited == 0);
                }
            }
        }
    uint8_t bad[] = {0x82, 1, 0, 1, 0,  0, 0, 0, 0, 0, 0,   8,   0,   0, 0,
                     0,    0, 0, 0, 17, 1, 8, 1, 3, 2, 'm', '1', 127, 1, 0};
    visited = 0;
    CHECK_FALSE(bw_visit_page(bad, sizeof bad, callback, &visited));
    CHECK(visited == 0);
}
TEST_CASE("binary: event transfer watermark commits only complete windows and ignores old final pages") {
    auto fs = fixtures();
    std::vector<Bytes> pages;
    for (const auto &e : fs.find("examples")->items())
        if (e.get_string("name")->starts_with("PAGE-EVENTS-same-version-"))
            pages.push_back(hex(e.get_string("hex").value()));
    REQUIRE(pages.size() == 3);
    bw_stream s;
    bw_stream_init(&s, 20);
    int apply = 0;
    REQUIRE(bw_stream_accept(&s, pages[0].data(), pages[0].size(), &apply));
    CHECK(apply);
    CHECK(s.ack_version == 20);
    auto before = s;
    CHECK_FALSE(bw_stream_accept(&s, pages[2].data(), pages[2].size(), &apply));
    CHECK(std::memcmp(&s, &before, sizeof s) == 0);
    REQUIRE(bw_stream_accept(&s, pages[0].data(), pages[0].size(), &apply));
    CHECK(!apply);
    CHECK(s.ack_version == 20);
    auto changed = pages[1];
    changed[15] = 43;
    CHECK_FALSE(bw_stream_accept(&s, changed.data(), changed.size(), &apply));
    CHECK(s.ack_version == 20);
    REQUIRE(bw_stream_accept(&s, pages[1].data(), pages[1].size(), &apply));
    CHECK(s.ack_version == 20);
    REQUIRE(bw_stream_accept(&s, pages[2].data(), pages[2].size(), &apply));
    CHECK(s.ack_version == 42);
    REQUIRE(bw_stream_accept(&s, pages[2].data(), pages[2].size(), &apply));
    CHECK(!apply);
    CHECK(s.ack_version == 42);
    // A future request cursor gets H=current, which may be below the local acknowledged value.
    auto empty = hex("82 01 00 02 00 00 00 00 00 00 00 0f 00 00 00 2a 00 00 00 16 01 0f 01 03 02 6d 31 16 08 "
                     "64 00 00 00 00 00 00 00");
    bw_stream_init(&s, 100);
    REQUIRE(bw_stream_accept(&s, empty.data(), empty.size(), &apply));
    CHECK(!apply);
    CHECK(s.ack_version == 100);
    REQUIRE(bw_stream_accept(&s, pages[2].data(), pages[2].size(), &apply));
    CHECK(!apply);
    CHECK(s.ack_version == 100);
}
TEST_CASE("binary: retained event window matches JSON after eventless trimming and is immutable") {
    server::ManualClock clock;
    server::GameServer srv(config(), clock);
    Bridge bridge(srv, clock);
    Client c(bridge, 1, 128);
    auto create = request("CREATE_MATCH");
    create.set("player_count", 2);
    create.set("planning_seconds", 0);
    auto m = c.call(create).get_string("match_id").value();
    auto token = c.call(request("JOIN_MATCH", m)).get_string("session").value();
    srv.handle(request("JOIN_MATCH", m));
    auto q = program(m, 1, 1, 1, false);
    q.set("session", token);
    auto original = c.raw(encode_request(with(request("GET_EVENTS", m), "since_version", 0), c.next++));
    auto version = u32(original, 15);
    CHECK(version == 1);
    for (int revision = 1; revision <= 520; ++revision) {
        q.set("revision", revision);
        REQUIRE(srv.handle(q).get_bool("ok").value());
    }
    CHECK(srv.find_match(m)->all_events().empty()); // only eventless versions remain
    CHECK(c.gather(original).find("events")->items().size() > 0);
    auto req = with(request("GET_EVENTS", m), "since_version", 0);
    CHECK(c.call(req) == srv.handle(req));
    CHECK(c.call(req).get_string("code") == "SNAPSHOT_REQUIRED");
    req.set("since_version", 1); // trim floor is actual removed event version, not current-512
    auto json = srv.handle(req);
    CHECK(json.get_string("type") == "EVENTS");
    auto e = c.call(req);
    CHECK(e.find("events")->items().empty());
    CHECK(e.get_int("state_version") == 17);
    CHECK(c.tracker.ack_version == 17);
    req.set("since_version", 520);
    e = c.call(req);
    CHECK(e.get_int("state_version") == 521);
    CHECK(c.tracker.ack_version == 521);
    req.set("since_version", 1);
    c.call(req);
    CHECK(c.tracker.ack_version == 521);
}
TEST_CASE("binary: frozen transfer rejects replaced cursors and preserves last exchange over expiry") {
    server::ManualClock clock;
    server::GameServer srv(config(), clock);
    Bridge b(srv, clock);
    Client c(b, 1, 128);
    auto m = c.call(request("CREATE_MATCH")).get_string("match_id").value();
    auto old = c.raw(encode_request(request("GET_MAP", m), c.next++));
    auto tid = u32(old, 3);
    auto status = c.raw(encode_request(request("GET_MATCH_STATUS", m), c.next++));
    auto status_request = encode_request(request("GET_MATCH_STATUS", m), c.next - 1);
    CHECK(c.raw(status_request) == status);
    Fields f;
    f.i(tid).i(0);
    auto expired = packet(3, c.next++, f.span());
    CHECK(c.raw(expired)[3] == 8);
    CHECK(c.raw(expired)[3] == 8);
    // A failed BIND leaves the existing transfer and binding usable.
    Fields bind;
    bind.b(1).str(m).i(1).str("wrong");
    CHECK(c.raw(packet(2, c.next++, bind.span()))[3] == 3);
    Fields fetch;
    fetch.i(u32(status, 3)).i(0);
    auto again = c.raw(packet(3, c.next++, fetch.span()));
    CHECK(Bytes(again.begin() + 3, again.end()) == Bytes(status.begin() + 3, status.end()));
}
TEST_CASE("binary: H3 refusals budgets and retry after close remain JSON-equivalent") {
    server::ManualClock clock;
    auto cfg = config();
    cfg.request_budget_per_round = 8;
    server::GameServer srv(cfg, clock);
    Bridge bridge(srv, clock);
    Client c(bridge, 1);
    auto create = request("CREATE_MATCH");
    create.set("player_count", 2);
    create.set("planning_seconds", 0);
    create.set("rounds", 2);
    auto m = c.call(create).get_string("match_id").value();
    auto token = c.call(request("JOIN_MATCH", m)).get_string("session").value();
    auto other = srv.handle(request("JOIN_MATCH", m)).get_string("session").value();
    auto q = program(m, 1, 2, 1, false, "b:0");
    q.set("session", token);
    auto early = c.call(q);
    CHECK(early.get_string("code") == "WRONG_ROUND");
    CHECK(srv.handle(q) == early);
    for (int i = 1; i < 8; ++i) {
        q.set("request_id", "b:" + std::to_string(i));
        CHECK(c.call(q).get_string("code") == "WRONG_ROUND");
    }
    q.set("request_id", "b:8");
    CHECK(c.call(q).get_string("code") == "REQUEST_LIMIT");
    auto ready = program(m, 1, 1, 1, true);
    ready.set("session", token);
    auto accepted = c.call(ready);
    CHECK(accepted.get_bool("ok").value());
    auto second = program(m, 2, 1, 1, true);
    second.set("session", other);
    srv.handle(second);
    q.set("request_id", "b:0");
    CHECK(c.call(q) == early);
    q.set("request_id", "b:8");
    CHECK(c.call(q).get_bool("ok").value());
    // Same revision payload is unchanged (still no silent sequence-only deduplication).
    q.set("request_id", "other-id");
    CHECK(c.call(q).get_string("message") == "unchanged");
    q.set("ready", true);
    q.set("request_id", "new-id");
    CHECK(c.call(q).get_string("code") == "REVISION_CONFLICT");
}
TEST_CASE("binary: map metadata text can page without allocating an unbounded JSON list") {
    server::ManualClock clock;
    server::GameServer srv(config(), clock);
    auto map = std::make_shared<Map>(*srv.registered_map("training-01"));
    map->id = "long";
    server::MapInfo info;
    info.name = std::string(65535, 'n');
    info.description = std::string(65535, 'd');
    REQUIRE(srv.register_map(map, info));
    Bridge bridge(srv, clock, Bridge::Limits{64, 8192, 16384});
    Client c(bridge, 1);
    auto p = c.raw(encode_request(request("LIST_MAPS"), c.next++));
    CHECK(p[0] == 255);
    CHECK(p[3] == 9);
    CHECK(bridge.transfer_storage() == 0);
}
TEST_CASE("binary: exchange exhaustion never wraps and rotates with existing credential") {
    server::ManualClock clock;
    server::GameServer srv(config(), clock);
    Bridge b(srv, clock);
    Client c(b, 1);
    // Complete invalid bounded requests consume their expected ID but never call GameServer.
    for (unsigned ex = 1; ex <= 65535; ++ex) {
        Bytes req{0x7f, (uint8_t)ex, (uint8_t)(ex >> 8)};
        auto reply = c.raw(req);
        REQUIRE(reply[3] == 2);
    }
    CHECK(c.raw(Bytes{0x10, 0, 0})[3] == 6);
    CHECK(c.raw(Bytes{0x10, 1, 0})[3] == 6);
    CHECK(c.raw(Bytes{0x7f, 255, 255})[3] == 2);
    c.connection = 2;
    auto h = c.hello(true);
    CHECK(h[44] == 255);
    CHECK(h[45] == 255);
}

TEST_CASE("binary: measured packet budgets and session-local cursor reset") {
    for (unsigned capacity : {128, 240}) {
        server::ManualClock clock;
        server::GameServer srv(config(), clock);
        Bridge bridge(srv, clock);
        Client c(bridge, 1, capacity);
        c.call(request("LIST_MAPS"));
        std::cout << "P" << capacity << " LIST_MAPS pages=" << c.measured_pages
                  << " payload-bytes=" << c.measured_bytes << " largest=" << c.measured_max << '\n';
        auto create = with(with(request("CREATE_MATCH"), "player_count", 4), "bots", 3);
        auto m = c.call(create).get_string("match_id").value();
        c.call(request("JOIN_MATCH", m));
        for (const char *op : {"GET_MAP", "GET_SNAPSHOT", "GET_EVENTS", "GET_MATCH_STATUS"}) {
            auto q = request(op, m);
            if (std::string(op) == "GET_EVENTS")
                q.set("since_version", 0);
            c.call(q);
            CHECK(c.measured_max <= capacity);
            std::cout << "P" << capacity << ' ' << op << " pages=" << c.measured_pages
                      << " payload-bytes=" << c.measured_bytes << " largest=" << c.measured_max << '\n';
        }
        auto ack = c.tracker.ack_version;
        REQUIRE(ack > 0);
        c.hello(false);
        c.next = 1;
        bw_stream_init(&c.tracker, ack);
        c.call(with(request("GET_EVENTS", m), "since_version", 0));
        CHECK(c.tracker.ack_version >= ack);
        CHECK(c.tracker.transfer == 1);
    }
    auto bad = program("m1", 1, 1, 1, false);
    bad.set("revision", INT64_MAX);
    CHECK_THROWS_AS(encode_request(bad, 1), CodecError);
}

TEST_CASE("S1 named blend compatibility metadata survives frozen binary status") {
    server::ManualClock clock;
    server::GameServer s(config(), clock);
    auto pool = server::parse_pool_config(R"({"offerings":[{"id":"blend","name":"Blend","map_id":"foundry-01","player_count":4,"instances":1,"planning_seconds":0,"bots":[{"name":"Private A","personality":{"hauler":0.2,"opportunist":0.3,"saboteur":0.5}},{"name":"Private B","personality":{"hauler":0.4999999,"opportunist":0.5000001,"saboteur":0}},{"name":"Private C","personality":{"hauler":0.5,"opportunist":0,"saboteur":0.5}}]}]})");
    std::ostringstream report;
    std::string error;
    REQUIRE(s.provision_pool(pool.offerings, report, error));
    Bridge b(s, clock);
    Client c(b, 1);
    REQUIRE(c.call(request("JOIN_MATCH", "m1")).get_bool("ok").value());
    auto json = s.handle(request("GET_MATCH_STATUS", "m1"));
    auto wire = c.call(request("GET_MATCH_STATUS", "m1"));
    CHECK(wire == json);
    REQUIRE(wire.find("status"));
    auto kinds = wire.find("status")->find("bot_personalities");
    REQUIRE(kinds);
    CHECK(kinds->dump() == R"(["saboteur","opportunist","hauler"])");
    CHECK(wire.dump().find("Private") == std::string::npos);
    CHECK(wire.dump().find("weights") == std::string::npos);
}

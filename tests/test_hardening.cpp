// M0-H2: untrusted input. JSON parsing limits and GameServer request validation.
#include <cstdint>
#include <string>

#include "botched/json.hpp"
#include "botched/serialize.hpp"
#include "botched/match.hpp"
#include "doctest.h"
#include "map_fixture.hpp"
#include "line_reader.hpp"
#include "server.hpp"
#include "bot.hpp"
#include <sstream>

using namespace botched;
using namespace botched::server;

namespace {

Json req(const char* type, const std::string& match = "") {
    Json j = Json::object();
    j.set("type", type);
    if (!match.empty()) j.set("match_id", match);
    return j;
}

std::string code(const Json& j) { return j.get_string("code").value_or(""); }
bool ok(const Json& j) { return j.get_bool("ok").value_or(false); }

// A started two-player match plus enough handles to poke at it.
struct Fixture {
    ManualClock clock;
    GameServer srv;
    std::string match;
    std::vector<std::string> session;
    static ServerConfig reproducible(ServerConfig cfg) {  // fixed tokens: failures must be reproducible
        cfg.deterministic_token_seed = 1;
        return cfg;
    }
    explicit Fixture(ServerConfig cfg = {}) : srv(reproducible(cfg), clock) {
        Json c = req("CREATE_MATCH");
        c.set("player_count", 2);
        match = *srv.handle(c).get_string("match_id");
        for (int i = 0; i < 2; ++i) session.push_back(*srv.handle(req("JOIN_MATCH", match)).get_string("session"));
    }
    Json submit_req(int p = 1, int round = 1, int rev = 1) const {
        Json s = req("SUBMIT_PROGRAM", match);
        s.set("player_id", p);
        s.set("session", session[static_cast<std::size_t>(p - 1)]);
        s.set("round", round);
        s.set("revision", rev);
        Json a = Json::array();
        for (const char* o : {"FORWARD_1", "WAIT", "WAIT"}) a.push(o);
        s.set("orders", a);
        s.set("ready", false);
        s.set("request_id", "r-1");
        return s;
    }
    // Everything a client could observe or that the rules could depend on.
    std::string digest() const {
        return srv.find_match(match)->save().dump();
    }
};

}  // namespace

// ------------------------------------------------------------------ JSON parser limits

TEST_CASE("H2 json: nesting, size and shape limits reject instead of exhausting memory") {
    CHECK_FALSE(Json::parse(std::string(100000, '[')).has_value());
    CHECK_FALSE(Json::parse(std::string(100000, '{')).has_value());
    std::string nested;
    for (int i = 0; i < 200; ++i) nested += "{\"a\":";
    CHECK_FALSE(Json::parse(nested).has_value());
}

TEST_CASE("H2 json: duplicate object keys are rejected (no last-one-wins ambiguity)") {
    CHECK_FALSE(Json::parse("{\"type\":\"GET_MAP\",\"type\":\"CREATE_MATCH\"}").has_value());
    CHECK_FALSE(Json::parse("{\"a\":1,\"b\":2,\"a\":3}").has_value());
    CHECK(Json::parse("{\"a\":1,\"b\":2}").has_value());
}

TEST_CASE("H2 json: non-canonical numbers are rejected") {
    for (const char* bad : {"01", "-01", "00", "[007]", "-", "- 1", "+1", "1e3", "1.0", ".5", "0x10",
                            "9007199254740993", "-9007199254740993", "123456789012345678901234567890"})
        CHECK_FALSE(Json::parse(bad).has_value());
    for (const char* good : {"0", "-0", "7", "-7", "9007199254740992", "-9007199254740992"})
        CHECK(Json::parse(good).has_value());
}

TEST_CASE("H2 json: strings are printable ASCII only (protocol identifiers are ASCII)") {
    CHECK_FALSE(Json::parse("\"a\x01z\"").has_value());      // raw control byte
    CHECK_FALSE(Json::parse("\"a\nz\"").has_value());        // raw newline
    CHECK_FALSE(Json::parse("\"caf\xc3\xa9\"").has_value()); // UTF-8
    CHECK_FALSE(Json::parse("\"\\u00e9\"").has_value());     // escaped non-ASCII
    CHECK_FALSE(Json::parse("\"\\ud83d\\ude00\"").has_value());
    CHECK(Json::parse("\"a\\nb\"").has_value());             // escaped control is fine
}

// ------------------------------------------------------------------ server: field types and ranges

TEST_CASE("H2 server: optional fields of the wrong type are errors, not silent defaults") {
    ManualClock clock;
    GameServer srv(ServerConfig{}, clock);
    struct Bad { const char* key; Json value; };
    const std::vector<Bad> bads = {
        {"map_id", Json(5)}, {"map_id", Json(true)}, {"map_id", Json(nullptr)},
        {"player_count", Json("4")}, {"player_count", Json(true)}, {"player_count", Json(nullptr)},
        {"rounds", Json("12")}, {"rounds", Json(nullptr)},
        {"planning_seconds", Json("60")}, {"planning_seconds", Json(nullptr)},
    };
    for (const auto& b : bads) {
        Json c = req("CREATE_MATCH");
        c.set(b.key, b.value);
        Json r = srv.handle(c);
        INFO(b.key << " = " << b.value.dump());
        CHECK_FALSE(ok(r));
        CHECK(code(r) == "BAD_REQUEST");
    }
    // Nothing was created by any of them.
    CHECK(code(srv.handle(req("GET_MATCH_STATUS", "m1"))) == "UNKNOWN_MATCH");
}

TEST_CASE("H2 server: 64-bit values never wrap into valid 32-bit ones") {
    ManualClock clock;
    GameServer srv(ServerConfig{}, clock);
    const std::int64_t wrap = INT64_C(4294967296);  // 2^32
    struct Case { const char* key; std::int64_t v; };
    for (const Case& c : {Case{"player_count", wrap + 4}, Case{"player_count", -wrap + 2}, Case{"rounds", wrap + 12},
                          Case{"planning_seconds", wrap}, Case{"planning_seconds", wrap + 60},
                          Case{"planning_seconds", INT64_C(9007199254740991)}, Case{"planning_seconds", -1}}) {
        Json r = req("CREATE_MATCH");
        r.set(c.key, Json(c.v));
        Json resp = srv.handle(r);
        INFO(c.key << " = " << c.v);
        CHECK_FALSE(ok(resp));
        CHECK(code(resp) == "BAD_REQUEST");
    }
    CHECK(code(srv.handle(req("GET_MATCH_STATUS", "m1"))) == "UNKNOWN_MATCH");
}

TEST_CASE("H2 server: SUBMIT_PROGRAM round/revision/player cannot wrap, and a rejection mutates nothing") {
    Fixture f;
    const std::string before = f.digest();
    const std::int64_t wrap = INT64_C(4294967296);
    struct Case { const char* key; Json v; };
    const std::vector<Case> cases = {
        {"round", Json(wrap + 1)}, {"revision", Json(wrap + 1)}, {"player_id", Json(wrap + 1)},
        {"round", Json("1")}, {"revision", Json("1")}, {"player_id", Json("1")},
        {"round", Json(nullptr)}, {"revision", Json(true)},
        {"ready", Json("yes")}, {"ready", Json(1)}, {"ready", Json(nullptr)},
        {"request_id", Json(5)}, {"request_id", Json(true)}, {"request_id", Json(nullptr)},
        {"session", Json(5)}, {"orders", Json("FORWARD_1")}, {"orders", Json(nullptr)},
    };
    for (const auto& c : cases) {
        Json s = f.submit_req();
        s.set(c.key, c.v);
        Json r = f.srv.handle(s);
        INFO(c.key << " = " << c.v.dump());
        CHECK_FALSE(ok(r));
        CHECK(f.digest() == before);
    }
}

TEST_CASE("H2 server: GET_EVENTS since_version is range-checked") {
    Fixture f;
    for (std::int64_t v : {INT64_C(4294967296), INT64_C(-4294967296), INT64_C(9007199254740991), INT64_C(2147483648)}) {
        Json e = req("GET_EVENTS", f.match);
        e.set("since_version", Json(v));
        Json r = f.srv.handle(e);
        INFO("since_version = " << v);
        CHECK_FALSE(ok(r));
        CHECK(code(r) == "BAD_REQUEST");
    }
    Json e = req("GET_EVENTS", f.match);
    e.set("since_version", "0");
    CHECK(code(f.srv.handle(e)) == "BAD_REQUEST");
    // A negative in-range value keeps its pre-H2 meaning: no such history is retained.
    Json neg = req("GET_EVENTS", f.match);
    neg.set("since_version", -1);
    CHECK(code(f.srv.handle(neg)) == "SNAPSHOT_REQUIRED");
}

TEST_CASE("H2 server: orders must be exactly three strings") {
    Fixture f;
    const std::string before = f.digest();
    for (int n : {0, 1, 2, 4, 50}) {
        Json s = f.submit_req();
        Json a = Json::array();
        for (int i = 0; i < n; ++i) a.push("WAIT");
        s.set("orders", a);
        CHECK_FALSE(ok(f.srv.handle(s)));
    }
    Json s = f.submit_req();
    Json a = Json::array();
    a.push("WAIT"); a.push(5); a.push("WAIT");
    s.set("orders", a);
    CHECK_FALSE(ok(f.srv.handle(s)));
    CHECK(f.digest() == before);
}

TEST_CASE("H2 server: request_id and match_id must be short printable tokens") {
    Fixture f;
    const std::string before = f.digest();
    for (const std::string& id : {std::string(65, 'x'), std::string(10000, 'x'), std::string(""), std::string("a b"),
                                  std::string("a\x01" "b"), std::string("caf\xc3\xa9")}) {
        Json s = f.submit_req();
        s.set("request_id", id);
        INFO("request_id length " << id.size());
        if (id.empty()) continue;  // empty is allowed: it means "no idempotency key"
        CHECK_FALSE(ok(f.srv.handle(s)));
    }
    CHECK(f.digest() == before);
    Json g = req("GET_SNAPSHOT", std::string(5000, 'm'));
    CHECK_FALSE(ok(f.srv.handle(g)));
}

TEST_CASE("H2 server: the number of hosted matches is bounded") {
    ManualClock clock;
    GameServer srv(ServerConfig{}, clock);
    int created = 0;
    for (int i = 0; i < 5000; ++i) {
        Json r = srv.handle(req("CREATE_MATCH"));
        if (!ok(r)) {
            CHECK(code(r) == "TOO_MANY_MATCHES");
            break;
        }
        ++created;
    }
    CHECK(created < 5000);
    CHECK(created >= 1);
}

// ------------------------------------------------------------------ parser limits (explicit)

TEST_CASE("H2 json: every resource limit is enforced and reported as a parse failure") {
    ParseLimits l;
    l.max_bytes = 20;
    CHECK(Json::parse("[1,2,3]", l).has_value());
    CHECK_FALSE(Json::parse("[1,2,3,4,5,6,7,8,9,10,11]", l).has_value());

    ParseLimits d;
    d.max_depth = 3;
    CHECK(Json::parse("[[[1]]]", d).has_value());
    CHECK_FALSE(Json::parse("[[[[1]]]]", d).has_value());

    ParseLimits st;
    st.max_string = 4;
    CHECK(Json::parse("\"abcd\"", st).has_value());
    CHECK_FALSE(Json::parse("\"abcde\"", st).has_value());
    CHECK_FALSE(Json::parse("\"\\u0041\\u0041\\u0041\\u0041\\u0041\"", st).has_value());  // limit counts decoded bytes

    ParseLimits ar;
    ar.max_array = 3;
    CHECK(Json::parse("[1,2,3]", ar).has_value());
    CHECK_FALSE(Json::parse("[1,2,3,4]", ar).has_value());

    ParseLimits mb;
    mb.max_members = 2;
    CHECK(Json::parse("{\"a\":1,\"b\":2}", mb).has_value());
    CHECK_FALSE(Json::parse("{\"a\":1,\"b\":2,\"c\":3}", mb).has_value());

    ParseLimits nd;
    nd.max_nodes = 4;
    CHECK(Json::parse("[1,2,3]", nd).has_value());          // array + 3 numbers
    CHECK_FALSE(Json::parse("[1,2,3,4]", nd).has_value());

    // Big hostile documents fail fast under the default limits instead of allocating.
    std::string wide = "[";
    for (int i = 0; i < 100000; ++i) wide += "1,";
    wide += "1]";
    CHECK_FALSE(Json::parse(wide).has_value());                         // > max_array (65536)
    CHECK_FALSE(Json::parse(std::string(5u << 20, ' ') + "1").has_value());  // > max_bytes (4 MiB)
    CHECK_FALSE(Json::parse("\"" + std::string(5000, 'a') + "\"").has_value());  // > max_string (4096)
}

TEST_CASE("H2 json: documents the engine itself writes always parse under the default limits") {
    // Guard against the defaults being tightened below what checkpoints/snapshots need.
    MatchConfig cfg;
    cfg.match_id = "m";
    cfg.map = std::make_shared<const Map>(training01());
    cfg.player_count = 4;
    Match m(cfg);
    m.play_to_end();
    CHECK(Json::parse(m.save().dump()).has_value());
    CHECK(Json::parse(m.snapshot().dump()).has_value());
    Json all = Json::array();
    for (const auto& e : m.all_events()) all.push(event_to_json(e, "m"));
    CHECK(Json::parse(all.dump()).has_value());
    CHECK(Json::parse(map_to_json(training01()).dump()).has_value());
}

// ------------------------------------------------------------------ request size and shape

TEST_CASE("H2 server: oversized and malformed request text is refused before it is parsed") {
    Fixture f;
    const std::string before = f.digest();
    Json big = f.submit_req();
    big.set("request_id", std::string(kMaxIdLength + 1, 'x'));
    CHECK(code(f.srv.handle_text(big.dump())) == "BAD_REQUEST");

    std::string huge(kMaxRequestBytes + 1, 'a');
    CHECK(code(f.srv.handle_text(huge)) == "REQUEST_TOO_LARGE");
    CHECK(code(f.srv.handle_text("{\"type\":\"GET_MAP\",\"pad\":\"" + std::string(kMaxRequestBytes, 'a') + "\"}")) ==
          "REQUEST_TOO_LARGE");
    // Within the byte limit but over a shape limit: too many members / too long array / too deep.
    std::string many = "{";
    for (int i = 0; i < 40; ++i) many += "\"k" + std::to_string(i) + "\":1,";
    many += "\"type\":\"GET_MAP\"}";
    CHECK(code(f.srv.handle_text(many)) == "BAD_REQUEST");
    CHECK(code(f.srv.handle_text("{\"type\":\"GET_MAP\",\"a\":[[[[[1]]]]]}")) == "BAD_REQUEST");
    CHECK(code(f.srv.handle_text("{\"type\":\"GET_MAP\",\"a\":[1,2,3,4,5,6,7,8,9]}")) == "BAD_REQUEST");
    for (const char* junk : {"", "   ", "null", "[]", "5", "\"x\"", "{", "}", "{\"type\":", "{\"type\":5}", "{\"type\":null}",
                             "{\"type\":\"GET_MAP\"}x", "\xff\xfe", "{\"type\":\"A B\"}"})
        CHECK_FALSE(ok(f.srv.handle_text(junk)));
    CHECK(f.digest() == before);
}

TEST_CASE("H2 server: error responses never reflect arbitrary client text") {
    Fixture f;
    Json bad = f.submit_req();
    bad.set("request_id", "has a space");  // invalid id
    Json r = f.srv.handle(bad);
    CHECK_FALSE(ok(r));
    CHECK(r.find("request_id") == nullptr);
    Json unk = req("NOPE<script>");
    Json r2 = f.srv.handle(unk);
    CHECK(r2.find("request_type") == nullptr);
    CHECK(r2.dump().find("script") == std::string::npos);
    Json good = f.submit_req();
    good.set("revision", "x");
    CHECK(f.srv.handle(good).get_string("request_id") == "r-1");  // well-formed ids are echoed
}

// ------------------------------------------------------------------ bounded memory

// (The H2 LRU-cache test was superseded by tests/test_idempotency.cpp: the H3 contract keeps every
// record for the life of the match and bounds memory with a per-round budget instead.)

TEST_CASE("H2 host: bounded line reader caps memory and resynchronises after an oversize line") {
    using namespace botched::server;
    std::istringstream in("short\n" + std::string(10000, 'x') + "\nnext\r\n\nlast");
    std::string line;
    CHECK(read_line_bounded(in, line, 100) == LineStatus::Ok);
    CHECK(line == "short");
    CHECK(read_line_bounded(in, line, 100) == LineStatus::TooLong);
    CHECK(line.empty());
    CHECK(read_line_bounded(in, line, 100) == LineStatus::Ok);
    CHECK(line == "next");  // CR stripped, stream resynchronised
    CHECK(read_line_bounded(in, line, 100) == LineStatus::Ok);
    CHECK(line.empty());
    CHECK(read_line_bounded(in, line, 100) == LineStatus::Ok);
    CHECK(line == "last");  // unterminated final line
    CHECK(read_line_bounded(in, line, 100) == LineStatus::Eof);

    std::istringstream exact(std::string(100, 'y') + "\n" + std::string(101, 'z') + "\n");
    CHECK(read_line_bounded(exact, line, 100) == LineStatus::Ok);
    CHECK(line.size() == 100);
    CHECK(read_line_bounded(exact, line, 100) == LineStatus::TooLong);
    // A stream with no newline at all and no end in sight is still consumed in O(max) memory.
    std::istringstream endless(std::string(1 << 22, 'q'));
    CHECK(read_line_bounded(endless, line, 100) == LineStatus::TooLong);
    CHECK(line.capacity() < 4096);
}

// ------------------------------------------------------------------ mutation fuzz

TEST_CASE("H2 server: random request mutations never crash and a refusal never changes state") {
    Fixture f;
    sim::Rng rng(20261120);
    std::vector<std::string> seeds;
    seeds.push_back(f.submit_req().dump());
    seeds.push_back(f.submit_req(2, 1, 3).dump());
    {
        Json c = req("CREATE_MATCH");
        c.set("player_count", 3);
        seeds.push_back(c.dump());
    }
    seeds.push_back(req("JOIN_MATCH", f.match).dump());
    seeds.push_back(req("START_MATCH", f.match).dump());
    {
        Json e = req("GET_EVENTS", f.match);
        e.set("since_version", 0);
        seeds.push_back(e.dump());
    }
    for (const char* t : {"GET_SNAPSHOT", "GET_MAP", "GET_MATCH_STATUS"}) seeds.push_back(req(t, f.match).dump());

    const char* junk[] = {"null", "true", "-1", "99999999999", "4294967297", "\"\"", "\"x\"", "[]", "{}", "[1,2,3]",
                          "\"FORWARD_1\"", "1.5", "\"\\u00e9\"", "9007199254740993"};
    auto observable = [&] {
        std::string d = f.digest();
        for (int i = 1; i <= 40; ++i) {
            const Match* mm = f.srv.find_match("m" + std::to_string(i));
            if (mm) d += mm->save().dump();
        }
        return hex64(fnv1a64(d));
    };
    int refused = 0, accepted = 0;
    for (int iter = 0; iter < 6000; ++iter) {
        std::string text = seeds[static_cast<std::size_t>(rng.below(static_cast<int>(seeds.size())))];
        switch (rng.below(5)) {
            case 0:  // flip a byte
                if (!text.empty()) text[static_cast<std::size_t>(rng.below(static_cast<int>(text.size())))] = static_cast<char>(rng.below(256));
                break;
            case 1:  // truncate
                text.resize(static_cast<std::size_t>(rng.below(static_cast<int>(text.size()) + 1)));
                break;
            case 2: {  // replace a value with a hostile one
                auto colon = text.find(':', static_cast<std::size_t>(rng.below(static_cast<int>(text.size()))));
                if (colon != std::string::npos) {
                    auto end = text.find_first_of(",}", colon);
                    if (end != std::string::npos) text.replace(colon + 1, end - colon - 1, junk[rng.below(14)]);
                }
                break;
            }
            case 3: {  // duplicate or delete a chunk
                std::size_t a = static_cast<std::size_t>(rng.below(static_cast<int>(text.size()) + 1));
                std::size_t n = static_cast<std::size_t>(rng.below(20));
                if (rng.below(2)) text.insert(a, text.substr(a, n)); else text.erase(a, n);
                break;
            }
            default:  // glue two requests together / pad
                text += seeds[static_cast<std::size_t>(rng.below(static_cast<int>(seeds.size())))];
                break;
        }
        const std::string before = observable();
        Json resp = f.srv.handle_text(text);
        if (ok(resp)) { ++accepted; continue; }
        ++refused;
        CHECK_MESSAGE(observable() == before, "state changed by a refused request: " << text);
        CHECK(resp.find("code") != nullptr);
    }
    CHECK(refused > 3000);
    CHECK(accepted > 0);
}

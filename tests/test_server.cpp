// Server orchestration: lobby, sessions, deadlines, auto-advance, API errors.
#include "server.hpp"
#include "doctest.h"

using namespace botched;
using namespace botched::server;

namespace {

Json req(const char* type, const std::string& match = "") {
    Json j = Json::object();
    j.set("type", type);
    if (!match.empty()) j.set("match_id", match);
    return j;
}

struct Lobby {
    ManualClock clock;
    GameServer srv;
    std::string match;
    std::vector<std::string> session;
    explicit Lobby(int players = 2, int planning_seconds = 60, int rounds = 12)
        : srv(ServerConfig{}, clock) {
        Json c = req("CREATE_MATCH");
        c.set("player_count", players);
        c.set("planning_seconds", planning_seconds);
        c.set("rounds", rounds);
        match = *srv.handle(c).get_string("match_id");
        for (int i = 0; i < players; ++i) session.push_back(*srv.handle(req("JOIN_MATCH", match)).get_string("session"));
    }
    Json submit(int p, int round, int rev, std::vector<std::string> orders, bool ready, std::string id = "") {
        Json s = req("SUBMIT_PROGRAM", match);
        s.set("player_id", p);
        s.set("session", session[static_cast<std::size_t>(p - 1)]);
        s.set("round", round);
        s.set("revision", rev);
        Json a = Json::array();
        for (auto& o : orders) a.push(o);
        s.set("orders", a);
        s.set("ready", ready);
        if (!id.empty()) s.set("request_id", id);
        return srv.handle(s);
    }
    Json snapshot() { return *srv.handle(req("GET_SNAPSHOT", match)).find("snapshot"); }
};

std::string code(const Json& j) { return j.get_string("code").value_or(""); }

}  // namespace

TEST_CASE("server: utc formatting") {
    CHECK(format_utc(0) == "1970-01-01T00:00:00Z");
    CHECK(format_utc(1'795'000'000'000) == "2026-11-18T11:06:40Z");
    CHECK(format_utc(951'782'400'000) == "2000-02-29T00:00:00Z");
}

TEST_CASE("server: lobby fills, assigns slots 1..N and starts when full") {
    ManualClock clock;
    GameServer srv(ServerConfig{}, clock);
    Json c = req("CREATE_MATCH");
    c.set("player_count", 3);
    Json created = srv.handle(c);
    REQUIRE(created.get_bool("ok").value_or(false));
    std::string id = *created.get_string("match_id");
    CHECK(srv.handle(req("GET_MATCH_STATUS", id)).find("status")->get_string("phase") == "LOBBY");
    CHECK(code(srv.handle(req("GET_SNAPSHOT", id))) == "NOT_STARTED");
    for (int i = 1; i <= 3; ++i) {
        Json j = srv.handle(req("JOIN_MATCH", id));
        CHECK(j.get_int("player_id") == i);
        CHECK(j.get_bool("started").value() == (i == 3));
    }
    CHECK(code(srv.handle(req("JOIN_MATCH", id))) == "ALREADY_STARTED");
    CHECK(srv.handle(req("GET_MATCH_STATUS", id)).find("status")->get_string("phase") == "PLANNING");
}

TEST_CASE("server: START_MATCH with fewer joined players disables the empty slots") {
    ManualClock clock;
    GameServer srv(ServerConfig{}, clock);
    std::string id = *srv.handle(req("CREATE_MATCH")).get_string("match_id");  // capacity 4
    srv.handle(req("JOIN_MATCH", id));
    CHECK(code(srv.handle(req("START_MATCH", id))) == "START_FAILED");  // needs two
    srv.handle(req("JOIN_MATCH", id));
    srv.handle(req("JOIN_MATCH", id));
    Json st = srv.handle(req("START_MATCH", id));
    CHECK(st.get_bool("ok").value_or(false));
    CHECK(st.get_int("player_count") == 3);
    CHECK(srv.find_match(id)->snapshot().find("players")->items().size() == 3);
}

TEST_CASE("server: create validates its arguments") {
    ManualClock clock;
    GameServer srv(ServerConfig{}, clock);
    Json c = req("CREATE_MATCH");
    c.set("player_count", 5);
    CHECK(code(srv.handle(c)) == "BAD_REQUEST");
    Json d = req("CREATE_MATCH");
    d.set("map_id", "nope");
    CHECK(code(srv.handle(d)) == "UNKNOWN_MAP");
    CHECK(code(srv.handle(req("FROBNICATE"))) == "UNKNOWN_REQUEST");
    CHECK(code(srv.handle(Json::object())) == "BAD_REQUEST");
    CHECK(code(srv.handle_text("not json")) == "BAD_REQUEST");
    CHECK(code(srv.handle(req("GET_SNAPSHOT", "m999"))) == "UNKNOWN_MATCH");
}

TEST_CASE("server: sessions authorise a player slot") {
    Lobby l;
    Json bad = req("SUBMIT_PROGRAM", l.match);
    bad.set("player_id", 1);
    bad.set("session", l.session[1]);  // player 2's token
    bad.set("round", 1);
    bad.set("revision", 1);
    Json orders = Json::array();
    for (int i = 0; i < 3; ++i) orders.push("WAIT");
    bad.set("orders", orders);
    CHECK(code(l.srv.handle(bad)) == "UNAUTHORIZED");
    bad.set("session", "garbage");
    CHECK(code(l.srv.handle(bad)) == "UNAUTHORIZED");
    Json missing = bad;
    missing.set("player_id", 7);
    CHECK(code(l.srv.handle(missing)) == "UNKNOWN_PLAYER");
    CHECK(l.submit(1, 1, 1, {"WAIT", "WAIT", "WAIT"}, false).get_bool("ok").value());
}

TEST_CASE("server: planning closes and the round runs once every player is ready") {
    Lobby l;
    CHECK(l.snapshot().get_int("round") == 1);
    l.submit(1, 1, 1, {"FORWARD_2", "FORWARD_2", "WAIT"}, true);
    CHECK(l.snapshot().get_int("round") == 1);  // still waiting for player 2
    l.submit(2, 1, 1, {"WAIT", "WAIT", "WAIT"}, true);
    Json s = l.snapshot();
    CHECK(s.get_int("round") == 2);
    CHECK(s.get_string("phase") == "PLANNING");
    CHECK(s.find("ready_players")->dump() == "[]");
}

TEST_CASE("server: the planning deadline applies WAIT to missing programmes") {
    Lobby l;
    Json s0 = l.snapshot();
    REQUIRE(s0.get_string("planning_deadline_utc").has_value());
    l.submit(1, 1, 1, {"FORWARD_2", "FORWARD_2", "WAIT"}, false);  // not ready, kept at close
    l.clock.advance_ms(59'000);
    CHECK(l.snapshot().get_int("round") == 1);
    l.clock.advance_ms(2'000);
    Json s = l.srv.handle(req("GET_MATCH_STATUS", l.match));  // any request triggers the deadline check
    Json s1 = l.snapshot();
    CHECK(s1.get_int("round") == 2);
    CHECK(s1.find("players")->items()[0].get_int("x") == 5);   // P1's unready programme ran
    CHECK(s1.find("players")->items()[1].get_int("x") == 10);  // P2's missing programme became WAITs
    CHECK(s1.get_string("planning_deadline_utc") != s0.get_string("planning_deadline_utc"));
    (void)s;
}

TEST_CASE("server: timer disabled means planning waits for ready players only") {
    Lobby l(2, 0);
    CHECK(l.snapshot().find("planning_deadline_utc")->is_null());
    l.clock.advance_ms(10'000'000);
    l.srv.poll();
    CHECK(l.snapshot().get_int("round") == 1);
}

TEST_CASE("server: retried submissions are idempotent end to end") {
    Lobby l;
    Json a = l.submit(1, 1, 1, {"WAIT", "WAIT", "WAIT"}, true, "c-1");
    Json b = l.submit(1, 1, 1, {"WAIT", "WAIT", "WAIT"}, true, "c-1");
    CHECK(a.dump() == b.dump());
    l.submit(2, 1, 1, {"WAIT", "WAIT", "WAIT"}, true, "c-2");  // round advances here
    Json late = l.submit(1, 1, 1, {"WAIT", "WAIT", "WAIT"}, true, "c-1");  // retry after the round ran
    CHECK(late.dump() == a.dump());
    Json fresh = l.submit(1, 1, 1, {"WAIT", "WAIT", "WAIT"}, true, "c-new");
    CHECK(code(fresh) == "WRONG_ROUND");
}

TEST_CASE("server: GET_EVENTS, GET_MAP and SNAPSHOT_REQUIRED") {
    ManualClock clock;
    ServerConfig cfg;
    GameServer srv(cfg, clock);
    Json c = req("CREATE_MATCH");
    c.set("player_count", 2);
    c.set("rounds", 2);
    c.set("planning_seconds", 1);
    std::string id = *srv.handle(c).get_string("match_id");
    srv.handle(req("JOIN_MATCH", id));
    srv.handle(req("JOIN_MATCH", id));

    Json ev = req("GET_EVENTS", id);
    CHECK(code(srv.handle(ev)) == "BAD_REQUEST");
    ev.set("since_version", 0);
    Json all = srv.handle(ev);
    REQUIRE(all.get_bool("ok").value_or(false));
    CHECK(all.find("events")->items().size() >= 5);  // spawned crates + ROUND_STARTED
    CHECK(all.find("events")->items()[0].get_string("match_id") == id);
    CHECK(all.find("events")->items().back().get_string("type") == "ROUND_STARTED");

    Json map = srv.handle(req("GET_MAP", id));
    CHECK(map.find("map")->get_string("map_id") == "training-01");
    CHECK(map.find("map")->find("rows")->items().size() == 10);

    clock.advance_ms(1'500);
    srv.poll();
    clock.advance_ms(1'500);
    srv.poll();
    CHECK(srv.find_match(id)->finished());
    Json tail = req("GET_EVENTS", id);
    tail.set("since_version", srv.find_match(id)->state_version() - 1);
    Json t = srv.handle(tail);
    CHECK(t.find("events")->items().back().get_string("type") == "MATCH_ENDED");
    // Planning after the end is closed, and the server never advances a finished match.
    clock.advance_ms(100'000);
    srv.poll();
    CHECK(srv.find_match(id)->state().round == 2);
}

TEST_CASE("server: session tokens are 256-bit, unique and not derived from a known seed") {
    ManualClock clock;
    GameServer a(ServerConfig{}, clock), b(ServerConfig{}, clock);
    auto join_one = [](GameServer& s) {
        Json c = req("CREATE_MATCH");
        std::string id = *s.handle(c).get_string("match_id");
        return *s.handle(req("JOIN_MATCH", id)).get_string("session");
    };
    std::string ta = join_one(a), tb = join_one(b);
    CHECK(ta.size() == 64);
    CHECK(ta != tb);  // two identical servers must not mint the same token
    ServerConfig det;
    det.deterministic_token_seed = 1;
    GameServer d1(det, clock), d2(det, clock);
    CHECK(join_one(d1) == join_one(d2));  // opt-in reproducible mode still available
}

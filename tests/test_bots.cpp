// B-07: server-managed bot seats (CREATE_MATCH "bots").
#include <string>
#include <vector>

#include "botched/serialize.hpp"
#include "doctest.h"
#include "server.hpp"

using namespace botched;
using namespace botched::server;

namespace {

Json req(const char* type, const std::string& match = "") {
    Json j = Json::object();
    j.set("type", type);
    if (!match.empty()) j.set("match_id", match);
    return j;
}

struct Table {
    ManualClock clock;
    GameServer srv;
    std::string match;
    std::vector<std::string> session;  // humans only
    Table(int players, int bots, int humans_to_join, int rounds = 12, int planning_seconds = 30,
          std::uint64_t bot_seed = 1)
        : srv(make_cfg(bot_seed), clock) {
        Json c = req("CREATE_MATCH");
        c.set("player_count", players);
        c.set("bots", bots);
        c.set("rounds", rounds);
        c.set("planning_seconds", planning_seconds);
        Json r = srv.handle(c);
        REQUIRE(r.get_bool("ok").value_or(false));
        match = *r.get_string("match_id");
        for (int i = 0; i < humans_to_join; ++i) session.push_back(*srv.handle(req("JOIN_MATCH", match)).get_string("session"));
    }
    static ServerConfig make_cfg(std::uint64_t seed) {
        ServerConfig c;
        c.deterministic_token_seed = 7;
        c.bot_seed = seed;
        return c;
    }
    Json status() { return *srv.handle(req("GET_MATCH_STATUS", match)).find("status"); }
    Json snapshot() { return *srv.handle(req("GET_SNAPSHOT", match)).find("snapshot"); }
    Json submit(int p, int round, int rev, std::vector<std::string> orders, bool ready, const std::string& id) {
        Json s = req("SUBMIT_PROGRAM", match);
        s.set("player_id", p);
        s.set("session", session.at(static_cast<std::size_t>(p - 1)));
        s.set("round", round);
        s.set("revision", rev);
        Json a = Json::array();
        for (auto& o : orders) a.push(o);
        s.set("orders", a);
        s.set("ready", ready);
        s.set("request_id", id);
        return srv.handle(s);
    }
    // Humans always WAIT and lock in; returns the final state hash.
    std::string play_to_end() {
        for (int guard = 0; guard < 200; ++guard) {
            Json snap = snapshot();
            if (snap.get_string("phase").value_or("") == "FINISHED") break;
            const int round = static_cast<int>(snap.get_int("round").value_or(0));
            for (std::size_t h = 0; h < session.size(); ++h)
                submit(static_cast<int>(h) + 1, round, 1, {"WAIT", "WAIT", "WAIT"}, true, "h" + std::to_string(h) + "r" + std::to_string(round));
        }
        return hex64(fnv1a64(snapshot().dump()));
    }
};

}  // namespace

TEST_CASE("bots: seat rules - at least one human, bots leave room for humans, lobby counts seats") {
    ManualClock clock;
    GameServer srv(ServerConfig{}, clock);
    Json c = req("CREATE_MATCH");
    c.set("player_count", 3);
    c.set("bots", 3);  // no seat left for a human
    CHECK_FALSE(srv.handle(c).get_bool("ok").value_or(true));
    c.set("bots", 4);
    CHECK_FALSE(srv.handle(c).get_bool("ok").value_or(true));
    c.set("bots", -1);
    CHECK_FALSE(srv.handle(c).get_bool("ok").value_or(true));

    // two humans + two bots: the lobby shows 2 bots among 4 seats and starts when both humans are in
    Table t(4, 2, 0);
    Json st = t.status();
    CHECK(st.get_string("phase").value_or("") == "LOBBY");
    CHECK(st.get_int("capacity").value_or(0) == 4);
    CHECK(st.get_int("bots").value_or(0) == 2);
    CHECK(st.get_int("joined").value_or(0) == 2);  // the bots already occupy their seats
    Json j1 = t.srv.handle(req("JOIN_MATCH", t.match));
    CHECK(j1.get_int("player_id").value_or(0) == 1);
    CHECK_FALSE(j1.get_bool("started").value_or(true));
    Json j2 = t.srv.handle(req("JOIN_MATCH", t.match));
    CHECK(j2.get_int("player_id").value_or(0) == 2);
    CHECK(j2.get_bool("started").value_or(false));
    Json j3 = t.srv.handle(req("JOIN_MATCH", t.match));  // no third human seat
    CHECK_FALSE(j3.get_bool("ok").value_or(true));

    st = t.status();
    CHECK(st.get_string("phase").value_or("") == "PLANNING");
    REQUIRE(st.find("bot_players") != nullptr);
    REQUIRE(st.find("bot_players")->items().size() == 2);
    CHECK(st.find("bot_players")->items()[0].as_int() == 3);
    CHECK(st.find("bot_players")->items()[1].as_int() == 4);
    CHECK(t.snapshot().find("players")->items().size() == 4);
}

TEST_CASE("bots: they lock in normal programmes at the start of every round, and cannot be driven by clients") {
    Table t(4, 3, 1, 4);
    for (int round = 1; round <= 3; ++round) {
        Json snap = t.snapshot();
        CHECK(snap.get_int("round").value_or(0) == round);
        const Json* ready = snap.find("ready_players");
        REQUIRE(ready != nullptr);
        std::vector<int> r;
        for (const auto& v : ready->items()) r.push_back(static_cast<int>(v.as_int()));
        CHECK(r == std::vector<int>{2, 3, 4});  // bots are ready, the human is not
        // The human is the only one the round waits for.
        CHECK(t.snapshot().get_string("phase").value_or("") == "PLANNING");
        Json ok = t.submit(1, round, 1, {"WAIT", "WAIT", "WAIT"}, true, "h" + std::to_string(round));
        CHECK(ok.get_bool("ok").value_or(false));
    }
    // A client claiming a bot seat is refused, even with a guessable session string.
    Json s = req("SUBMIT_PROGRAM", t.match);
    s.set("player_id", 3);
    s.set("session", t.session[0]);
    s.set("round", 4);
    s.set("revision", 2);
    Json a = Json::array();
    for (int i = 0; i < 3; ++i) a.push("WAIT");
    s.set("orders", a);
    s.set("ready", true);
    s.set("request_id", "evil");
    Json r = t.srv.handle(s);
    CHECK_FALSE(r.get_bool("ok").value_or(true));
    CHECK(r.get_string("code").value_or("") == "UNAUTHORIZED");
}

TEST_CASE("bots: one human against three bots, and two against two, complete full matches deterministically") {
    for (auto cfg : {std::pair{4, 3}, std::pair{4, 2}, std::pair{2, 1}}) {
        const int humans = cfg.first - cfg.second;
        Table a(cfg.first, cfg.second, humans), b(cfg.first, cfg.second, humans);
        const std::string ha = a.play_to_end(), hb = b.play_to_end();
        CHECK(a.snapshot().get_string("phase").value_or("") == "FINISHED");
        CHECK(ha == hb);  // same seed, same inputs: identical match
        Json st = a.status();
        CHECK(st.get_int("round").value_or(0) == 12);
        // Bots actually play: with the humans idle the bots score.
        int bot_score = 0;
        const Json final_snap = a.snapshot();
        const auto& players = final_snap.find("players")->items();
        for (std::size_t i = static_cast<std::size_t>(humans); i < players.size(); ++i) bot_score += static_cast<int>(players[i].get_int("score").value_or(0));
        CHECK(bot_score > 0);
    }
    // a different bot seed gives a different match
    Table c(4, 3, 1, 12, 30, 1), d(4, 3, 1, 12, 30, 2);
    CHECK(c.play_to_end() != d.play_to_end());
}

TEST_CASE("bots: purposeful but imperfect - they collect and deliver crates and rarely fall into pits") {
    int deliveries = 0, pit_falls = 0, matches = 0;
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        Table t(4, 3, 1, 12, 30, seed);
        t.play_to_end();
        const Json final_snap = t.snapshot();
        for (const auto& p : final_snap.find("players")->items()) deliveries += static_cast<int>(p.get_int("deliveries").value_or(0));
        // events are retained for the whole short match: count pit disables
        Json q = req("GET_EVENTS", t.match);
        q.set("since_version", 0);
        Json r = t.srv.handle(q);
        const Json* ev = r.find("events");
        REQUIRE(ev != nullptr);  // retained for the whole match (otherwise the pit count would be vacuous)
        {
            for (const auto& e : ev->items())
                if (e.get_string("type").value_or("") == "ROBOT_DISABLED" && e.find("params")->get_string("cause").value_or("") == "PIT") ++pit_falls;
        }
        ++matches;
    }
    CHECK(deliveries >= matches * 6);  // three bots deliver at least 2 crates each per 12-round match on average
    CHECK(pit_falls <= matches);       // imperfect, but not suicidal
}

// G1: score-target victory. Fixed-round behaviour is unchanged; a target is judged only after a
// complete round; the round cap is a safety net; everything is deterministic.
#include <memory>
#include <string>

#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include <algorithm>
#include "doctest.h"
#include "map_fixture.hpp"
#include "server.hpp"
#include "sim_runner.hpp"

using namespace botched;
using namespace botched::server;

namespace {

MatchConfig cfg(int players, int rounds, int target) {
    MatchConfig c;
    c.match_id = "g1";
    c.map = std::make_shared<const Map>(training01());
    c.player_count = players;
    c.rounds = rounds;
    c.target_score = target;
    return c;
}

const Event* last_event(const Match& m, const std::string& type) {
    const Event* found = nullptr;
    for (const auto& e : m.all_events()) if (e.type == type) found = &e;
    return found;
}

struct Seat { int deliveries; int damage_taken = 0; };

// A checkpoint of a fresh match with chosen delivery counts (score = 5 x deliveries, as botched/1
// requires; crate ids are advanced to match), restored under `c`.
Match with_seats(const MatchConfig& c, std::initializer_list<Seat> seats) {
    Match fresh(c);
    Json saved = fresh.save();
    Json& players = const_cast<Json&>(*saved.find("players"));
    int total = 0, i = 0;
    for (const Seat& st : seats) {
        Json& p = const_cast<Json&>(players.items()[static_cast<std::size_t>(i++)]);
        p.set("score", kDeliveryScore * st.deliveries);
        p.set("deliveries", st.deliveries);
        p.set("damage_taken", st.damage_taken);
        total += st.deliveries;
    }
    Json& world = saved;
    world.set("next_crate_id", static_cast<int>(world.get_int("next_crate_id").value_or(1)) + total);
    std::string err;
    auto m = Match::restore(c, saved, err);
    REQUIRE_MESSAGE(m.has_value(), err);
    return std::move(*m);
}

}  // namespace

TEST_CASE("G1: fixed-round mode is unchanged (no target in snapshot or end event)") {
    Match m(cfg(2, 3, 0));
    CHECK(m.snapshot().find("target_score") == nullptr);  // keeps every fixed-round hash identical
    m.play_to_end();
    CHECK(m.state().round == 3);
    const Event* end = last_event(m, "MATCH_ENDED");
    REQUIRE(end);
    CHECK(end->params.find("reason") == nullptr);
}

TEST_CASE("G1: the target is judged only after the whole round, never mid-round") {
    // Both leaders start the round already at the target. Nobody has won while ticks run.
    Match m = with_seats(cfg(3, 50, 30), {{6}, {6}, {2}});
    REQUIRE(m.close_planning());
    CHECK(m.run_next_tick());
    CHECK_FALSE(m.finished());
    CHECK(m.run_next_tick());
    CHECK_FALSE(m.finished());
    CHECK(m.run_next_tick());   // third tick + round end
    CHECK(m.finished());
    CHECK(m.state().round == 1);
    CHECK(m.state().tick == kRegisters);
    // The events are ordered: all ticks, then ROUND_ENDED, then MATCH_ENDED.
    bool tick_seen[4] = {false, false, false, false};
    std::string prev;
    for (const auto& e : m.all_events()) {
        if (e.round == 1 && e.tick >= 1 && e.tick <= 3) tick_seen[e.tick] = true;
        prev = e.type;
    }
    CHECK(prev == "MATCH_ENDED");
    CHECK(tick_seen[1]);
    CHECK(tick_seen[2]);
    CHECK(tick_seen[3]);
    const Event* end = last_event(m, "MATCH_ENDED");
    REQUIRE(end);
    CHECK(end->params.get_string("reason").value_or("") == "target");
}

TEST_CASE("G1: two players over the target in the same round resolve by score, then damage taken") {
    {   // higher score wins even though the other also reached the target
        Match m = with_seats(cfg(2, 50, 30), {{7}, {6}});
        m.run_round();
        REQUIRE(m.finished());
        REQUIRE(m.status().find("winners")->items().size() == 1);
        CHECK(m.status().find("winners")->items()[0].as_int() == 1);
    }
    {   // equal score: the player who took less damage wins
        Match m = with_seats(cfg(2, 50, 30), {{6, 2}, {6, 0}});
        m.run_round();
        REQUIRE(m.finished());
        REQUIRE(m.status().find("winners")->items().size() == 1);
        CHECK(m.status().find("winners")->items()[0].as_int() == 2);
    }
    {   // fully tied: shared victory
        Match m = with_seats(cfg(2, 50, 30), {{6}, {6}});
        m.run_round();
        REQUIRE(m.finished());
        CHECK(m.status().find("winners")->items().size() == 2);
    }
}

TEST_CASE("G1: below the target the match keeps going; the cap ends it with the highest score") {
    Match m = with_seats(cfg(2, 4, 1000), {{4}, {3}});
    m.play_to_end();
    CHECK(m.state().round == 4);                       // the cap, not the target
    const Event* end = last_event(m, "MATCH_ENDED");
    REQUIRE(end);
    CHECK(end->params.get_string("reason").value_or("") == "cap");
    CHECK(m.status().find("winners")->items()[0].as_int() == 1);
}

TEST_CASE("G1: a target of exactly the current score ends after round 1; one more point does not") {
    Match a = with_seats(cfg(2, 50, 20), {{4}, {0}});
    a.run_round();
    CHECK(a.finished());
    Match b = with_seats(cfg(2, 50, 21), {{4}, {0}});
    b.run_round();
    CHECK_FALSE(b.finished());
    CHECK(b.state().round == 2);
}

TEST_CASE("G1: a score-target match checkpoint restores; a different target or an impossible finish is refused") {
    MatchConfig c = cfg(2, 50, 30);
    Match m(c);
    m.run_round();
    Json saved = m.save();
    std::string err;
    CHECK(Match::restore(c, saved, err).has_value());
    MatchConfig other = c;
    other.target_score = 25;
    CHECK_FALSE(Match::restore(other, saved, err).has_value());
    CHECK(err.find("target_score") != std::string::npos);
    MatchConfig fixed = c;
    fixed.target_score = 0;
    CHECK_FALSE(Match::restore(fixed, saved, err).has_value());

    // FINISHED while neither at the cap nor at the target is not a state the engine can produce.
    Match done = with_seats(c, {{6}, {0}});
    done.run_round();
    REQUIRE(done.finished());
    CHECK(Match::restore(c, done.save(), err).has_value());
    Json bad = done.save();
    Json& players = const_cast<Json&>(*bad.find("players"));
    const_cast<Json&>(players.items()[0]).set("score", 5);      // 5 x 1 delivery: consistent,
    const_cast<Json&>(players.items()[0]).set("deliveries", 1);  // but below the target and not the last round
    CHECK_FALSE(Match::restore(c, bad, err).has_value());
}

TEST_CASE("G1: CREATE_MATCH validates and echoes the target and the cap") {
    ManualClock clock;
    GameServer srv(ServerConfig{}, clock);
    auto create = [&](std::int64_t players, std::int64_t target, std::int64_t rounds, bool give_rounds) {
        Json j = Json::object();
        j.set("type", "CREATE_MATCH");
        j.set("player_count", players);
        if (target >= 0) j.set("target_score", target);
        if (give_rounds) j.set("rounds", rounds);
        return srv.handle(j);
    };
    Json def = create(2, -1, 0, false);
    CHECK(def.get_int("rounds").value_or(0) == kDefaultRounds);   // no target: the classic 12 rounds
    CHECK(def.get_int("target_score").value_or(-1) == 0);
    Json t = create(2, 30, 0, false);
    REQUIRE(t.get_bool("ok").value_or(false));
    CHECK(t.get_int("rounds").value_or(0) == kDefaultRoundCap);   // target without a cap: 100
    CHECK(t.get_int("target_score").value_or(0) == 30);
    Json cap = create(2, 30, 200, true);
    CHECK(cap.get_int("rounds").value_or(0) == 200);
    CHECK_FALSE(create(2, 30, 201, true).get_bool("ok").value_or(true));
    CHECK_FALSE(create(2, 1001, 0, false).get_bool("ok").value_or(true));
    CHECK_FALSE(create(2, -1, 0, true).get_bool("ok").value_or(true));  // rounds 0 invalid

    // Lobby status shows the rules before the match starts; the snapshot carries them afterwards.
    Json q = Json::object();
    q.set("type", "GET_MATCH_STATUS");
    q.set("match_id", *t.get_string("match_id"));
    const Json resp = srv.handle(q);
    const Json* st = resp.find("status");
    REQUIRE(st);
    CHECK(st->get_int("target_score").value_or(0) == 30);
    CHECK(st->get_int("rounds_total").value_or(0) == kDefaultRoundCap);
}

TEST_CASE("G1: a simulated score-target match ends on the target, deterministically, on every map") {
    for (const char* map : {"training-01", "factory-01"}) {
        sim::SimOptions o;
        o.players = 3;
        o.seed = 7;
        o.rounds = 100;
        o.target_score = 15;
        o.map_id = map;
        sim::SimResult a = sim::run_simulation(o), b = sim::run_simulation(o);
        INFO(map);
        REQUIRE_MESSAGE(a.ok, a.error);
        CHECK(a.state_hash == b.state_hash);
        CHECK(a.event_hash == b.event_hash);
        CHECK(a.rounds_played < 100);
        int best = 0;
        for (const auto& p : a.final_snapshot.find("players")->items()) best = std::max<int>(best, p.get_int("score").value_or(0));
        CHECK(best >= 15);
        CHECK(a.final_snapshot.get_int("target_score").value_or(0) == 15);
    }
    sim::SimOptions o;   // unreachable target: ends at the cap
    o.players = 2;
    o.rounds = 3;
    o.target_score = 1000;
    sim::SimResult r = sim::run_simulation(o);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.rounds_played == 3);
}

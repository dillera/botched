// Acceptance tests 11-14: programme submission, determinism, end of match, snapshot restore.
#include <memory>

#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"

using namespace botched;

namespace {

std::unique_ptr<Match> make_match(int players = 4, int rounds = kDefaultRounds) {
    MatchConfig c;
    c.match_id = "m-test";
    c.map = std::make_shared<const Map>(training01());
    c.player_count = players;
    c.rounds = rounds;
    return std::make_unique<Match>(c);
}

SubmitResult submit(Match& m, int p, int rev, std::vector<std::string> orders, bool ready,
                    const std::string& id = "", int round = -1) {
    return m.submit_program(p, round < 0 ? m.state().round : round, rev, orders, ready, id);
}

const std::vector<std::string> kWait = {"WAIT", "WAIT", "WAIT"};

int count_events(const Match& m, const std::string& type) {
    int n = 0;
    for (const auto& e : m.all_events()) if (e.type == type) ++n;
    return n;
}

}  // namespace

TEST_CASE("11. a missing programme becomes WAIT, WAIT, WAIT") {
    auto m = make_match(4);
    submit(*m, 1, 1, {"FORWARD_1", "FORWARD_1", "FORWARD_1"}, true);
    m->run_round();  // players 2-4 never submitted
    const Robot& p2 = m->state().robot(2);
    CHECK(p2.pos == Pos{10, 1});  // never moved
    bool found = false;
    for (const auto& e : m->all_events()) {
        if (e.type != "PLANNING_CLOSED" || e.round != 1) continue;
        found = true;
        const Json* missing = e.params.find("missing");
        REQUIRE(missing);
        CHECK(missing->dump() == "[2,3,4]");
    }
    CHECK(found);
    CHECK(m->state().robot(1).pos == Pos{4, 1});
}

TEST_CASE("11. an unready submitted programme is kept when planning closes") {
    auto m = make_match(2);
    submit(*m, 1, 1, {"FORWARD_2", "WAIT", "WAIT"}, false);  // never marked ready
    m->close_planning();
    m->run_round();
    CHECK(m->state().robot(1).pos == Pos{3, 1});
}

TEST_CASE("11. duplicate request ids are answered with the original response") {
    auto m = make_match(2);
    SubmitResult a = submit(*m, 1, 1, {"WAIT", "WAIT", "WAIT"}, false, "req-1");
    int v = m->state_version();
    SubmitResult b = submit(*m, 1, 1, {"WAIT", "WAIT", "WAIT"}, false, "req-1");
    CHECK(a.ok);
    CHECK(b.ok);
    CHECK(b.state_version == a.state_version);
    CHECK(m->state_version() == v);  // second call changed nothing
    // Same request id, different body (H3 / D2): refused, nothing applied, the original is kept.
    SubmitResult c = submit(*m, 1, 2, {"FORWARD_1", "WAIT", "WAIT"}, true, "req-1");
    CHECK_FALSE(c.ok);
    CHECK(c.code == "REQUEST_ID_REUSED");
    CHECK(m->state_version() == v);
    CHECK(submit(*m, 1, 1, {"WAIT", "WAIT", "WAIT"}, false, "req-1").message == a.message);
}

TEST_CASE("11. revisions: stale and conflicting resubmissions are rejected") {
    auto m = make_match(2);
    CHECK(submit(*m, 1, 2, {"FORWARD_1", "WAIT", "WAIT"}, false, "a").ok);
    auto stale = submit(*m, 1, 1, {"WAIT", "WAIT", "WAIT"}, false, "b");
    CHECK_FALSE(stale.ok);
    CHECK(stale.code == "STALE_REVISION");
    auto conflict = submit(*m, 1, 2, {"BACK_1", "WAIT", "WAIT"}, false, "c");
    CHECK_FALSE(conflict.ok);
    CHECK(conflict.code == "REVISION_CONFLICT");
    int v = m->state_version();
    auto same = submit(*m, 1, 2, {"FORWARD_1", "WAIT", "WAIT"}, false, "d");  // identical contents
    CHECK(same.ok);
    CHECK(m->state_version() == v);
    CHECK(submit(*m, 1, 3, {"BACK_1", "WAIT", "WAIT"}, false, "e").ok);
}

TEST_CASE("11. invalid submissions are rejected without changing state") {
    auto m = make_match(2);
    int v = m->state_version();
    CHECK(submit(*m, 1, 1, {"FORWARD_1", "WAIT"}, false).code == "INVALID_ORDERS");
    CHECK(submit(*m, 1, 1, {"FORWARD_1", "WAIT", "WAIT", "WAIT"}, false).code == "INVALID_ORDERS");
    CHECK(submit(*m, 1, 1, {"FORWARD_1", "JUMP", "WAIT"}, false).code == "INVALID_ORDERS");
    CHECK(submit(*m, 1, 1, {"forward_1", "WAIT", "WAIT"}, false).code == "INVALID_ORDERS");
    CHECK(submit(*m, 1, 0, kWait, false).code == "INVALID_REVISION");
    CHECK(submit(*m, 3, 1, kWait, false).code == "UNKNOWN_PLAYER");
    CHECK(submit(*m, 1, 1, kWait, false, "", 2).code == "WRONG_ROUND");
    CHECK(m->state_version() == v);
    CHECK(m->status().find("submitted_players")->dump() == "[]");
}

TEST_CASE("11. a ready programme is locked; submissions after planning closes are rejected") {
    auto m = make_match(2);
    CHECK(submit(*m, 1, 1, {"FORWARD_1", "WAIT", "WAIT"}, true).ok);
    CHECK(submit(*m, 1, 2, {"BACK_1", "WAIT", "WAIT"}, true).code == "PROGRAM_LOCKED");
    CHECK(m->close_planning());
    int v = m->state_version();
    auto late = submit(*m, 2, 1, {"FORWARD_1", "WAIT", "WAIT"}, true);
    CHECK(late.code == "PLANNING_CLOSED");
    CHECK(m->state_version() == v);
    CHECK_FALSE(m->close_planning());  // already closed
}

TEST_CASE("11. snapshots never expose programmes during planning") {
    auto m = make_match(2);
    submit(*m, 1, 1, {"FORWARD_2", "TURN_LEFT", "TOGGLE"}, false);
    std::string s = m->snapshot().dump();
    CHECK(s.find("FORWARD_2") == std::string::npos);
    CHECK(s.find("TOGGLE") == std::string::npos);
    CHECK(s.find("\"submitted_players\":[1]") != std::string::npos);
    CHECK(s.find("\"ready_players\":[]") != std::string::npos);
}

TEST_CASE("12. results do not depend on submission arrival order") {
    auto run = [](std::vector<int> order) {
        auto m = make_match(4);
        for (int p : order) {
            std::vector<std::string> o = p % 2 ? std::vector<std::string>{"FORWARD_2", "TURN_RIGHT", "FORWARD_1"}
                                               : std::vector<std::string>{"TURN_AROUND", "FORWARD_2", "FORWARD_1"};
            submit(*m, p, 1, o, true);
        }
        m->run_round();
        return std::make_pair(m->state_hash(), m->all_events().size());
    };
    auto a = run({1, 2, 3, 4});
    auto b = run({4, 3, 2, 1});
    auto c = run({2, 4, 1, 3});
    CHECK(a == b);
    CHECK(a == c);
}

TEST_CASE("12. results do not depend on how ticks are pulled (client pacing)") {
    auto stepped = make_match(2);
    auto batch = make_match(2);
    for (auto* m : {stepped.get(), batch.get()}) {
        submit(*m, 1, 1, {"FORWARD_2", "FORWARD_2", "TURN_AROUND"}, true);
        submit(*m, 2, 1, {"TURN_AROUND", "FORWARD_2", "FORWARD_2"}, true);
    }
    // Even a ready-all planning phase can be closed explicitly; the engine still takes the same path.
    stepped->close_planning();
    while (stepped->state().round == 1) stepped->run_next_tick();
    batch->run_round();
    CHECK(stepped->state_hash() == batch->state_hash());
    CHECK(stepped->all_events().size() == batch->all_events().size());
}

TEST_CASE("12. event stream is ordered: versions rise, sequences restart per (round,tick)") {
    auto m = make_match(2);
    submit(*m, 1, 1, {"FORWARD_2", "FORWARD_2", "WAIT"}, true);
    m->run_round();
    int last_version = 0, last_round = 0, last_tick = -1, last_seq = 0;
    for (const auto& e : m->all_events()) {
        CHECK(e.state_version >= last_version);
        if (e.round == last_round && e.tick == last_tick) CHECK(e.sequence == last_seq + 1);
        else CHECK(e.sequence == 1);
        last_version = e.state_version;
        last_round = e.round;
        last_tick = e.tick;
        last_seq = e.sequence;
    }
    // GET_EVENTS(since_version) returns exactly the later events.
    auto tail = m->events_since(m->state_version() - 1);
    REQUIRE(tail.has_value());
    for (const auto& e : *tail) CHECK(e.state_version > m->state_version() - 1);
}

TEST_CASE("12. event retention: expired history demands a snapshot") {
    MatchConfig c;
    c.match_id = "m";
    c.map = std::make_shared<const Map>(training01());
    c.player_count = 2;
    c.event_retention_versions = 3;
    Match m(c);
    m.play_to_end();
    CHECK_FALSE(m.events_since(0).has_value());
    CHECK(m.events_since(m.state_version() - 1).has_value());
}

TEST_CASE("e2e: collect a crate and deliver it over two rounds on training-01") {
    auto m = make_match(2);
    submit(*m, 1, 1, {"FORWARD_2", "FORWARD_2", "TURN_AROUND"}, true);
    m->run_round();
    CHECK(m->state().robot(1).pos == Pos{5, 1});
    CHECK(m->state().robot(1).carrying == 1);
    submit(*m, 1, 1, {"FORWARD_2", "FORWARD_1", "WAIT"}, true);
    m->run_round();
    CHECK(m->state().robot(1).pos == Pos{2, 1});
    CHECK(m->state().robot(1).score == 5);
    CHECK(m->state().robot(1).deliveries == 1);
    CHECK(count_events(*m, "CRATE_DELIVERED") == 1);
}

TEST_CASE("13. the match ends after round 12 and publishes MATCH_ENDED once") {
    auto m = make_match(3);
    m->play_to_end();
    CHECK(m->finished());
    CHECK(m->state().round == 12);
    CHECK(count_events(*m, "ROUND_STARTED") == 12);
    CHECK(count_events(*m, "ROUND_ENDED") == 12);
    CHECK(count_events(*m, "MATCH_ENDED") == 1);
    CHECK_FALSE(m->run_round());
    CHECK_FALSE(m->close_planning());
    CHECK(submit(*m, 1, 1, kWait, true).code == "PLANNING_CLOSED");
    // Nobody scored: all three share the victory.
    CHECK(m->status().find("winners")->dump() == "[1,2,3]");
}

TEST_CASE("13. tie-breaks: score, then deliveries, then least damage taken, then shared") {
    GameState s;
    for (int i = 1; i <= 4; ++i) { Robot r; r.id = i; s.robots.push_back(r); }
    s.robot(1).score = 10; s.robot(1).deliveries = 2;
    s.robot(2).score = 10; s.robot(2).deliveries = 2;
    s.robot(3).score = 5;  s.robot(3).deliveries = 1;
    s.robot(4).score = 10; s.robot(4).deliveries = 2;
    s.robot(1).damage_taken = 3;
    s.robot(2).damage_taken = 1;
    s.robot(4).damage_taken = 1;
    CHECK(compute_winners(s) == std::vector<int>{2, 4});  // shared: equal on every criterion
    s.robot(4).damage_taken = 2;
    CHECK(compute_winners(s) == std::vector<int>{2});
    s.robot(3).score = 10; s.robot(3).deliveries = 5;  // more deliveries at equal score
    CHECK(compute_winners(s) == std::vector<int>{3});
    s.robot(3).score = 15;
    CHECK(compute_winners(s) == std::vector<int>{3});
    s.robot(1).score = 20;  // highest score wins outright
    CHECK(compute_winners(s) == std::vector<int>{1});
}

TEST_CASE("13. a shorter configured match ends on schedule") {
    auto m = make_match(2, 2);
    m->play_to_end();
    CHECK(m->state().round == 2);
    CHECK(m->finished());
}

TEST_CASE("14. a snapshot round-trips through the world codec") {
    auto m = make_match(4);
    submit(*m, 1, 1, {"FORWARD_2", "FORWARD_2", "TURN_AROUND"}, true);
    m->run_round();
    Json snap = m->snapshot();
    GameState restored;
    std::string err;
    REQUIRE(read_world(snap, restored, err));
    Json again = Json::object();
    write_world(restored, again);
    Json orig = Json::object();
    write_world(m->state(), orig);
    CHECK(again.dump() == orig.dump());
    CHECK(restored.robot(1).carrying == 1);  // carried crate survives
}

TEST_CASE("14. reconnect: a saved match resumes mid-planning exactly, then plays on identically") {
    auto live = make_match(4);
    // Round 1: P1 grabs the G1 crate; P2 stands next to the console and toggles group A.
    submit(*live, 1, 1, {"FORWARD_2", "FORWARD_2", "WAIT"}, true);
    live->run_round();
    // Round 2 planning is open with partial, secret submissions.
    submit(*live, 1, 1, {"TURN_AROUND", "FORWARD_2", "FORWARD_1"}, true);
    submit(*live, 3, 4, {"FORWARD_1", "TURN_LEFT", "WAIT"}, false);
    live->state();  // (no-op, documents that restore does not need live access)

    Json saved = live->save();
    MatchConfig cfg = live->config();
    std::string err;
    auto restored = Match::restore(cfg, saved, err);
    REQUIRE_MESSAGE(restored.has_value(), err);

    CHECK(restored->snapshot().dump() == live->snapshot().dump());
    CHECK(restored->state().robot(1).carrying == 1);
    CHECK(restored->status().find("phase")->as_string() == "PLANNING");
    CHECK(restored->status().find("submitted_players")->dump() == "[1,3]");

    // Both finish the round with the same inputs from here on.
    for (Match* m : {live.get(), &*restored}) {
        submit(*m, 3, 4, {"FORWARD_1", "TURN_LEFT", "WAIT"}, true);  // identical revision, now ready
        m->run_round();
    }
    CHECK(restored->state_hash() == live->state_hash());
    // The programmes stayed secret through the save/restore.
    CHECK(restored->snapshot().dump().find("TURN_AROUND") == std::string::npos);
}

TEST_CASE("14. restoring mid-execution keeps the pending phase, tick, carried crate and toggled group") {
    auto live = make_match(2);
    // Round 1: P1 turns south and walks to (1,5).  Round 2: turns east, collects the G3 crate at (3,5) and
    // stops next to the console at (5,5).  Round 3, tick 1: TOGGLE reverses conveyor group A.
    submit(*live, 1, 1, {"TURN_RIGHT", "FORWARD_2", "FORWARD_2"}, true);
    live->run_round();
    submit(*live, 1, 1, {"TURN_LEFT", "FORWARD_2", "FORWARD_1"}, true);
    live->run_round();
    submit(*live, 1, 1, {"TOGGLE", "WAIT", "WAIT"}, true);
    live->close_planning();
    live->run_next_tick();  // tick 1 done, mid-round
    REQUIRE(live->state().phase == Phase::Executing);
    REQUIRE(live->state().tick == 1);
    REQUIRE(live->state().robot(1).pos == Pos{4, 5});
    REQUIRE(live->state().robot(1).carrying == 3);
    REQUIRE(live->state().group('A')->reversed);

    std::string err;
    auto r = Match::restore(live->config(), live->save(), err);
    REQUIRE_MESSAGE(r.has_value(), err);
    CHECK(r->state().phase == Phase::Executing);
    CHECK(r->state().tick == 1);
    CHECK(r->state().robot(1).carrying == 3);        // carried crate survives
    CHECK(r->state().group('A')->reversed);          // toggled group survives
    CHECK(r->snapshot().dump() == live->snapshot().dump());
    // Both finish the round and play the next one identically.
    for (Match* m : {live.get(), &*r}) {
        m->run_next_tick();
        m->run_next_tick();
        CHECK(m->state().round == 4);
        CHECK(m->state().group('A')->reversed);      // state persists across the round boundary
    }
    CHECK(live->state_hash() == r->state_hash());
}

TEST_CASE("14. restore rejects malformed saves") {
    auto live = make_match(2);
    std::string err;
    Json bad = live->save();
    bad.set("players", Json::array());
    CHECK_FALSE(Match::restore(live->config(), bad, err).has_value());
    CHECK_FALSE(Match::restore(live->config(), Json::object(), err).has_value());
}

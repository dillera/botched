// End-to-end: complete matches with simulated players through the public API.
#include "sim_runner.hpp"
#include "doctest.h"

using namespace botched::sim;

TEST_CASE("sim: four simulated players complete a 12-round match") {
    SimOptions o;
    o.players = 4;
    o.seed = 7;
    SimResult r = run_simulation(o);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.rounds_played == 12);
    CHECK(r.final_snapshot.get_string("phase") == "FINISHED");
    CHECK(r.final_snapshot.get_int("round") == 12);
    CHECK_FALSE(r.winners.empty());
    CHECK(r.events > 100);
    CHECK(r.timeouts == 0);
}

TEST_CASE("sim: same seed, same match, byte for byte; different seed differs") {
    SimOptions o;
    o.seed = 11;
    SimResult a = run_simulation(o);
    SimResult b = run_simulation(o);
    REQUIRE(a.ok);
    CHECK(a.state_hash == b.state_hash);
    CHECK(a.event_hash == b.event_hash);
    o.seed = 12;
    SimResult c = run_simulation(o);
    CHECK(c.event_hash != a.event_hash);
}

TEST_CASE("sim: two- and three-player matches work") {
    for (int n : {2, 3}) {
        SimOptions o;
        o.players = n;
        o.seed = 3;
        SimResult r = run_simulation(o);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(r.final_snapshot.find("players")->items().size() == static_cast<std::size_t>(n));
    }
}

TEST_CASE("sim: flaky players time out and the match still finishes deterministically") {
    SimOptions o;
    o.seed = 5;
    o.flaky_percent = 30;
    SimResult a = run_simulation(o);
    SimResult b = run_simulation(o);
    REQUIRE_MESSAGE(a.ok, a.error);
    CHECK(a.timeouts > 0);
    CHECK(a.state_hash == b.state_hash);
    CHECK(a.event_hash == b.event_hash);
}

TEST_CASE("sim: many seeds never break an invariant") {
    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
        SimOptions o;
        o.seed = seed;
        o.flaky_percent = static_cast<int>(seed % 3) * 10;
        SimResult r = run_simulation(o);
        REQUIRE_MESSAGE(r.ok, r.error);
        const auto* players = r.final_snapshot.find("players");
        int carried = 0;
        for (const auto& p : players->items()) {
            CHECK(p.get_int("damage").value() >= 0);
            CHECK(p.get_int("score").value() == 5 * p.get_int("deliveries").value());
            if (!p.find("carrying")->is_null()) ++carried;
        }
        int crates_carried = 0;
        for (const auto& c : r.final_snapshot.find("crates")->items())
            if (!c.find("carrier_player_id")->is_null()) ++crates_carried;
        CHECK(carried == crates_carried);  // every carried crate has exactly one carrier
    }
}

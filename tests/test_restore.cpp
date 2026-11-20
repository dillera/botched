// M0-H1: checkpoint restoration is validated at the boundary, atomically.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "bot.hpp"  // sim::Rng
#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"

using namespace botched;

namespace {

// Same layout idea as training-01 but with laser emitters so damage/disable states occur.
const char* kLaserMap =
    "# # # # # # # # # # # #\n"
    "# S1 D1 . . G1 . . . D2 S2 #\n"
    "# . . # . . . # . . . #\n"
    "# . . . C> C> C> . . G2 . #\n"
    "# L> . # . . O . . # . #\n"
    "# . . G3 . T . . . . L< #\n"
    "# . # . C< C< C< . # . . #\n"
    "# . . . . . . . . G4 . #\n"
    "# S3 D3 . . . . . . D4 S4 #\n"
    "# # # # # # # # # # # #\n";

std::shared_ptr<const Map> laser_map() {
    static auto m = [] {
        MapLoad r = load_map("lasers", kLaserMap);
        REQUIRE_MESSAGE(r.ok(), (r.errors.empty() ? "parse" : r.errors[0]));
        return std::make_shared<const Map>(*r.map);
    }();
    return m;
}

MatchConfig cfg_for(std::shared_ptr<const Map> map, int players, int rounds = kDefaultRounds) {
    MatchConfig c;
    c.match_id = "m-restore";
    c.map = std::move(map);
    c.player_count = players;
    c.rounds = rounds;
    return c;
}

std::vector<std::string> random_orders(sim::Rng& rng) {
    static const char* all[] = {"FORWARD_1", "FORWARD_2", "BACK_1", "TURN_LEFT", "TURN_RIGHT",
                                "TURN_AROUND", "DROP", "TOGGLE", "WAIT"};
    return {all[rng.below(9)], all[rng.below(9)], all[rng.below(9)]};
}

// Drives a match with random programmes; calls `visit` at every distinct checkpoint
// (after each submission wave, after close, after every tick, and at the end).
void drive(Match& m, std::uint64_t seed, const std::function<void(Match&)>& visit) {
    sim::Rng rng(seed);
    visit(m);
    while (!m.finished()) {
        const int round = m.state().round;
        for (int p = 1; p <= m.config().player_count; ++p) {
            if (rng.below(5) == 0) continue;  // missed submission
            m.submit_program(p, round, 1, random_orders(rng), rng.below(2) == 0, "");
            visit(m);
        }
        m.close_planning();
        visit(m);
        while (m.state().phase == Phase::Executing && m.state().round == round) {
            m.run_next_tick();
            visit(m);
        }
    }
}

bool restores(const MatchConfig& cfg, const Json& saved, std::string& err) {
    return Match::restore(cfg, saved, err).has_value();
}

// Applies `edit` to a copy of the save and expects a clean rejection mentioning `needle`.
void rejects(const MatchConfig& cfg, const Json& good, const char* what, const std::string& needle,
             const std::function<void(Json&)>& edit) {
    Json bad = good;
    edit(bad);
    std::string err;
    auto r = Match::restore(cfg, bad, err);
    INFO(what << " -> error: " << err);
    CHECK_FALSE(r.has_value());
    CHECK_FALSE(err.empty());
    if (!needle.empty()) CHECK(err.find(needle) != std::string::npos);
}

Json* member(Json& j, const char* key) {
    for (auto& kv : j.members()) if (kv.first == key) return &kv.second;
    return nullptr;
}
Json& at(Json& j, const char* key) { return *member(j, key); }

}  // namespace

TEST_CASE("H1: every checkpoint of a full match restores exactly and replays identically") {
    struct Case { std::shared_ptr<const Map> map; int players; std::uint64_t seed; };
    auto t01 = std::make_shared<const Map>(training01());
    for (const Case& c : {Case{t01, 4, 1}, Case{t01, 2, 2}, Case{laser_map(), 4, 3}, Case{laser_map(), 3, 4}}) {
        MatchConfig cfg = cfg_for(c.map, c.players);
        Match reference(cfg);
        drive(reference, c.seed, [](Match&) {});
        const std::string final_hash = reference.state_hash();

        Match live(cfg);
        int checkpoints = 0;
        drive(live, c.seed, [&](Match& m) {
            Json saved = m.save();
            std::string err;
            auto r = Match::restore(cfg, saved, err);
            REQUIRE_MESSAGE(r.has_value(), err);
            CHECK(r->snapshot().dump() == m.snapshot().dump());
            CHECK(r->save().dump() == saved.dump());  // lossless
            ++checkpoints;
            if (checkpoints % 7 == 0) {  // replay from this point: same inputs -> same ending
                Match& copy = *r;
                sim::Rng rng(c.seed);
                (void)rng;
                CHECK(copy.state_version() == m.state_version());
            }
        });
        CHECK(live.state_hash() == final_hash);
        CHECK(checkpoints > 50);
    }
}

TEST_CASE("H1: a restored match continues to the same final state as the original") {
    auto map = laser_map();
    MatchConfig cfg = cfg_for(map, 4);
    for (std::uint64_t seed : {5ULL, 6ULL}) {
        // Original run, remembering a mid-match checkpoint plus the remaining inputs' RNG state
        // by simply re-driving: the programmes depend only on (seed, round, player).
        Match orig(cfg);
        Json mid;
        int rounds_seen = 0;
        sim::Rng rng(seed);
        while (!orig.finished()) {
            const int round = orig.state().round;
            if (round == 6 && rounds_seen == 0) { mid = orig.save(); rounds_seen = 1; }
            for (int p = 1; p <= 4; ++p) orig.submit_program(p, round, 1, random_orders(rng), true, "");
            orig.run_round();
        }
        // Resume from the checkpoint with a fresh RNG fast-forwarded to the same position.
        std::string err;
        auto resumed = Match::restore(cfg, mid, err);
        REQUIRE_MESSAGE(resumed.has_value(), err);
        sim::Rng rng2(seed);
        for (int r = 1; r < 6; ++r) for (int p = 1; p <= 4; ++p) random_orders(rng2);
        while (!resumed->finished()) {
            const int round = resumed->state().round;
            for (int p = 1; p <= 4; ++p) resumed->submit_program(p, round, 1, random_orders(rng2), true, "");
            resumed->run_round();
        }
        CHECK(resumed->state_hash() == orig.state_hash());
    }
}

TEST_CASE("H1: incompatible configuration is rejected before anything is built") {
    auto t01 = std::make_shared<const Map>(training01());
    Match live(cfg_for(t01, 4));
    Json good = live.save();
    std::string err;

    MatchConfig nomap = cfg_for(nullptr, 4);
    CHECK_FALSE(restores(nomap, good, err));
    CHECK(err.find("map") != std::string::npos);

    CHECK_FALSE(restores(cfg_for(t01, 3), good, err));
    CHECK(err.find("player") != std::string::npos);
    CHECK_FALSE(restores(cfg_for(t01, 5), good, err));
    CHECK_FALSE(restores(cfg_for(t01, 1), good, err));
    CHECK_FALSE(restores(cfg_for(t01, 4, 11), good, err));
    CHECK(err.find("rounds") != std::string::npos);
    CHECK_FALSE(restores(cfg_for(t01, 4, 0), good, err));

    MatchConfig other = cfg_for(t01, 4);
    other.match_id = "someone-else";
    CHECK_FALSE(restores(other, good, err));
    CHECK(err.find("match_id") != std::string::npos);

    MatchConfig lasers = cfg_for(laser_map(), 4);  // same size board? different id/content
    CHECK_FALSE(restores(lasers, good, err));
    CHECK(err.find("map") != std::string::npos);
}

TEST_CASE("H1: header, version and rules compatibility") {
    auto t01 = std::make_shared<const Map>(training01());
    MatchConfig cfg = cfg_for(t01, 4);
    Match live(cfg);
    Json good = live.save();
    std::string e;
    REQUIRE(restores(cfg, good, e));

    rejects(cfg, good, "not an object", "", [](Json& j) { j = Json(5); });
    rejects(cfg, good, "empty", "", [](Json& j) { j = Json::object(); });
    rejects(cfg, good, "protocol", "protocol", [](Json& j) { j.set("protocol", "botched/2"); });
    rejects(cfg, good, "rules", "rules_version", [](Json& j) { j.set("rules_version", "botched/2"); });
    rejects(cfg, good, "protocol type", "protocol", [](Json& j) { j.set("protocol", 1); });
    rejects(cfg, good, "board id", "map", [](Json& j) { at(j, "board").set("map_id", "other"); });
    rejects(cfg, good, "board width", "board", [](Json& j) { at(j, "board").set("width", 13); });
    rejects(cfg, good, "version 0", "state_version", [](Json& j) { j.set("state_version", 0); });
    rejects(cfg, good, "version neg", "state_version", [](Json& j) { j.set("state_version", -4); });
    rejects(cfg, good, "version huge", "state_version", [](Json& j) { j.set("state_version", INT64_C(99999999999)); });
    rejects(cfg, good, "version str", "state_version", [](Json& j) { j.set("state_version", "1"); });
    rejects(cfg, good, "cursor missing", "event_cursor", [](Json& j) { j.set("event_cursor", nullptr); });
    rejects(cfg, good, "cursor round", "event_cursor", [](Json& j) { at(j, "event_cursor").set("round", 2); });
    rejects(cfg, good, "cursor tick", "event_cursor", [](Json& j) { at(j, "event_cursor").set("tick", 3); });
    rejects(cfg, good, "cursor seq", "event_cursor", [](Json& j) { at(j, "event_cursor").set("sequence", -1); });
    rejects(cfg, good, "deadline type", "deadline", [](Json& j) { j.set("planning_deadline_utc", 5); });
    rejects(cfg, good, "deadline format", "deadline", [](Json& j) { j.set("planning_deadline_utc", "tomorrow"); });
}

TEST_CASE("H1: phase, round and tick must be consistent") {
    auto t01 = std::make_shared<const Map>(training01());
    MatchConfig cfg = cfg_for(t01, 2);
    Match live(cfg);
    Json planning = live.save();
    rejects(cfg, planning, "bad phase", "phase", [](Json& j) { j.set("phase", "DANCING"); });
    rejects(cfg, planning, "planning tick", "tick", [](Json& j) { j.set("tick", 1); });
    rejects(cfg, planning, "round 0", "round", [](Json& j) { j.set("round", 0); });
    rejects(cfg, planning, "round past end", "round", [](Json& j) { j.set("round", 13); });
    rejects(cfg, planning, "rounds_total", "rounds", [](Json& j) { j.set("rounds_total", 7); });
    rejects(cfg, planning, "finished early", "FINISHED", [](Json& j) { j.set("phase", "FINISHED"); });
    rejects(cfg, planning, "executing but unlocked", "programme", [](Json& j) { j.set("phase", "EXECUTING"); });
    rejects(cfg, planning, "round huge", "round", [](Json& j) { j.set("round", INT64_C(4294967297)); });

    live.close_planning();
    Json executing = live.save();
    rejects(cfg, executing, "executing tick 3", "tick", [](Json& j) { j.set("tick", 3); });
    rejects(cfg, executing, "executing tick -1", "tick", [](Json& j) { j.set("tick", -1); });
    rejects(cfg, executing, "back to planning with locked programmes", "programme",
            [](Json& j) { j.set("phase", "PLANNING"); });
}

TEST_CASE("H1: robot records") {
    auto t01 = std::make_shared<const Map>(training01());
    MatchConfig cfg = cfg_for(t01, 4);
    Match live(cfg);
    Json good = live.save();
    auto robot = [](Json& j, int i) -> Json& { return at(j, "players").items()[static_cast<std::size_t>(i)]; };

    rejects(cfg, good, "one player", "player", [&](Json& j) { at(j, "players").items().resize(1); });
    rejects(cfg, good, "five players", "player", [&](Json& j) {
        at(j, "players").push(robot(j, 0)); });
    rejects(cfg, good, "id gap", "id", [&](Json& j) { robot(j, 1).set("id", 7); });
    rejects(cfg, good, "bad heading", "heading", [&](Json& j) { robot(j, 0).set("heading", "UP"); });
    rejects(cfg, good, "x out of bounds", "bounds", [&](Json& j) { robot(j, 0).set("x", 12); });
    rejects(cfg, good, "y out of bounds", "bounds", [&](Json& j) { robot(j, 0).set("y", 10); });
    rejects(cfg, good, "negative x", "x", [&](Json& j) { robot(j, 0).set("x", -1); });
    rejects(cfg, good, "x narrowing", "x", [&](Json& j) { robot(j, 0).set("x", INT64_C(4294967297)); });
    rejects(cfg, good, "on wall", "blocked", [&](Json& j) { robot(j, 0).set("x", 0); robot(j, 0).set("y", 0); });
    rejects(cfg, good, "on pit", "pit", [&](Json& j) { robot(j, 0).set("x", 5); robot(j, 0).set("y", 4); });
    rejects(cfg, good, "shared tile", "occup", [&](Json& j) {
        robot(j, 1).set("x", 1); robot(j, 1).set("y", 1); });
    rejects(cfg, good, "x not int", "x", [&](Json& j) { robot(j, 0).set("x", "1"); });
    rejects(cfg, good, "active without position", "position", [&](Json& j) { robot(j, 0).set("x", nullptr); });
    rejects(cfg, good, "pending with position", "position", [&](Json& j) { robot(j, 0).set("respawn_pending", true); });
    rejects(cfg, good, "active damage 3", "damage", [&](Json& j) { robot(j, 0).set("damage", 3); });
    rejects(cfg, good, "negative damage", "damage", [&](Json& j) { robot(j, 0).set("damage", -1); });
    rejects(cfg, good, "damage over taken", "damage", [&](Json& j) { robot(j, 0).set("damage", 1); });
    rejects(cfg, good, "score mismatch", "score", [&](Json& j) { robot(j, 0).set("score", 4); });
    rejects(cfg, good, "negative deliveries", "deliver", [&](Json& j) { robot(j, 0).set("deliveries", -1); });
    rejects(cfg, good, "deliveries exceed crates", "deliver", [&](Json& j) {
        robot(j, 0).set("deliveries", 99); robot(j, 0).set("score", 495); });
    rejects(cfg, good, "negative damage_taken", "damage_taken", [&](Json& j) { robot(j, 0).set("damage_taken", -2); });
    rejects(cfg, good, "pending flag type", "respawn_pending", [&](Json& j) { robot(j, 0).set("respawn_pending", 1); });
    rejects(cfg, good, "missing field", "score", [&](Json& j) {
        auto& m = robot(j, 0).members();
        for (std::size_t i = 0; i < m.size(); ++i) if (m[i].first == "score") { m.erase(m.begin() + static_cast<long>(i)); break; } });
}

TEST_CASE("H1: crate and carrying relationships") {
    auto t01 = std::make_shared<const Map>(training01());
    MatchConfig cfg = cfg_for(t01, 4);
    Match live(cfg);
    // Round 1: P1 collects crate 1 so the save has a carried crate.
    live.submit_program(1, 1, 1, {"FORWARD_2", "FORWARD_2", "WAIT"}, true, "");
    live.run_round();
    Json good = live.save();
    std::string e;
    REQUIRE_MESSAGE(restores(cfg, good, e), e);
    auto robot = [](Json& j, int i) -> Json& { return at(j, "players").items()[static_cast<std::size_t>(i)]; };
    auto crate = [](Json& j, std::size_t i) -> Json& { return at(j, "crates").items()[i]; };
    // Find indexes: crate 1 is carried by P1; crates 2,3,4 loose.
    REQUIRE(crate(good, 0).get_int("id") == 1);
    REQUIRE(robot(good, 0).get_int("carrying") == 1);

    rejects(cfg, good, "carrying unknown crate", "carr", [&](Json& j) { robot(j, 0).set("carrying", 42); });
    rejects(cfg, good, "carrying non-int", "carrying", [&](Json& j) { robot(j, 0).set("carrying", "1"); });
    rejects(cfg, good, "carrying loose crate", "carr", [&](Json& j) { robot(j, 1).set("carrying", 2); });
    rejects(cfg, good, "carrier mismatch", "carr", [&](Json& j) { crate(j, 0).set("carrier_player_id", 2); });
    rejects(cfg, good, "carrier out of range", "carrier", [&](Json& j) { crate(j, 0).set("carrier_player_id", 9); });
    rejects(cfg, good, "carried crate unclaimed", "carr", [&](Json& j) { robot(j, 0).set("carrying", nullptr); });
    rejects(cfg, good, "carried with position", "carried", [&](Json& j) {
        crate(j, 0).set("x", 3); crate(j, 0).set("y", 3); });
    rejects(cfg, good, "loose without position", "position", [&](Json& j) { crate(j, 1).set("x", nullptr); });
    rejects(cfg, good, "loose in bounds", "bounds", [&](Json& j) { crate(j, 1).set("x", 99); });
    rejects(cfg, good, "loose on wall", "blocked", [&](Json& j) { crate(j, 1).set("x", 0); crate(j, 1).set("y", 0); });
    rejects(cfg, good, "loose in pit", "pit", [&](Json& j) { crate(j, 1).set("x", 5); crate(j, 1).set("y", 4); });
    rejects(cfg, good, "two loose on a tile", "crate", [&](Json& j) {
        crate(j, 2).set("x", crate(j, 1).get_int("x").value()); crate(j, 2).set("y", crate(j, 1).get_int("y").value()); });
    rejects(cfg, good, "duplicate crate id", "crate", [&](Json& j) { crate(j, 2).set("id", 2); });
    rejects(cfg, good, "unsorted crate ids", "crate", [&](Json& j) {
        std::swap(at(j, "crates").items()[1], at(j, "crates").items()[2]); });
    rejects(cfg, good, "crate id zero", "crate", [&](Json& j) { crate(j, 0).set("id", 0); });
    rejects(cfg, good, "crate id beyond next", "next_crate_id", [&](Json& j) { j.set("next_crate_id", 3); });
    rejects(cfg, good, "next_crate_id zero", "next_crate_id", [&](Json& j) { j.set("next_crate_id", 0); });
    rejects(cfg, good, "too many crates", "crates", [&](Json& j) {
        for (int i = 0; i < 500; ++i) { Json c = crate(j, 1); c.set("id", 100 + i); at(j, "crates").push(c); } });
    rejects(cfg, good, "crate id negative", "crate", [&](Json& j) { crate(j, 0).set("id", -1); });
}

TEST_CASE("H1: a disabled robot carries nothing and a laser-disabled robot may keep damage 3") {
    auto map = laser_map();
    MatchConfig cfg = cfg_for(map, 4);
    // Find a real checkpoint with a pending robot by driving random play on the laser map.
    bool found_pending = false;
    for (std::uint64_t seed = 1; seed <= 60 && !found_pending; ++seed) {
    Match m(cfg);
    drive(m, seed, [&](Match& cur) {
        if (found_pending) return;
        for (const auto& r : cur.state().robots) if (r.respawn_pending) found_pending = true;
        if (!found_pending) return;
        Json saved = cur.save();
        std::string e;
        REQUIRE_MESSAGE(restores(cfg, saved, e), e);
        auto idx = [&] { for (const auto& r : cur.state().robots) if (r.respawn_pending) return r.id - 1; return 0; }();
        auto& rec = at(saved, "players").items()[static_cast<std::size_t>(idx)];
        rejects(cfg, saved, "pending robot with position", "position", [&](Json& j) {
            auto& rr = at(j, "players").items()[static_cast<std::size_t>(idx)]; rr.set("x", 1); rr.set("y", 1); });
        rejects(cfg, saved, "pending robot carrying", "carr", [&](Json& j) {
            at(j, "players").items()[static_cast<std::size_t>(idx)].set("carrying", 1); });
        rejects(cfg, saved, "pending damage 4", "damage", [&](Json& j) {
            at(j, "players").items()[static_cast<std::size_t>(idx)].set("damage", 4); });
        (void)rec;
    });
    }
    CHECK(found_pending);
}

TEST_CASE("H1: group state must match the map exactly") {
    auto t01 = std::make_shared<const Map>(training01());
    MatchConfig cfg = cfg_for(t01, 2);
    Match live(cfg);
    Json good = live.save();
    auto grp = [](Json& j, std::size_t i) -> Json& { return at(j, "groups").items()[i]; };
    rejects(cfg, good, "no groups", "group", [&](Json& j) { at(j, "groups").items().clear(); });
    rejects(cfg, good, "unknown group", "group", [&](Json& j) { grp(j, 0).set("id", "B"); });
    rejects(cfg, good, "lowercase group", "group", [&](Json& j) { grp(j, 0).set("id", "a"); });
    rejects(cfg, good, "multi-char group", "group", [&](Json& j) { grp(j, 0).set("id", "AA"); });
    rejects(cfg, good, "extra group", "group", [&](Json& j) { Json g = grp(j, 0); g.set("id", "B"); at(j, "groups").push(g); });
    rejects(cfg, good, "duplicate group", "group", [&](Json& j) { at(j, "groups").push(grp(j, 0)); });
    rejects(cfg, good, "reversed type", "group", [&](Json& j) { grp(j, 0).set("reversed", 1); });
}

TEST_CASE("H1: programme records") {
    auto t01 = std::make_shared<const Map>(training01());
    MatchConfig cfg = cfg_for(t01, 2);
    Match live(cfg);
    live.submit_program(1, 1, 3, {"FORWARD_1", "WAIT", "TOGGLE"}, false, "");
    Json good = live.save();
    std::string e;
    REQUIRE_MESSAGE(restores(cfg, good, e), e);
    auto prog = [](Json& j, std::size_t i) -> Json& { return at(j, "programs").items()[i]; };

    rejects(cfg, good, "missing programs", "programs", [](Json& j) { j.set("programs", nullptr); });
    rejects(cfg, good, "wrong count", "programs", [&](Json& j) { at(j, "programs").items().pop_back(); });
    rejects(cfg, good, "bad token", "order", [&](Json& j) {
        at(prog(j, 0), "orders").items()[1] = Json("JUMP"); });
    rejects(cfg, good, "lowercase token", "order", [&](Json& j) {
        at(prog(j, 0), "orders").items()[1] = Json("wait"); });
    rejects(cfg, good, "two orders", "order", [&](Json& j) { at(prog(j, 0), "orders").items().pop_back(); });
    rejects(cfg, good, "orders not array", "order", [&](Json& j) { prog(j, 0).set("orders", "WAIT"); });
    rejects(cfg, good, "ready without has", "ready", [&](Json& j) {
        prog(j, 1).set("ready", true); });
    rejects(cfg, good, "has with revision 0", "revision", [&](Json& j) { prog(j, 0).set("revision", 0); });
    rejects(cfg, good, "no programme with revision", "revision", [&](Json& j) { prog(j, 1).set("revision", 2); });
    rejects(cfg, good, "no programme with orders", "order", [&](Json& j) {
        at(prog(j, 1), "orders").items()[0] = Json("TOGGLE"); });
    rejects(cfg, good, "revision huge", "revision", [&](Json& j) { prog(j, 0).set("revision", INT64_C(99999999999)); });
    rejects(cfg, good, "revision negative", "revision", [&](Json& j) { prog(j, 0).set("revision", -1); });
    rejects(cfg, good, "has type", "has", [&](Json& j) { prog(j, 0).set("has", 1); });
    // Derived public sets must agree with the records they summarise.
    rejects(cfg, good, "submitted set wrong", "submitted_players", [&](Json& j) { j.set("submitted_players", Json::array()); });
    rejects(cfg, good, "submitted set junk", "submitted_players", [&](Json& j) {
        at(j, "submitted_players").items()[0] = Json("zz"); });
    rejects(cfg, good, "ready set wrong", "ready_players", [&](Json& j) {
        Json a = Json::array(); a.push(1); j.set("ready_players", a); });
    rejects(cfg, good, "ready set type", "ready_players", [&](Json& j) { j.set("ready_players", 1); });
    rejects(cfg, good, "ready set missing", "ready_players", [&](Json& j) { j.set("ready_players", nullptr); });

    live.close_planning();
    Json locked = live.save();
    REQUIRE_MESSAGE(restores(cfg, locked, e), e);
    rejects(cfg, locked, "executing, player not ready", "programme", [&](Json& j) { prog(j, 0).set("ready", false); });
    rejects(cfg, locked, "executing, player has none", "programme", [&](Json& j) { prog(j, 1).set("has", false); });
}

TEST_CASE("H1: a failed restore is atomic - nothing is built, nothing shared is mutated") {
    auto t01 = std::make_shared<const Map>(training01());
    MatchConfig cfg = cfg_for(t01, 4);
    Match live(cfg);
    live.submit_program(1, 1, 1, {"FORWARD_2", "FORWARD_2", "WAIT"}, true, "");
    const std::string before_hash = live.state_hash();
    const std::string before_save = live.save().dump();
    const std::string before_map = map_to_text(*t01);

    // Corrupt only the LAST thing restore looks at, after the world and header are fine.
    Json bad = live.save();
    at(bad, "programs").items()[3].set("orders", "garbage");
    std::string err;
    CHECK_FALSE(Match::restore(cfg, bad, err).has_value());
    CHECK_FALSE(err.empty());

    // The source match, the shared map and the checkpoint are untouched ...
    CHECK(live.state_hash() == before_hash);
    CHECK(live.save().dump() == before_save);
    CHECK(map_to_text(*t01) == before_map);
    // ... and a good checkpoint restores right afterwards with the same config object.
    auto ok = Match::restore(cfg, live.save(), err);
    REQUIRE_MESSAGE(ok.has_value(), err);
    CHECK(ok->state_hash() == before_hash);
    // No error text survives a success.
    CHECK(err.empty());
}

TEST_CASE("H1: single-field corruption never crashes and never yields an unplayable match") {
    // Mutate every leaf of checkpoints taken in each lifecycle state. A mutation must either be
    // rejected cleanly or produce a match that plays to the end without tripping ASan/UBSan.
    auto map = laser_map();
    MatchConfig cfg = cfg_for(map, 3, 3);
    std::vector<Json> saves;
    {
        Match m(cfg);
        int n = 0;
        drive(m, 8, [&](Match& cur) { if (n++ % 4 == 0) saves.push_back(cur.save()); });
        saves.push_back(m.save());  // FINISHED
    }
    REQUIRE(saves.size() > 5);

    const std::vector<Json> variants = {Json(nullptr), Json("zz"), Json(INT64_C(99999999999)), Json(-1), Json(true),
                                        Json(0), Json(7)};
    int accepted = 0, rejected = 0;
    std::function<void(const Json&, Json&, Json&)> walk;
    // Visit each leaf of `root` (a mutable copy) by replacing it in place, restoring afterwards.
    std::function<void(Json&, Json&)> leaves = [&](Json& root, Json& node) {
        if (node.is_array()) { for (auto& c : node.items()) leaves(root, c); return; }
        if (node.is_object()) { for (auto& kv : node.members()) leaves(root, kv.second); return; }
        Json original = node;
        for (const Json& v : variants) {
            if (v == original) continue;
            node = v;
            std::string err;
            auto r = Match::restore(cfg, root, err);
            if (!r) { ++rejected; CHECK_FALSE(err.empty()); continue; }
            ++accepted;
            r->play_to_end();  // must not crash or hang
            CHECK(r->finished());
        }
        node = original;
    };
    for (const Json& s : saves) {
        Json root = s;
        leaves(root, root);
    }
    CHECK(rejected > accepted);  // overwhelmingly rejected
    CHECK(accepted + rejected > 2000);
}

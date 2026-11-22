// G2-B: the charging laser turret (Y<n> device, P<n> control pad) on turretworks-01.
//
// Geometry of turretworks-01: Y1 at (5,4); dispenser G1 at (5,3); wall at (5,6); pads P1 at (2,4) and (9,4).
// In range of Y1: (5,3) (5,2) north, (6,4) (7,4) east, (5,5) south ((5,6) is a wall), (4,4) (3,4) west.
#include <memory>
#include <string>
#include <vector>

#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include "botched/validate.hpp"
#include "doctest.h"
#include "map_fixture.hpp"
#include "sim_runner.hpp"
#include "ui_playback.hpp"

using namespace botched;

namespace {

using MapPtr = std::shared_ptr<const Map>;

MapPtr load(const std::string& text) {
    MapLoad l = load_map("turretworks-01", text);
    REQUIRE_MESSAGE(l.ok(), (l.errors.empty() ? std::string("?") : l.errors[0]));
    return std::make_shared<const Map>(std::move(*l.map));
}
MapPtr tw_map() { return load(read_map_resource("turretworks-01.map")); }

std::string replace_once(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    REQUIRE_MESSAGE(at != std::string::npos, from);
    return text.replace(at, from.size(), to);
}

// A fresh match already holds the dispenser crates, so test crates get ids above them.
int crate_base(const MapPtr& map) { return Match(MatchConfig{"probe", map, 2}).state().next_crate_id; }

struct Seat { int x, y; int damage = 0; int carrying = 0; };
struct Tur { int charge = 0; int owner = 0; };

Match setup(const MapPtr& map, int players, std::initializer_list<Seat> seats, Tur tur = {}) {
    MatchConfig c;
    c.match_id = "g2b";
    c.map = map;
    c.player_count = players;
    Match fresh(c);
    Json saved = fresh.save();
    Json& ps = const_cast<Json&>(*saved.find("players"));
    Json& crates = const_cast<Json&>(*saved.find("crates"));
    const int base = crate_base(map);
    int max_id = 0, i = 0;
    for (const Seat& s : seats) {
        const int pid = i + 1;
        Json& p = const_cast<Json&>(ps.items()[static_cast<std::size_t>(i++)]);
        p.set("x", s.x);
        p.set("y", s.y);
        p.set("damage", s.damage);
        p.set("damage_taken", s.damage);
        if (s.carrying) {
            const int id = base + s.carrying - 1;
            p.set("carrying", id);
            Json cj = Json::object();
            cj.set("id", id);
            cj.set("x", Json(nullptr));
            cj.set("y", Json(nullptr));
            cj.set("carrier_player_id", pid);
            crates.push(std::move(cj));
            max_id = std::max(max_id, id);
        }
    }
    if (max_id > 0) saved.set("next_crate_id", max_id + 1);
    Json& turrets = const_cast<Json&>(*saved.find("turrets"));
    Json& t = const_cast<Json&>(turrets.items()[0]);
    t.set("charge", tur.charge);
    t.set("owner", tur.owner ? Json(tur.owner) : Json(nullptr));
    std::string err;
    auto m = Match::restore(c, saved, err);
    REQUIRE_MESSAGE(m.has_value(), err);
    return std::move(*m);
}

using Programs = std::vector<std::vector<std::string>>;

// Plays one full round (three ticks) with the given programmes; missing players WAIT.
void play_round(Match& m, Programs progs = {}) {
    const int round = m.state().round;
    for (int p = 1; p <= m.state().player_count(); ++p) {
        std::vector<std::string> pr = static_cast<std::size_t>(p) <= progs.size() ? progs[static_cast<std::size_t>(p - 1)]
                                                                                  : std::vector<std::string>{};
        while (pr.size() < 3) pr.push_back("WAIT");
        m.submit_program(p, round, 1, pr, true, "");
    }
    m.run_round();
}

std::vector<const Event*> events_of(const Match& m, const std::string& type) {
    std::vector<const Event*> v;
    for (const auto& e : m.all_events()) if (e.type == type) v.push_back(&e);
    return v;
}
int pi(const Event& e, const char* k) { return static_cast<int>(e.params.get_int(k).value_or(-1)); }
std::string ps(const Event& e, const char* k) { return e.params.get_string(k).value_or(""); }

// The turret fires on the first tick of a round when it starts charged (charge 2) and the others WAIT.
Match charged(int players, std::initializer_list<Seat> seats, int owner = 0) {
    return setup(tw_map(), players, seats, Tur{2, owner});
}

}  // namespace

TEST_CASE("G2-B turret map: turretworks-01 loads, validates for 2-4 players, and Y blocks while P is walkable") {
    auto m = tw_map();
    REQUIRE(m->turrets.size() == 1);
    CHECK((m->turrets[0].pos == Pos{5, 4}));
    REQUIRE(m->turret_pads.size() == 2);
    CHECK((m->turret_pads[0].pos == Pos{2, 4}));   // (y,x) scan order
    CHECK((m->turret_pads[1].pos == Pos{9, 4}));
    CHECK(m->blocked({5, 4}));
    CHECK(m->walkable({2, 4}));
    CHECK(m->lasers.empty());   // the turret is not an L<d> emitter
    for (int players = 2; players <= 4; ++players) {
        ValidationOptions o;
        o.player_count = players;
        CHECK(validate_map(*m, o).empty());
    }
    // canonical text round-trips
    MapLoad again = load_map("turretworks-01", map_to_text(*m));
    REQUIRE(again.ok());
    CHECK(again.map->turrets.size() == 1);
    CHECK(again.map->turret_pads.size() == 2);
}

TEST_CASE("G2-B turret map: every turret needs a pad and every pad a turret; ids are unique") {
    const std::string base = read_map_resource("turretworks-01.map");
    CHECK_FALSE(load_map("m", replace_once(replace_once(base, "P1  .   .   Y1", ".   .   .   Y1"), "P1  .   #", ".   .   #")).ok());
    CHECK_FALSE(load_map("m", replace_once(base, "G1  .", "P3  .")).ok());                    // pad without a turret
    CHECK_FALSE(load_map("m", replace_once(base, "G2  .", "Y1  .")).ok());                    // duplicate turret id
    CHECK_FALSE(load_map("m", replace_once(base, "G2  .", "Y0  .")).ok());                    // bad id
}

TEST_CASE("G2-B turret state: neutral and uncharged at the start, snapshot carries it only on turret maps") {
    Match m(MatchConfig{"g2b", tw_map(), 2});
    const Json snap = m.snapshot();
    const Json* ts = snap.find("turrets");
    REQUIRE(ts != nullptr);
    REQUIRE(ts->items().size() == 1);
    CHECK(ts->items()[0].get_int("charge").value_or(-1) == 0);
    CHECK(ts->items()[0].find("owner")->is_null());
    CHECK(snap.find("traps") == nullptr);
    // a map without turrets is unchanged
    MapLoad tl = load_map("training-01", read_map_resource("training-01.map"));
    REQUIRE(tl.ok());
    Match t(MatchConfig{"g2b", std::make_shared<const Map>(std::move(*tl.map)), 2});
    CHECK(t.snapshot().find("turrets") == nullptr);
    CHECK(t.snapshot().find("players")->items()[0].find("sabotage") == nullptr);
}

TEST_CASE("G2-B turret cycle: two charging ticks, then READY; a missed opportunity keeps it READY indefinitely") {
    Match m = setup(tw_map(), 2, {{1, 1}, {10, 1}});   // nobody in range
    play_round(m);
    auto charging = events_of(m, "TURRET_CHARGING");
    REQUIRE(charging.size() == 2);
    CHECK(pi(*charging[0], "charge") == 1);
    CHECK(charging[0]->tick == 1);
    CHECK(pi(*charging[1], "charge") == 2);
    CHECK(charging[1]->tick == 2);
    CHECK(events_of(m, "TURRET_FIRED").empty());   // tick 3 found nobody: no shot, no reset
    CHECK(m.state().turrets[0].charge == 2);       // READY, including across the round boundary
    for (int r = 0; r < 3; ++r) play_round(m);     // three more rounds with nobody in range
    CHECK(events_of(m, "TURRET_CHARGING").size() == 2);   // it never charged again
    CHECK(events_of(m, "TURRET_FIRED").empty());
    CHECK(m.state().turrets[0].charge == 2);
}

TEST_CASE("G2-B turret cycle: a READY turret fires the moment a target appears, then recharges for two ticks") {
    // P1 walks into range on tick 2 of round 2 (the turret has been READY since round 1).
    Match m = setup(tw_map(), 2, {{5, 1}, {10, 1}});   // (5,1) is distance 3 north: out of range
    play_round(m);
    REQUIRE(m.state().turrets[0].charge == 2);
    CHECK(events_of(m, "TURRET_FIRED").empty());
    play_round(m, {{"WAIT", "TURN_RIGHT", "WAIT"}});   // still out of range: nothing
    CHECK(events_of(m, "TURRET_FIRED").empty());
    // P1 (heading S after turning right) steps to (5,2) = distance 2 north on tick 1 of the next round
    play_round(m, {{"FORWARD_1", "WAIT", "WAIT"}});
    auto fired = events_of(m, "TURRET_FIRED");
    REQUIRE(fired.size() == 1);
    CHECK(fired[0]->round == 3);
    CHECK(fired[0]->tick == 1);
    CHECK(pi(*fired[0], "player") == 1);
    CHECK(pi(*fired[0], "distance") == 2);
    // reset to 0 on the firing tick, then two charging ticks, then READY again (one shot only, tick 1)
    auto charging = events_of(m, "TURRET_CHARGING");
    REQUIRE(charging.size() == 4);   // round 1: 1, 2; round 3 ticks 2 and 3: 1, 2
    CHECK(charging[2]->round == 3);
    CHECK(charging[2]->tick == 2);
    CHECK(pi(*charging[2], "charge") == 1);
    CHECK(charging[3]->tick == 3);
    CHECK(pi(*charging[3], "charge") == 2);
    CHECK(m.state().turrets[0].charge == 2);
    CHECK(events_of(m, "TURRET_FIRED").size() == 1);   // P1 still in range on ticks 2-3, but it was recharging
}

TEST_CASE("G2-B turret cycle: a target that stays in range is hit once per recharge (every third tick)") {
    Match m = setup(tw_map(), 2, {{5, 3}, {10, 1}}, Tur{2, 0});   // READY, P1 adjacent
    play_round(m);   // fires tick 1, recharges ticks 2-3
    play_round(m);   // READY after tick 3, so it fires again on tick 1
    auto fired = events_of(m, "TURRET_FIRED");
    REQUIRE(fired.size() == 2);
    CHECK(fired[0]->round == 1);
    CHECK(fired[0]->tick == 1);
    CHECK(fired[1]->round == 2);
    CHECK(fired[1]->tick == 1);
}

TEST_CASE("G2-B turret: a fresh turret fires on the first tick it is READY (tick 3), once, for 1 damage") {
    Match m = setup(tw_map(), 2, {{5, 3}, {10, 1}});   // P1 one tile north of the turret
    play_round(m);
    auto fired = events_of(m, "TURRET_FIRED");
    REQUIRE(fired.size() == 1);
    CHECK(fired[0]->tick == 3);
    CHECK(pi(*fired[0], "player") == 1);
    CHECK(ps(*fired[0], "dir") == "N");
    CHECK(pi(*fired[0], "distance") == 1);
    CHECK(pi(*fired[0], "damage") == 1);
    CHECK(m.state().robot(1).damage_taken == 1);
}

TEST_CASE("G2-B turret targeting: nearest wins; range is 2 along N/E/S/W only; ties go N, E, S, W") {
    auto first_target = [](std::initializer_list<Seat> seats, int players) -> int {
        Match m = setup(tw_map(), players, seats, Tur{2, 0});
        play_round(m);
        auto fired = events_of(m, "TURRET_FIRED");
        return fired.empty() ? 0 : pi(*fired[0], "player");
    };
    CHECK(first_target({{5, 3}, {7, 4}}, 2) == 1);   // N1 beats E2
    CHECK(first_target({{5, 2}, {6, 4}}, 2) == 2);   // N2 loses to E1
    CHECK(first_target({{5, 3}, {6, 4}}, 2) == 1);   // N1 vs E1: north first
    CHECK(first_target({{5, 5}, {4, 4}}, 2) == 1);   // S1 vs W1: south first
    CHECK(first_target({{6, 4}, {5, 5}}, 2) == 1);   // E1 vs S1: east first
    CHECK(first_target({{5, 1}, {1, 1}}, 2) == 0);   // distance 3
    CHECK(first_target({{6, 3}, {6, 5}}, 2) == 0);   // diagonals are never targets
    CHECK(first_target({{3, 4}, {10, 1}}, 2) == 1);  // W2 is in range
}

TEST_CASE("G2-B turret targeting: a wall blocks the line of sight") {
    // Wall at (6,4): the robot at (7,4) is distance 2 east but hidden behind it.
    auto walled = load(replace_once(read_map_resource("turretworks-01.map"), "Y1  .   .   .   P1", "Y1  #   .   .   P1"));
    Match m = setup(walled, 2, {{7, 4}, {10, 1}}, Tur{2, 0});
    play_round(m);
    CHECK(events_of(m, "TURRET_FIRED").empty());
    CHECK(m.state().turrets[0].charge == 2);   // READY, held back by the wall
    // and the wall at (5,6) shortens the southern shot to the single tile (5,5), as designed
    auto plain = tw_map();
    CHECK(plain->blocked({5, 6}));
}

TEST_CASE("G2-B turret scoring: an owned hit on an opponent is +1; a disabling hit is +4 in total; crate drops") {
    Match m = charged(2, {{10, 1}, {5, 5, 2, 1}}, /*owner*/ 1);   // P2 at damage 2 carrying a crate, in range south
    play_round(m);
    auto fired = events_of(m, "TURRET_FIRED");
    REQUIRE(fired.size() == 1);
    CHECK(pi(*fired[0], "owner") == 1);
    CHECK(pi(*fired[0], "points") == 1);
    auto scores = events_of(m, "SCORE_AWARDED");
    REQUIRE(scores.size() == 2);
    CHECK(ps(*scores[0], "reason") == "TURRET_DAMAGE");
    CHECK(pi(*scores[0], "points") == 1);
    CHECK(pi(*scores[0], "turret") == 1);
    CHECK(pi(*scores[0], "victim") == 2);
    CHECK(ps(*scores[1], "reason") == "TURRET_DISABLE");
    CHECK(pi(*scores[1], "points") == 3);
    auto dis = events_of(m, "ROBOT_DISABLED");
    REQUIRE(dis.size() == 1);
    CHECK(ps(*dis[0], "cause") == "TURRET");
    CHECK(pi(*dis[0], "turret") == 1);
    CHECK(pi(*dis[0], "by") == 1);
    CHECK(m.state().robot(1).sabotage == 4);
    CHECK(m.state().robot(1).score == 4);
    CHECK(m.state().robot(2).score == 0);
    auto drops = events_of(m, "CRATE_DROPPED");
    REQUIRE(drops.size() == 1);
    CHECK(pi(*drops[0], "x") == 5);
    CHECK(pi(*drops[0], "y") == 5);
}

TEST_CASE("G2-B turret scoring: damage to the owner or from a neutral turret scores nothing") {
    {   // owner stands in range: hit, no points, and a self-disable gives no bonus
        Match m = charged(2, {{5, 5, 2}, {10, 1}}, /*owner*/ 1);
        play_round(m);
        REQUIRE(events_of(m, "TURRET_FIRED").size() == 1);
        CHECK(pi(*events_of(m, "TURRET_FIRED")[0], "points") == 0);
        CHECK(events_of(m, "SCORE_AWARDED").empty());
        CHECK(m.state().robot(1).sabotage == 0);
        auto dis = events_of(m, "ROBOT_DISABLED");
        REQUIRE(dis.size() == 1);
        CHECK(pi(*dis[0], "by") == 1);
    }
    {   // neutral turret hits and disables an opponent: nobody scores
        Match m = charged(2, {{10, 1}, {5, 5, 2}}, /*owner*/ 0);
        play_round(m);
        REQUIRE(events_of(m, "TURRET_FIRED").size() == 1);
        CHECK(events_of(m, "SCORE_AWARDED").empty());
        CHECK(pi(*events_of(m, "ROBOT_DISABLED")[0], "by") == 0);
        CHECK(m.state().robot(1).score == 0);
        CHECK(m.state().robot(2).score == 0);
    }
}

TEST_CASE("G2-B turret ownership: entering a pad claims; standing still, or an owner already on a pad, changes nothing") {
    // P1 enters the left pad at tick 1; P2 enters the right pad at tick 2 and takes the turret; charge never resets.
    Match m = setup(tw_map(), 2, {{1, 4}, {10, 4}});
    play_round(m, {{"FORWARD_1", "WAIT", "WAIT"}, {"WAIT", "FORWARD_1", "WAIT"}});
    auto cap = events_of(m, "TURRET_CAPTURED");
    REQUIRE(cap.size() == 2);
    CHECK(pi(*cap[0], "player") == 1);
    CHECK(pi(*cap[0], "previous_owner") == 0);
    CHECK(cap[0]->tick == 1);
    CHECK(pi(*cap[1], "player") == 2);
    CHECK(pi(*cap[1], "previous_owner") == 1);
    CHECK(cap[1]->tick == 2);
    CHECK(m.state().turrets[0].owner == 2);
    auto charging = events_of(m, "TURRET_CHARGING");
    REQUIRE(charging.size() == 2);   // 1 then 2: capturing did not pause or reset it
    CHECK(pi(*charging[0], "charge") == 1);
    CHECK(pi(*charging[1], "charge") == 2);
    CHECK(m.state().turrets[0].charge == 2);   // READY with its new owner, nobody in range
    // another round: P1 and P2 both just stand on pads -> no new capture
    play_round(m);
    CHECK(events_of(m, "TURRET_CAPTURED").size() == 2);
}

TEST_CASE("G2-B turret ownership: a robot starting the match on a pad does not claim it; two entrants - pad order decides") {
    Match still = setup(tw_map(), 2, {{2, 4}, {10, 1}});
    play_round(still);
    CHECK(events_of(still, "TURRET_CAPTURED").empty());
    CHECK(still.state().turrets[0].owner == 0);
    // both enter on the same tick: the left pad (first in scan order) wins
    Match both = setup(tw_map(), 2, {{1, 4}, {10, 4}});
    play_round(both, {{"FORWARD_1"}, {"FORWARD_1"}});
    auto cap = events_of(both, "TURRET_CAPTURED");
    REQUIRE(cap.size() == 1);
    CHECK(pi(*cap[0], "player") == 1);
    // and a capture applies to that very tick's shot: P2 steps on while the turret is about to fire
    Match late = setup(tw_map(), 2, {{5, 3}, {10, 4}}, Tur{2, 0});
    play_round(late, {{}, {"FORWARD_1"}});   // P2 -> (9,4) pad, claims, then Y1 fires at P1 (north, distance 1)
    auto fired = events_of(late, "TURRET_FIRED");
    REQUIRE(fired.size() == 1);
    CHECK(pi(*fired[0], "owner") == 2);
    CHECK(late.state().robot(2).sabotage == 1);
}

TEST_CASE("G2-B turret ordering: a robot felled by a laser this tick is not a turret target (one source per disable)") {
    // L> at (2,5) lights row 5 east of it, including (5,5), which Y1 covers to the south.
    auto both = load(replace_once(read_map_resource("turretworks-01.map"), "#   .   .   .   .   .   .   .   .   .   .   #",
                                  "#   .   L>  .   .   .   .   .   .   .   .   #"));
    Match m = setup(both, 2, {{10, 1}, {5, 5, 2}}, Tur{2, 1});   // P1 owns the turret; P2 has damage 2
    play_round(m);
    CHECK(events_of(m, "TURRET_FIRED").empty());
    auto dis = events_of(m, "ROBOT_DISABLED");
    REQUIRE(dis.size() == 1);
    CHECK(ps(*dis[0], "cause") == "LASER");
    CHECK(m.state().robot(1).sabotage == 0);
    CHECK(m.state().robot(2).damage_taken == 3);
}

TEST_CASE("G2-B turret state survives save/restore and validation rejects impossible turret state") {
    Match m = setup(tw_map(), 2, {{1, 1}, {10, 1}}, Tur{1, 2});
    Json saved = m.save();
    std::string err;
    MatchConfig c{"g2b", tw_map(), 2};
    auto back = Match::restore(c, saved, err);
    REQUIRE_MESSAGE(back.has_value(), err);
    CHECK(back->state().turrets[0].charge == 1);
    CHECK(back->state().turrets[0].owner == 2);
    CHECK(back->state() == m.state());
    auto bad = [&](auto edit) {
        Json j = m.save();
        edit(const_cast<Json&>(j.find("turrets")->items()[0]));
        std::string e;
        return Match::restore(c, j, e).has_value();
    };
    CHECK_FALSE(bad([](Json& t) { t.set("charge", 3); }));
    CHECK_FALSE(bad([](Json& t) { t.set("owner", 3); }));
    CHECK_FALSE(bad([](Json& t) { t.set("id", 2); }));
}

TEST_CASE("G2-B playback: charge, capture, fire, score, disable and drop replay to the authoritative board") {
    Match m = setup(tw_map(), 2, {{1, 4}, {5, 5, 2, 1}}, Tur{0, 0});
    const ui::BoardView before = ui::read_board(m.snapshot());
    const int version = m.state_version();
    const int round = m.state().round;
    m.submit_program(1, round, 1, {"FORWARD_1", "WAIT", "WAIT"}, true, "");   // claims at tick 1
    m.submit_program(2, round, 1, {"WAIT", "WAIT", "WAIT"}, true, "");
    m.run_round();   // tick 3: P2 (damage 2, south of Y1) is hit and disabled by P1's turret
    const auto evs = m.events_since(version);
    REQUIRE(evs.has_value());
    Json events = Json::array();
    for (const auto& e : *evs) events.push(event_to_json(e, "g2b"));

    ui::Playback pb;
    pb.set_speed(8);
    pb.start(before, events);
    int captures = 0, charges = 0, fires = 0, scores = 0, disables = 0, drops = 0;
    std::size_t last = static_cast<std::size_t>(-1);
    while (pb.active()) {
        const ui::Step* st = pb.current();
        if (pb.step_index() != last) {
            last = pb.step_index();
            captures += st->kind == ui::Step::TurretCapture;
            charges += st->kind == ui::Step::TurretCharge;
            fires += st->kind == ui::Step::TurretFire;
            scores += st->kind == ui::Step::Score && st->turret;
            disables += st->kind == ui::Step::Disable;
            drops += st->kind == ui::Step::Drop;
        }
        pb.advance(50);
    }
    CHECK(captures == 1);
    CHECK(charges == 2);
    CHECK(fires == 1);
    CHECK(scores == 2);   // +1 damage, +3 disable
    CHECK(disables == 1);
    CHECK(drops == 1);
    const ui::BoardView after = ui::read_board(m.snapshot());
    CHECK_MESSAGE(pb.board().equals(after), pb.board().diff(after));
    CHECK(after.turret(1)->owner == 1);
}

TEST_CASE("G2-B: bots play turretworks-01 to the end deterministically with 2-4 players") {
    for (int players = 2; players <= 4; ++players) {
        sim::SimOptions o;
        o.players = players;
        o.seed = 5;
        o.map_id = "turretworks-01";
        sim::SimResult a = sim::run_simulation(o), b = sim::run_simulation(o);
        REQUIRE_MESSAGE(a.ok, a.error);
        CHECK(a.rounds_played == 12);
        CHECK(a.state_hash == b.state_hash);
        CHECK(a.event_hash == b.event_hash);
    }
}

TEST_CASE("G2-B playback: a turret left READY across a round boundary replays to the authoritative board") {
    Match m = setup(tw_map(), 2, {{1, 1}, {10, 1}});
    const ui::BoardView before = ui::read_board(m.snapshot());
    const int version = m.state_version();
    play_round(m);
    play_round(m);
    const auto evs = m.events_since(version);
    REQUIRE(evs.has_value());
    Json events = Json::array();
    for (const auto& e : *evs) events.push(event_to_json(e, "g2b"));
    ui::Playback pb;
    pb.set_speed(8);
    pb.start(before, events);
    while (pb.active()) pb.advance(50);
    const ui::BoardView after = ui::read_board(m.snapshot());
    CHECK_MESSAGE(pb.board().equals(after), pb.board().diff(after));
    CHECK(after.turret(1)->charge == 2);
}

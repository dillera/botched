// G2-A: owned proximity traps (docs/botched-gameplay-milestones-g1-g4.md). Positions are set up by
// restoring a validated checkpoint, so every case starts from exactly the state it describes.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"
#include "map_fixture.hpp"
#include "ui_playback.hpp"

using namespace botched;

namespace {

// The trap X1 is at (4,4): its eight neighbours are x 3..5, y 3..5. Its console K1 is at (1,6).
// Lv at (3,1) fires down column 3 and so crosses the trap's neighbourhood at (3,3), (3,4), (3,5).
const char* kTrapMap =
    "# # # # # # # #\n"
    "# S1 D1 Lv . D2 S2 #\n"
    "# . . . . . . #\n"
    "# G1 . . . . G2 #\n"
    "# . . . X1 . . #\n"
    "# . . . . . . #\n"
    "# K1 . . . . . #\n"
    "# S3 D3 . . D4 S4 #\n"
    "# # # # # # # #\n";

std::shared_ptr<const Map> trap_map() {
    MapLoad l = load_map("trap-test", kTrapMap);
    REQUIRE_MESSAGE(l.ok(), (l.errors.empty() ? std::string("?") : l.errors[0]));
    return std::make_shared<const Map>(std::move(*l.map));
}

struct Seat { int x, y; int damage = 0; int sabotage = 0; };
struct TrapSetup { bool armed = true; int owner = 0; };

MatchConfig cfg(int players) {
    MatchConfig c;
    c.match_id = "g2a";
    c.map = trap_map();
    c.player_count = players;
    return c;
}

Match setup(int players, std::initializer_list<Seat> seats, TrapSetup trap = {}) {
    MatchConfig c = cfg(players);
    Match fresh(c);
    Json saved = fresh.save();
    Json& ps = const_cast<Json&>(*saved.find("players"));
    int i = 0;
    for (const Seat& s : seats) {
        Json& p = const_cast<Json&>(ps.items()[static_cast<std::size_t>(i++)]);
        p.set("x", s.x);
        p.set("y", s.y);
        p.set("damage", s.damage);
        p.set("damage_taken", s.damage);
        p.set("sabotage", s.sabotage);
        p.set("score", s.sabotage);
    }
    Json& ts = const_cast<Json&>(*saved.find("traps"));
    Json& t = const_cast<Json&>(ts.items()[0]);
    t.set("armed", trap.armed);
    t.set("owner", trap.owner ? Json(trap.owner) : Json(nullptr));
    std::string err;
    auto m = Match::restore(c, saved, err);
    REQUIRE_MESSAGE(m.has_value(), err);
    return std::move(*m);
}

// Runs one round: `orders[p-1]` for tick 1 (WAIT elsewhere).
void run_round(Match& m, std::vector<std::string> first_orders = {}) {
    const int round = m.state().round;
    for (int p = 1; p <= m.state().player_count(); ++p) {
        std::string o = p <= static_cast<int>(first_orders.size()) ? first_orders[static_cast<std::size_t>(p - 1)] : "WAIT";
        m.submit_program(p, round, 1, {o, "WAIT", "WAIT"}, true, "");
    }
    m.run_round();
}

// Tick 1 only: a disabled robot has not respawned yet, so state after it shows the disable.
void run_tick1(Match& m, std::vector<std::string> first_orders = {}) {
    const int round = m.state().round;
    for (int p = 1; p <= m.state().player_count(); ++p) {
        std::string o = p <= static_cast<int>(first_orders.size()) ? first_orders[static_cast<std::size_t>(p - 1)] : "WAIT";
        m.submit_program(p, round, 1, {o, "WAIT", "WAIT"}, true, "");
    }
    REQUIRE(m.close_planning());
    REQUIRE(m.run_next_tick());
}

std::vector<const Event*> events_of(const Match& m, const std::string& type) {
    std::vector<const Event*> v;
    for (const auto& e : m.all_events()) if (e.type == type) v.push_back(&e);
    return v;
}
int pi(const Event& e, const char* k) { return static_cast<int>(e.params.get_int(k).value_or(-1)); }

}  // namespace

TEST_CASE("G2-A map: X<n> and K<n> parse, round-trip and validate") {
    auto m = trap_map();
    REQUIRE(m->traps.size() == 1);
    CHECK(m->traps[0].id == 1);
    CHECK((m->traps[0].pos == Pos{4, 4}));
    REQUIRE(m->trap_consoles.size() == 1);
    CHECK((m->trap_consoles[0].pos == Pos{1, 6}));
    CHECK(m->blocked({4, 4}));                   // the device itself is solid
    CHECK_FALSE(m->blocked({1, 6}));             // the console is walkable like a belt console
    MapLoad back = parse_map("again", map_to_text(*m));
    REQUIRE(back.ok());
    CHECK(map_to_text(*back.map) == map_to_text(*m));
    MapLoad viaJson = map_from_json(map_to_json(*m));
    REQUIRE(viaJson.ok());
    CHECK(map_to_text(*viaJson.map) == map_to_text(*m));

    auto errors = [](std::string text) {
        MapLoad l = load_map("t", text);
        return l.errors;
    };
    std::string base = kTrapMap;
    auto replace = [](std::string s, const std::string& a, const std::string& b) { s.replace(s.find(a), a.size(), b); return s; };
    CHECK_FALSE(errors(replace(base, "K1", ".")).empty());              // trap with no console
    CHECK_FALSE(errors(replace(base, "# . . . . . . #\n# K1", "# . . . . K2 . #\n# K1")).empty());  // console for a missing trap
    CHECK_FALSE(errors(replace(base, "# . . . . . . #\n# K1", "# . . . X1 . . #\n# K1")).empty());  // duplicate trap id
    CHECK_FALSE(parse_map("t", replace(base, "X1", "X0")).ok());
    CHECK_FALSE(parse_map("t", replace(base, "X1", "X12")).ok());
}

TEST_CASE("G2-A: a robot walking into the trap device is blocked like a wall") {
    Match m = setup(2, {{4, 3}, {6, 6}});
    m.submit_program(1, 1, 1, {"TURN_RIGHT", "FORWARD_1", "WAIT"}, true, "");  // P1 faces E; turn to S, then try to go into (4,4)
    m.submit_program(2, 1, 1, {"WAIT", "WAIT", "WAIT"}, true, "");
    m.run_round();
    CHECK_FALSE(events_of(m, "MOVE_BLOCKED").empty());
}

TEST_CASE("G2-A: an unoccupied trap stays armed and nothing outside the 8 tiles is hurt") {
    Match m = setup(2, {{2, 2}, {6, 6}});          // both at distance >= 2 from (4,4)
    run_round(m);
    CHECK(events_of(m, "TRAP_TRIGGERED").empty());
    CHECK(m.state().trap(1)->armed);
    CHECK(m.state().robot(1).damage == 0);
    CHECK(m.state().robot(2).damage == 0);
}

TEST_CASE("G2-A: a neutral trap hurts whoever is in range, once, and scores nothing") {
    Match m = setup(2, {{5, 3}, {6, 6}});          // P1 diagonal to the trap
    run_round(m);
    auto trig = events_of(m, "TRAP_TRIGGERED");
    REQUIRE(trig.size() == 1);                     // fires on tick 1 only; it is disarmed for ticks 2 and 3
    CHECK(trig[0]->tick == 1);
    CHECK(pi(*trig[0], "owner") == 0);
    CHECK_FALSE(m.state().trap(1)->armed);
    CHECK(m.state().robot(1).damage == 1);
    CHECK(m.state().robot(1).damage_taken == 1);
    CHECK(m.state().robot(2).damage == 0);
    CHECK(events_of(m, "SCORE_AWARDED").empty());
    CHECK(m.state().robot(1).score == 0);
    auto hit = events_of(m, "TRAP_HIT");
    REQUIRE(hit.size() == 1);
    CHECK(pi(*hit[0], "points") == 0);
}

TEST_CASE("G2-A: an owned trap damages every robot in range at once; opponents score for the owner, self-damage does not") {
    Match m = setup(3, {{5, 4}, {5, 5}, {4, 3}}, {true, 1});   // P1 (the owner) and two opponents all in range
    run_tick1(m);
    auto trig = events_of(m, "TRAP_TRIGGERED");
    REQUIRE(trig.size() == 1);
    CHECK(trig[0]->params.find("victims")->items().size() == 3);
    for (int p = 1; p <= 3; ++p) CHECK(m.state().robot(p).damage == 1);
    CHECK(m.state().robot(1).score == 2);          // +1 for each damaged opponent, nothing for itself
    CHECK(m.state().robot(1).sabotage == 2);
    CHECK(m.state().robot(1).deliveries == 0);
    CHECK(m.state().robot(2).score == 0);
    CHECK(m.state().robot(3).score == 0);
    CHECK(events_of(m, "SCORE_AWARDED").size() == 2);
}

TEST_CASE("G2-A: a disabling hit earns +1 and the +3 bonus exactly once") {
    Match m = setup(3, {{6, 6}, {5, 4, 2}, {4, 3, 0}}, {true, 1});  // P2 is at damage 2, P3 healthy; both in range
    run_tick1(m);
    CHECK(m.state().robot(2).respawn_pending);
    auto dis = events_of(m, "ROBOT_DISABLED");
    REQUIRE(dis.size() == 1);
    CHECK(dis[0]->params.get_string("cause").value_or("") == "TRAP");
    CHECK(pi(*dis[0], "by") == 1);
    CHECK(pi(*dis[0], "trap") == 1);
    CHECK(m.state().robot(1).score == 1 + 1 + 3);  // P2 damage, P3 damage, P2 disable bonus
    int bonuses = 0;
    for (const Event* e : events_of(m, "SCORE_AWARDED"))
        if (e->params.get_string("reason").value_or("") == "TRAP_DISABLE") ++bonuses;
    CHECK(bonuses == 1);
}

TEST_CASE("G2-A: self-disable and neutral disable score nothing") {
    {   // the owner walks into its own trap at damage 2 and is disabled by it
        Match m = setup(2, {{5, 4, 2}, {6, 6}}, {true, 1});
        run_tick1(m);
        CHECK(m.state().robot(1).respawn_pending);
        CHECK(m.state().robot(1).score == 0);
        CHECK(events_of(m, "SCORE_AWARDED").empty());
    }
    {   // a neutral trap
        Match m = setup(2, {{5, 4, 2}, {6, 6}}, {true, 0});
        run_tick1(m);
        CHECK(m.state().robot(1).respawn_pending);
        CHECK(m.state().robot(2).score == 0);
        CHECK(events_of(m, "SCORE_AWARDED").empty());
    }
}

TEST_CASE("G2-A: a robot already felled by a laser this tick takes no trap damage and awards no bonus") {
    // (3,3) is in the laser's column and in the trap's range. Damage 2 + laser = 3; the trap then skips it.
    Match m = setup(2, {{6, 6}, {3, 3, 2}}, {true, 1});
    run_tick1(m);
    auto dis = events_of(m, "ROBOT_DISABLED");
    REQUIRE(dis.size() == 1);
    CHECK(dis[0]->params.get_string("cause").value_or("") == "LASER");
    CHECK(m.state().robot(1).score == 0);
    CHECK(events_of(m, "TRAP_HIT").empty());
    CHECK(events_of(m, "SCORE_AWARDED").empty());
    CHECK_FALSE(m.state().trap(1)->armed);  // it did fire (the robot was in range) and disarmed
}

TEST_CASE("G2-A: TOGGLE at the console arms and claims the trap; the same tick's machinery phase then judges it") {
    // P1 stands at (1,5) next to K1 and arms. P2 is in range, so the freshly owned trap fires at once.
    Match m = setup(2, {{1, 5}, {5, 5}}, {true, 0});
    run_tick1(m, {"TOGGLE"});
    auto armed = events_of(m, "TRAP_ARMED");
    REQUIRE(armed.size() == 1);
    CHECK(pi(*armed[0], "player") == 1);
    CHECK(armed[0]->params.get_bool("was_armed").value_or(false));  // re-arming an armed trap: ownership only
    CHECK(pi(*armed[0], "previous_owner") == 0);
    CHECK(m.state().trap(1)->owner == 1);
    // the claim came before the burst in the event stream, and P2 paid for it
    std::vector<std::string> order;
    for (const auto& e : m.all_events())
        if (e.type == "TRAP_ARMED" || e.type == "TRAP_TRIGGERED") order.push_back(e.type);
    REQUIRE(order.size() == 2);
    CHECK(order[0] == "TRAP_ARMED");
    CHECK(m.state().robot(2).damage == 1);
    CHECK(m.state().robot(1).score == 1);
}

TEST_CASE("G2-A: re-arming a disarmed trap transfers it; an idle re-arm does not fire it") {
    Match m = setup(2, {{1, 5}, {6, 6}}, {false, 2});   // disarmed, owned by P2
    run_round(m, {"TOGGLE"});
    auto armed = events_of(m, "TRAP_ARMED");
    REQUIRE(armed.size() == 1);
    CHECK_FALSE(armed[0]->params.get_bool("was_armed").value_or(true));
    CHECK(pi(*armed[0], "previous_owner") == 2);
    CHECK(m.state().trap(1)->armed);
    CHECK(m.state().trap(1)->owner == 1);
    CHECK(events_of(m, "TRAP_TRIGGERED").empty());      // nobody in range: stays armed

    Match n = setup(2, {{1, 5}, {6, 6}}, {true, 2});    // already armed by P2: ownership moves, nothing fires
    run_round(n, {"TOGGLE"});
    CHECK(n.state().trap(1)->owner == 1);
    CHECK(n.state().trap(1)->armed);
    CHECK(events_of(n, "TRAP_TRIGGERED").empty());
}

TEST_CASE("G2-A: TOGGLE away from every console still reports NO_CONSOLE") {
    Match m = setup(2, {{2, 2}, {6, 6}});
    run_round(m, {"TOGGLE"});
    auto no = events_of(m, "ORDER_NO_OP");
    REQUIRE(no.size() >= 1);
    CHECK(no[0]->params.get_string("reason").value_or("") == "NO_CONSOLE");
    CHECK(events_of(m, "TRAP_ARMED").empty());
}

TEST_CASE("G2-A: trap state and sabotage points survive snapshot, checkpoint and replay deterministically") {
    auto run = [] {
        Match m = setup(3, {{5, 4}, {5, 5}, {4, 3, 1}}, {true, 1});
        run_tick1(m);
        return m;
    };
    Match a = run(), b = run();
    CHECK(a.state_hash() == b.state_hash());
    const Json snap = a.snapshot();
    REQUIRE(snap.find("traps"));
    CHECK(snap.find("traps")->items()[0].get_bool("armed").value_or(true) == false);
    CHECK(snap.find("players")->items()[0].get_int("sabotage").value_or(-1) == 2);

    std::string err;
    auto restored = Match::restore(cfg(3), a.save(), err);
    REQUIRE_MESSAGE(restored.has_value(), err);
    CHECK(restored->state_hash() == a.state_hash());

    // Scores must add up: 5 x deliveries + sabotage.
    Json bad = a.save();
    const_cast<Json&>(bad.find("players")->items()[0]).set("score", 1);
    CHECK_FALSE(Match::restore(cfg(3), bad, err).has_value());
}

TEST_CASE("G2-A: maps without traps are unchanged (no traps or sabotage in snapshots)") {
    MatchConfig c;
    c.match_id = "plain";
    c.map = std::make_shared<const Map>(training01());
    c.player_count = 2;
    Match m(c);
    CHECK(m.snapshot().find("traps") == nullptr);
    CHECK(m.snapshot().find("players")->items()[0].find("sabotage") == nullptr);
}

TEST_CASE("G2-A: sabotage points count toward a score target") {
    MatchConfig c = cfg(2);
    c.target_score = 1;
    Match fresh(c);
    Json saved = fresh.save();
    Json& ps = const_cast<Json&>(*saved.find("players"));
    const_cast<Json&>(ps.items()[1]).set("x", 5);
    const_cast<Json&>(ps.items()[1]).set("y", 4);
    Json& t = const_cast<Json&>(saved.find("traps")->items()[0]);
    t.set("owner", 1);
    std::string err;
    auto m = Match::restore(c, saved, err);
    REQUIRE_MESSAGE(m.has_value(), err);
    run_round(*m);
    CHECK(m->finished());                                  // P1 reached 1 point from sabotage alone
    REQUIRE(m->status().find("winners")->items().size() == 1);
    CHECK(m->status().find("winners")->items()[0].as_int() == 1);
}

TEST_CASE("G2-A playback: the replay of arm, burst, hits, scores and a trap disable ends on the authoritative board") {
    // P1 arms the trap on tick 1; P2 (damage 2) and P3 are in range, so the burst disables P2 and scores 1+1+3 for P1.
    Match m = setup(3, {{1, 5}, {5, 4, 2}, {4, 3}}, {true, 0});
    const ui::BoardView before = ui::read_board(m.snapshot());
    const int version = m.state_version();
    run_round(m, {"TOGGLE"});
    Json events = Json::array();
    const auto evs = m.events_since(version);
    REQUIRE(evs.has_value());
    for (const auto& e : *evs) events.push(event_to_json(e, "g2a"));

    ui::Playback pb;
    pb.set_speed(8);
    pb.start(before, events);
    bool saw_arm = false, saw_burst = false, saw_score = false;
    int hits = 0;
    std::size_t last_hit_step = static_cast<std::size_t>(-1);
    while (pb.active()) {
        const ui::Step* st = pb.current();
        saw_arm |= st->kind == ui::Step::TrapArm;
        saw_burst |= st->kind == ui::Step::TrapBurst && st->victims.size() == 2 && st->trap == 1 && st->owner == 1;
        saw_score |= st->kind == ui::Step::Score && st->points > 0;
        if (st->kind == ui::Step::TrapHit && pb.step_index() != last_hit_step) { ++hits; last_hit_step = pb.step_index(); }
        pb.advance(50);
    }
    CHECK(saw_arm);
    CHECK(saw_burst);
    CHECK(saw_score);
    CHECK(hits == 2);   // P1 stands at the console, out of range: only P2 and P3 are caught
    const ui::BoardView after = ui::read_board(m.snapshot());
    CHECK_MESSAGE(pb.board().equals(after), pb.board().diff(after));
    CHECK(after.robot(1)->sabotage == 5);
}

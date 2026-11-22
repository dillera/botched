// G2-C: tar. A generator (token A) makes the eight tiles around it tar: no damage, no points, no state.
// tarworks-01: generator at (5,4); tar tiles are x 4-6, y 3-5 except (5,4). Dispenser G1 (5,3) is inside it.
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
    MapLoad l = load_map("tarworks-01", text);
    REQUIRE_MESSAGE(l.ok(), (l.errors.empty() ? std::string("?") : l.errors[0]));
    return std::make_shared<const Map>(std::move(*l.map));
}
MapPtr tar_map() { return load(read_map_resource("tarworks-01.map")); }

struct Seat { int x, y; const char* heading = "E"; };

Match setup(const MapPtr& map, int players, std::initializer_list<Seat> seats) {
    MatchConfig c;
    c.match_id = "g2c";
    c.map = map;
    c.player_count = players;
    Match fresh(c);
    Json saved = fresh.save();
    Json& ps = const_cast<Json&>(*saved.find("players"));
    int i = 0;
    for (const Seat& s : seats) {
        Json& p = const_cast<Json&>(ps.items()[static_cast<std::size_t>(i++)]);
        p.set("x", s.x);
        p.set("y", s.y);
        p.set("heading", s.heading);
    }
    std::string err;
    auto m = Match::restore(c, saved, err);
    REQUIRE_MESSAGE(m.has_value(), err);
    return std::move(*m);
}

using Programs = std::vector<std::vector<std::string>>;

void play_round(Match& m, Programs progs = {}) {
    const int round = m.state().round;
    for (int p = 1; p <= m.state().player_count(); ++p) {
        std::vector<std::string> pr = static_cast<std::size_t>(p) <= progs.size() ? progs[static_cast<std::size_t>(p - 1)] : std::vector<std::string>{};
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
std::string ps(const Event& e, const char* k) { return e.params.get_string(k).value_or(""); }
bool at(const Match& m, int p, int x, int y) { return m.state().robot(p).pos == Pos{x, y}; }

}  // namespace

TEST_CASE("G2-C map: tarworks-01 loads, validates for 2-4 players; the generator blocks and its 8 neighbours are tar") {
    auto m = tar_map();
    REQUIRE(m->tar_generators.size() == 1);
    CHECK((m->tar_generators[0] == Pos{5, 4}));
    CHECK(m->blocked({5, 4}));
    for (int players = 2; players <= 4; ++players) {
        ValidationOptions o;
        o.player_count = players;
        CHECK(validate_map(*m, o).empty());
    }
    int tar = 0;
    for (int y = 0; y < m->height; ++y)
        for (int x = 0; x < m->width; ++x) tar += m->in_tar({x, y});
    CHECK(tar == 8);   // exactly the eight neighbours; the generator's own tile is not tar
    CHECK_FALSE(m->in_tar({3, 4}));
    CHECK_FALSE(m->in_tar({5, 2}));
    CHECK(m->in_tar({4, 3}));
    CHECK(m->in_tar({6, 5}));
    MapLoad again = load_map("tarworks-01", map_to_text(*m));
    REQUIRE(again.ok());
    CHECK(again.map->tar_generators.size() == 1);
    CHECK_FALSE(load_map("m", "#   #   #\n#   A1  #\n#   #   #\n").ok());   // A takes no id
}

TEST_CASE("G2-C tar: FORWARD_1 and BACK_1 cannot move a robot that starts in tar; entering tar from outside is allowed") {
    Match m = setup(tar_map(), 2, {{4, 3, "N"}, {3, 5, "E"}});   // P1 in tar facing north, P2 outside facing the tar
    play_round(m, {{"FORWARD_1", "BACK_1", "WAIT"}, {"FORWARD_1", "WAIT", "WAIT"}});
    CHECK(at(m, 1, 4, 3));
    auto noop = events_of(m, "ORDER_NO_OP");
    int tar_noops = 0;
    for (const auto* e : noop) tar_noops += ps(*e, "reason") == "TAR" && e->params.get_int("player").value_or(0) == 1;
    CHECK(tar_noops == 2);
    CHECK(at(m, 2, 4, 5));   // P2 stepped into the tar with FORWARD_1
}

TEST_CASE("G2-C tar: FORWARD_2 stops after one tile once the robot stands in tar, checked after each substep") {
    {   // from outside, the first tile is already tar: second substep cancelled
        Match m = setup(tar_map(), 2, {{3, 5, "E"}, {10, 1}});
        play_round(m, {{"FORWARD_2"}});
        CHECK(at(m, 1, 4, 5));
        auto sl = events_of(m, "TAR_SLOWED");
        REQUIRE(sl.size() == 1);
        CHECK(sl[0]->params.get_int("x").value_or(-1) == 4);
    }
    {   // entering tar on the second substep: nothing left to cancel
        Match m = setup(tar_map(), 2, {{2, 5, "E"}, {10, 1}});
        play_round(m, {{"FORWARD_2"}});
        CHECK(at(m, 1, 4, 5));
        CHECK(events_of(m, "TAR_SLOWED").empty());
    }
    {   // starting in tar and staying in it: one tile
        Match m = setup(tar_map(), 2, {{4, 5, "E"}, {10, 1}});
        play_round(m, {{"FORWARD_2"}});
        CHECK(at(m, 1, 5, 5));
        CHECK(events_of(m, "TAR_SLOWED").size() == 1);
    }
    {   // starting in tar but leaving it with the first tile: tar is judged per substep, so the second tile is free
        Match m = setup(tar_map(), 2, {{6, 5, "E"}, {10, 1}});
        play_round(m, {{"FORWARD_2"}});
        CHECK(at(m, 1, 8, 5));
        CHECK(events_of(m, "TAR_SLOWED").empty());
    }
    {   // outside tar entirely: unchanged behaviour
        Match m = setup(tar_map(), 2, {{7, 5, "E"}, {10, 1}});
        play_round(m, {{"FORWARD_2"}});
        CHECK(at(m, 1, 9, 5));
    }
}

TEST_CASE("G2-C tar: rotations, DROP, TOGGLE and WAIT are unaffected") {
    Match m = setup(tar_map(), 2, {{4, 5, "E"}, {10, 1}});
    play_round(m, {{"TURN_LEFT", "TURN_AROUND", "WAIT"}});
    CHECK(m.state().robot(1).heading == Dir::S);
    CHECK(events_of(m, "ROBOT_ROTATED").size() == 2);
    for (const auto* e : events_of(m, "ORDER_NO_OP")) CHECK(ps(*e, "reason") != "TAR");
    Match d = setup(tar_map(), 2, {{4, 5, "E"}, {10, 1}});
    play_round(d, {{"DROP", "TOGGLE", "WAIT"}});
    auto noop = events_of(d, "ORDER_NO_OP");
    REQUIRE(noop.size() == 2);
    CHECK(ps(*noop[0], "reason") == "NOT_CARRYING");   // the usual reasons, not TAR
    CHECK(ps(*noop[1], "reason") == "NO_CONSOLE");
}

TEST_CASE("G2-C tar: a robot in tar can still push with an allowed FORWARD_2; being pushed is not slowed; FORWARD_1 cannot push") {
    Match m = setup(tar_map(), 2, {{4, 5, "E"}, {5, 5, "E"}});   // P2 in tar directly in front of P1
    play_round(m, {{"FORWARD_2"}});
    CHECK(at(m, 2, 6, 5));   // pushed one tile (still tar) - the push itself is not restricted
    CHECK(at(m, 1, 5, 5));   // the pusher moved one tile and is slowed
    CHECK(events_of(m, "TAR_SLOWED").size() == 1);
    Match f = setup(tar_map(), 2, {{4, 5, "E"}, {5, 5, "E"}});
    play_round(f, {{"FORWARD_1"}});
    CHECK(at(f, 1, 4, 5));   // cannot move at all, so no push
    CHECK(at(f, 2, 5, 5));
}

TEST_CASE("G2-C tar: conveyors carry robots through tar unaffected, and tar causes no damage or points") {
    std::string text = read_map_resource("tarworks-01.map");
    const std::string row = "#   .   .   .   .   .   .   .   .   .   .   #";
    const auto at_row = text.find(row);
    REQUIRE(at_row != std::string::npos);
    text.replace(at_row, row.size(), "#   T   .   .   .   .   C>  .   .   .   .   #");   // belt at (6,5), inside the tar area; console T at (1,5)
    Match m = setup(load(text), 2, {{6, 5, "E"}, {10, 1}});
    play_round(m, {{"WAIT", "WAIT", "WAIT"}});
    auto moved = events_of(m, "ROBOT_MOVED");
    REQUIRE_FALSE(moved.empty());
    CHECK(ps(*moved[0], "cause") == "CONVEYOR");
    CHECK(at(m, 1, 7, 5));   // carried out of the tar by the belt
    for (int p = 1; p <= 2; ++p) {
        CHECK(m.state().robot(p).damage == 0);
        CHECK(m.state().robot(p).score == 0);
    }
    CHECK(events_of(m, "SCORE_AWARDED").empty());
}

TEST_CASE("G2-C tar: tar adds no world state - snapshots carry nothing new and the event is replayed by SDL") {
    Match m = setup(tar_map(), 2, {{3, 5, "E"}, {4, 3, "N"}});
    CHECK(m.snapshot().find("tar") == nullptr);
    CHECK(m.snapshot().find("traps") == nullptr);
    CHECK(m.snapshot().find("turrets") == nullptr);
    const ui::BoardView before = ui::read_board(m.snapshot());
    const int version = m.state_version();
    play_round(m, {{"FORWARD_2", "WAIT", "WAIT"}, {"FORWARD_1", "BACK_1", "WAIT"}});
    const auto evs = m.events_since(version);
    REQUIRE(evs.has_value());
    Json events = Json::array();
    for (const auto& e : *evs) events.push(event_to_json(e, "g2c"));
    ui::Playback pb;
    pb.set_speed(8);
    pb.start(before, events);
    int slows = 0;
    std::size_t last = static_cast<std::size_t>(-1);
    while (pb.active()) {
        if (pb.step_index() != last) {
            last = pb.step_index();
            slows += pb.current()->kind == ui::Step::TarSlow;
        }
        pb.advance(50);
    }
    CHECK(slows == 3);   // P1's slowed FORWARD_2, plus P2's two stuck FORWARD_1 / BACK_1
    const ui::BoardView after = ui::read_board(m.snapshot());
    CHECK_MESSAGE(pb.board().equals(after), pb.board().diff(after));
}

TEST_CASE("G2-C: bots play tarworks-01 to the end deterministically with 2-4 players") {
    for (int players = 2; players <= 4; ++players) {
        sim::SimOptions o;
        o.players = players;
        o.seed = 5;
        o.map_id = "tarworks-01";
        sim::SimResult a = sim::run_simulation(o), b = sim::run_simulation(o);
        REQUIRE_MESSAGE(a.ok, a.error);
        CHECK(a.rounds_played == 12);
        CHECK(a.state_hash == b.state_hash);
        CHECK(a.event_hash == b.event_hash);
    }
}

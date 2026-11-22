// G2-D: SHUNT - +8 sabotage points for pushing an opponent into a pit, on shuntworks-01.
// Pits at (4,4), (6,4), (5,6). Dispenser G1 at (5,3) (it holds a loose crate at the start).
#include <memory>
#include <string>
#include <vector>

#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"
#include "map_fixture.hpp"
#include "sim_runner.hpp"
#include "ui_playback.hpp"

using namespace botched;

namespace {

using MapPtr = std::shared_ptr<const Map>;

MapPtr load(const std::string& text) {
    MapLoad l = load_map("shuntworks-01", text);
    REQUIRE_MESSAGE(l.ok(), (l.errors.empty() ? std::string("?") : l.errors[0]));
    return std::make_shared<const Map>(std::move(*l.map));
}
MapPtr sw_map() { return load(read_map_resource("shuntworks-01.map")); }

struct Seat { int x, y; const char* heading = "E"; int carrying = 0; };

int crate_base(const MapPtr& map) { return Match(MatchConfig{"probe", map, 2}).state().next_crate_id; }

MatchConfig cfg(const MapPtr& map, int players) {
    MatchConfig c;
    c.match_id = "g2d";
    c.map = map;
    c.player_count = players;
    return c;
}

Match setup(const MapPtr& map, int players, std::initializer_list<Seat> seats) {
    MatchConfig c = cfg(map, players);
    Json saved = Match(c).save();
    Json& ps = const_cast<Json&>(*saved.find("players"));
    Json& crates = const_cast<Json&>(*saved.find("crates"));
    const int base = crate_base(map);
    int max_id = 0, i = 0;
    for (const Seat& s : seats) {
        const int pid = i + 1;
        Json& p = const_cast<Json&>(ps.items()[static_cast<std::size_t>(i++)]);
        p.set("x", s.x);
        p.set("y", s.y);
        p.set("heading", s.heading);
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
    std::string err;
    auto m = Match::restore(c, saved, err);
    REQUIRE_MESSAGE(m.has_value(), err);
    return std::move(*m);
}

using Programs = std::vector<std::vector<std::string>>;

void submit(Match& m, const Programs& progs) {
    const int round = m.state().round;
    for (int p = 1; p <= m.state().player_count(); ++p) {
        std::vector<std::string> pr = static_cast<std::size_t>(p) <= progs.size() ? progs[static_cast<std::size_t>(p - 1)] : std::vector<std::string>{};
        while (pr.size() < 3) pr.push_back("WAIT");
        m.submit_program(p, round, 1, pr, true, "");
    }
}
void play_round(Match& m, const Programs& progs = {}) {
    submit(m, progs);
    m.run_round();
}

std::vector<const Event*> events_of(const Match& m, const std::string& type) {
    std::vector<const Event*> v;
    for (const auto& e : m.all_events()) if (e.type == type) v.push_back(&e);
    return v;
}
int pi(const Event& e, const char* k) { return static_cast<int>(e.params.get_int(k).value_or(-1)); }
std::string ps(const Event& e, const char* k) { return e.params.get_string(k).value_or(""); }
std::vector<const Event*> shunts(const Match& m) {
    std::vector<const Event*> v;
    for (const auto* e : events_of(m, "SCORE_AWARDED")) if (ps(*e, "reason") == "SHUNT") v.push_back(e);
    return v;
}

}  // namespace

TEST_CASE("G2-D map: shuntworks-01 loads and validates for 2-4 players with three pits") {
    auto m = sw_map();
    int pits = 0;
    for (const auto& t : m->tiles) pits += t.terrain == Terrain::Pit;
    CHECK(pits == 3);
    CHECK(m->tile({4, 4}).terrain == Terrain::Pit);
    CHECK(m->tile({6, 4}).terrain == Terrain::Pit);
    CHECK(m->tile({5, 6}).terrain == Terrain::Pit);
    for (int players = 2; players <= 4; ++players) {
        ValidationOptions o;
        o.player_count = players;
        CHECK(validate_map(*m, o).empty());
    }
}

TEST_CASE("G2-D SHUNT: a direct push into a pit scores +8 for the pusher; crate drop and respawn are unchanged") {
    Match m = setup(sw_map(), 2, {{2, 4, "E"}, {3, 4, "W", 1}});   // P2 carries a crate
    play_round(m, {{"FORWARD_1"}});
    auto sh = shunts(m);
    REQUIRE(sh.size() == 1);
    CHECK(pi(*sh[0], "player") == 1);
    CHECK(pi(*sh[0], "points") == 8);
    CHECK(pi(*sh[0], "victim") == 2);
    CHECK(pi(*sh[0], "score") == 8);
    CHECK(sh[0]->params.find("trap") == nullptr);   // not a trap or turret score
    auto dis = events_of(m, "ROBOT_DISABLED");
    REQUIRE(dis.size() == 1);
    CHECK(ps(*dis[0], "cause") == "PIT");
    CHECK(pi(*dis[0], "by") == 1);
    // existing pit behaviour: the crate drops on the last safe tile (where P2 stood: (3,4)), P2 respawns at round end
    auto drops = events_of(m, "CRATE_DROPPED");
    REQUIRE(drops.size() == 1);
    CHECK(pi(*drops[0], "x") == 3);
    CHECK(pi(*drops[0], "y") == 4);
    CHECK(events_of(m, "ROBOT_RESPAWNED").size() == 1);
    CHECK(m.state().robot(1).score == 8);
    CHECK(m.state().robot(1).sabotage == 8);
    CHECK(m.state().robot(1).deliveries == 0);
    CHECK(m.state().robot(2).score == 0);
    CHECK(m.state().robot(2).damage_taken == 0);   // a pit is not damage
}

TEST_CASE("G2-D SHUNT: a push down a chain of robots credits the robot that moved") {
    Match m = setup(sw_map(), 3, {{1, 4, "E"}, {2, 4, "E"}, {3, 4, "E"}});
    play_round(m, {{"FORWARD_1"}});
    auto sh = shunts(m);
    REQUIRE(sh.size() == 1);
    CHECK(pi(*sh[0], "player") == 1);
    CHECK(pi(*sh[0], "victim") == 3);   // the robot at the end of the chain falls
    CHECK(m.state().robot(1).sabotage == 8);
    CHECK(m.state().robot(2).score == 0);   // the middle robot is only shoved
    auto dis = events_of(m, "ROBOT_DISABLED");
    REQUIRE(dis.size() == 1);
    CHECK(pi(*dis[0], "player") == 3);
}

TEST_CASE("G2-D SHUNT: no points for walking into a pit, or for an unattributed fall") {
    Match m = setup(sw_map(), 2, {{3, 4, "E"}, {10, 1}});
    play_round(m, {{"FORWARD_1"}});
    CHECK(events_of(m, "ROBOT_DISABLED").size() == 1);
    CHECK(pi(*events_of(m, "ROBOT_DISABLED")[0], "player") == 1);
    CHECK(events_of(m, "ROBOT_DISABLED")[0]->params.find("by") == nullptr);
    CHECK(events_of(m, "SCORE_AWARDED").empty());
    CHECK(m.state().robot(1).score == 0);
    CHECK(m.state().robot(2).score == 0);
}

TEST_CASE("G2-D SHUNT: attribution persists through the round - a pushed robot that later walks into a pit pays the pusher") {
    // tick 1: P1 pushes P2 south out of (5,4); tick 2: P2 (heading S) walks into the pit at (5,6)
    Match m = setup(sw_map(), 2, {{5, 3, "S"}, {5, 4, "S"}});
    play_round(m, {{"FORWARD_1", "WAIT", "WAIT"}, {"WAIT", "FORWARD_1", "WAIT"}});
    auto sh = shunts(m);
    REQUIRE(sh.size() == 1);
    CHECK(pi(*sh[0], "player") == 1);
    CHECK(sh[0]->tick == 2);
    CHECK(m.state().robot(1).sabotage == 8);
    CHECK(pi(*events_of(m, "ROBOT_DISABLED")[0], "by") == 1);
}

TEST_CASE("G2-D SHUNT: a later push by another player transfers the attribution") {
    // tick 1: P1 pushes P2 south to (5,5); tick 2: P3 pushes P2 east to (6,5); tick 3: P2 (heading N) walks into (6,4)
    Match m = setup(sw_map(), 3, {{5, 3, "S"}, {5, 4, "N"}, {4, 5, "E"}});
    play_round(m, {{"FORWARD_1", "WAIT", "WAIT"}, {"WAIT", "WAIT", "FORWARD_1"}, {"WAIT", "FORWARD_1", "WAIT"}});
    auto sh = shunts(m);
    REQUIRE(sh.size() == 1);
    CHECK(pi(*sh[0], "player") == 3);
    CHECK(m.state().robot(3).sabotage == 8);
    CHECK(m.state().robot(1).sabotage == 0);
}

TEST_CASE("G2-D SHUNT: attribution is cleared at round end - no credit carries into the next round") {
    // round 1 tick 3: P1 pushes P2 south (no pit); round 2: P2 walks into the pit at (5,6)
    Match m = setup(sw_map(), 2, {{5, 3, "S"}, {5, 4, "S"}});
    play_round(m, {{"WAIT", "WAIT", "FORWARD_1"}});
    CHECK(m.state().robot(2).pushed_by == 0);
    CHECK(shunts(m).empty());
    play_round(m, {{}, {"FORWARD_1", "WAIT", "WAIT"}});
    CHECK(events_of(m, "ROBOT_DISABLED").size() == 1);
    CHECK(shunts(m).empty());
    CHECK(m.state().robot(1).score == 0);
}

TEST_CASE("G2-D SHUNT: a disable clears attribution, and each disabled opponent pays once even into the same pit") {
    // P1 shunts P2 into (4,4) at tick 1; P3 then shunts P4 into the same (now empty) pit in the same tick
    Match m = setup(sw_map(), 4, {{2, 4, "E"}, {3, 4, "E"}, {4, 6, "N"}, {4, 5, "N"}});
    play_round(m, {{"FORWARD_1"}, {}, {"FORWARD_1"}});
    auto sh = shunts(m);
    REQUIRE(sh.size() == 2);
    CHECK(pi(*sh[0], "player") == 1);
    CHECK(pi(*sh[0], "victim") == 2);
    CHECK(pi(*sh[1], "player") == 3);
    CHECK(pi(*sh[1], "victim") == 4);
    CHECK(m.state().robot(1).sabotage == 8);
    CHECK(m.state().robot(3).sabotage == 8);
    // after respawn at round end nobody carries attribution
    for (const auto& r : m.state().robots) CHECK(r.pushed_by == 0);
}

TEST_CASE("G2-D SHUNT: a belt that drives a robot into another can credit the pusher") {
    std::string text = read_map_resource("shuntworks-01.map");
    const std::string row = "#   .   .   .   O   .   O   .   .   .   .   #";
    const auto at_row = text.find(row);
    REQUIRE(at_row != std::string::npos);
    text.replace(at_row, row.size(), "#   T   C>  .   O   .   O   .   .   .   .   #");   // belt at (2,4), console at (1,4)
    Match m = setup(load(text), 2, {{2, 4, "E"}, {3, 4, "E"}});   // P1 waits on the belt, which drives it into P2
    play_round(m, {{"WAIT", "WAIT", "WAIT"}, {"WAIT", "WAIT", "WAIT"}});
    auto sh = shunts(m);
    REQUIRE(sh.size() == 1);
    CHECK(pi(*sh[0], "player") == 1);
    CHECK(pi(*sh[0], "victim") == 2);
}

TEST_CASE("G2-D SHUNT: pit-push points count for score targets and persist through save/restore mid-round") {
    MatchConfig c = cfg(sw_map(), 2);
    c.target_score = 8;
    Match m(c);
    {   // rebuild the scenario on a target-8 match: restore into it
        Json saved = m.save();
        Json& ps = const_cast<Json&>(*saved.find("players"));
        const int pos[2][2] = {{5, 3}, {5, 4}};
        for (int i = 0; i < 2; ++i) {
            Json& p = const_cast<Json&>(ps.items()[static_cast<std::size_t>(i)]);
            p.set("x", pos[i][0]);
            p.set("y", pos[i][1]);
            p.set("heading", "S");
        }
        std::string err;
        auto r = Match::restore(c, saved, err);
        REQUIRE_MESSAGE(r.has_value(), err);
        m = std::move(*r);
    }
    submit(m, {{"FORWARD_1", "WAIT", "WAIT"}, {"WAIT", "FORWARD_1", "WAIT"}});
    REQUIRE(m.close_planning());
    REQUIRE(m.run_next_tick());   // tick 1: P1 pushes P2 south; attribution is live mid-round
    CHECK(m.state().robot(2).pushed_by == 1);
    std::string err;
    auto back = Match::restore(c, m.save(), err);
    REQUIRE_MESSAGE(back.has_value(), err);
    CHECK(back->state() == m.state());
    REQUIRE(back->run_next_tick());   // tick 2: P2 walks into the pit; the restored match still credits P1
    REQUIRE(back->run_next_tick());
    CHECK(back->state().robot(1).score == 8);
    CHECK(back->state().phase == Phase::Finished);   // 8 points reach the target, judged after the round
    // a checkpoint that names an impossible pusher is refused
    Json bad = m.save();
    const_cast<Json&>(bad.find("players")->items()[1]).set("pushed_by", 2);
    CHECK_FALSE(Match::restore(c, bad, err).has_value());
}

TEST_CASE("G2-D playback: SHUNT! step, the disable and the crate drop replay to the authoritative board") {
    Match m = setup(sw_map(), 2, {{2, 4, "E"}, {3, 4, "W", 1}});
    const ui::BoardView before = ui::read_board(m.snapshot());
    const int version = m.state_version();
    play_round(m, {{"FORWARD_1"}});
    const auto evs = m.events_since(version);
    REQUIRE(evs.has_value());
    Json events = Json::array();
    for (const auto& e : *evs) events.push(event_to_json(e, "g2d"));
    ui::Playback pb;
    pb.set_speed(8);
    pb.start(before, events);
    int shunt_steps = 0;
    bool in_machinery_before_shunt = false;
    std::size_t last = static_cast<std::size_t>(-1);
    while (pb.active()) {
        const ui::Step* st = pb.current();
        if (pb.step_index() != last) {
            last = pb.step_index();
            if (st->kind == ui::Step::Score && st->shunt) {
                ++shunt_steps;
                in_machinery_before_shunt = st->phase == ui::Step::Machinery;
            }
        }
        pb.advance(50);
    }
    CHECK(shunt_steps == 1);
    CHECK_FALSE(in_machinery_before_shunt);   // a shunt belongs to the orders phase
    const ui::BoardView after = ui::read_board(m.snapshot());
    CHECK_MESSAGE(pb.board().equals(after), pb.board().diff(after));
    CHECK(after.robots[0].score == 8);
    CHECK(after.robots[0].sabotage == 8);
}

TEST_CASE("G2-D: bots play shuntworks-01 to the end deterministically with 2-4 players") {
    for (int players = 2; players <= 4; ++players) {
        sim::SimOptions o;
        o.players = players;
        o.seed = 5;
        o.map_id = "shuntworks-01";
        sim::SimResult a = sim::run_simulation(o), b = sim::run_simulation(o);
        REQUIRE_MESSAGE(a.ok, a.error);
        CHECK(a.rounds_played == 12);
        CHECK(a.state_hash == b.state_hash);
        CHECK(a.event_hash == b.event_hash);
    }
}

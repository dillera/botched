// G2-B step 1: the existing continuous directional lasers, exercised on laserworks-01.
// No rules were changed for this step; these tests pin the current behaviour on a real map.
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

// Lit tiles: (2,5), (3,5) from L> at (1,5); (8,5), (9,5) from L< at (10,5). Safe: (5,5), (6,5).
std::shared_ptr<const Map> lw_map() {
    MapLoad l = load_map("laserworks-01", read_map_resource("laserworks-01.map"));
    REQUIRE_MESSAGE(l.ok(), (l.errors.empty() ? std::string("?") : l.errors[0]));
    return std::make_shared<const Map>(std::move(*l.map));
}

// A fresh match already holds the crates at the dispensers, so test crates get ids above them:
// "crate k" in a setup means id crate_base() + k - 1.
int crate_base() {
    static const int base = Match(MatchConfig{"probe", lw_map(), 2}).state().next_crate_id;
    return base;
}

struct Seat { int x, y; int damage = 0; int carrying = 0; };

MatchConfig cfg(int players) {
    MatchConfig c;
    c.match_id = "g2b";
    c.map = lw_map();
    c.player_count = players;
    return c;
}

// Restores a checkpoint with the given robots; `loose` crates are (id, x, y); carried crate ids are
// taken from the seats. Crate ids must be 1..n.
Match setup(int players, std::initializer_list<Seat> seats, std::vector<std::vector<int>> loose = {}) {
    MatchConfig c = cfg(players);
    Match fresh(c);
    Json saved = fresh.save();
    Json& ps = const_cast<Json&>(*saved.find("players"));
    Json& crates = const_cast<Json&>(*saved.find("crates"));
    int max_id = 0;
    for (const auto& l : loose) {   // loose first: crate ids must stay ascending
        Json cj = Json::object();
        cj.set("id", crate_base() + l[0] - 1);
        cj.set("x", l[1]);
        cj.set("y", l[2]);
        cj.set("carrier_player_id", Json(nullptr));
        crates.push(std::move(cj));
        max_id = std::max(max_id, crate_base() + l[0] - 1);
    }
    int i = 0;
    for (const Seat& s : seats) {
        const int pid = i + 1;
        Json& p = const_cast<Json&>(ps.items()[static_cast<std::size_t>(i++)]);
        p.set("x", s.x);
        p.set("y", s.y);
        p.set("damage", s.damage);
        p.set("damage_taken", s.damage);
        if (s.carrying) {
            const int id = crate_base() + s.carrying - 1;
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
    // crates must be ascending by id
    if (max_id > 0) saved.set("next_crate_id", max_id + 1);
    std::string err;
    auto m = Match::restore(c, saved, err);
    REQUIRE_MESSAGE(m.has_value(), err);
    return std::move(*m);
}

void run_tick(Match& m, int tick_count) {
    const int round = m.state().round;
    for (int p = 1; p <= m.state().player_count(); ++p) m.submit_program(p, round, 1, {"WAIT", "WAIT", "WAIT"}, true, "");
    REQUIRE(m.close_planning());
    for (int i = 0; i < tick_count; ++i) REQUIRE(m.run_next_tick());
}

std::vector<const Event*> events_of(const Match& m, const std::string& type) {
    std::vector<const Event*> v;
    for (const auto& e : m.all_events()) if (e.type == type) v.push_back(&e);
    return v;
}
int pi(const Event& e, const char* k) { return static_cast<int>(e.params.get_int(k).value_or(-1)); }

}  // namespace

TEST_CASE("G2-B map: laserworks-01 loads, validates for 2-4 players and has the intended beams") {
    auto m = lw_map();
    REQUIRE(m->lasers.size() == 2);
    CHECK((m->lasers[0] == Pos{1, 5}));
    CHECK((m->lasers[1] == Pos{10, 5}));
    CHECK(m->tile({1, 5}).dir == Dir::E);
    CHECK(m->tile({10, 5}).dir == Dir::W);
    // the rays stop at the walls: four lit tiles, none in the middle gap
    auto lit = [&](Pos e) {
        std::vector<Pos> v;
        Dir d = m->tile(e).dir;
        for (Pos p{e.x + dir_dx(d), e.y + dir_dy(d)}; !m->blocked(p); p = {p.x + dir_dx(d), p.y + dir_dy(d)}) v.push_back(p);
        return v;
    };
    CHECK(lit(m->lasers[0]).size() == 2);
    CHECK(lit(m->lasers[1]).size() == 2);
    for (int n = 2; n <= 4; ++n) {
        ValidationOptions vo;
        vo.player_count = n;
        CHECK(validate_map(*m, vo).empty());
    }
}

TEST_CASE("G2-B lasers: a robot in a lit lane takes 1 damage per tick and is disabled on the third hit") {
    Match m = setup(2, {{3, 5}, {6, 5}});   // P1 in the west lane, P2 in the safe gap
    run_tick(m, 1);
    CHECK(m.state().robot(1).damage == 1);
    auto hit = events_of(m, "LASER_HIT");
    REQUIRE(hit.size() == 1);
    CHECK(pi(*hit[0], "player") == 1);
    CHECK(pi(*hit[0], "emitter_x") == 1);
    CHECK(pi(*hit[0], "emitter_y") == 5);
    CHECK(m.run_next_tick());
    CHECK(m.state().robot(1).damage == 2);
    CHECK_FALSE(m.state().robot(1).respawn_pending);
    CHECK(m.run_next_tick());   // third tick: third hit, disabled in phase 4 (tick 3 also ends the round: respawn)
    auto dis = events_of(m, "ROBOT_DISABLED");
    REQUIRE(dis.size() == 1);
    CHECK(dis[0]->params.get_string("cause").value_or("") == "LASER");
    CHECK(m.state().robot(1).damage_taken == 3);
    CHECK(m.state().robot(2).damage == 0);                      // the gap is never lit
    CHECK(events_of(m, "SCORE_AWARDED").empty());               // lasers are neutral: no points, no sabotage
    CHECK(m.state().robot(1).score == 0);
    CHECK(m.state().robot(2).score == 0);
}

TEST_CASE("G2-B lasers: the east lane and the wall-stopped tiles behave as designed") {
    Match m = setup(2, {{4, 4}, {8, 5}});    // P1 beside the wall that ends the west beam, P2 in the east lane
    run_tick(m, 1);
    CHECK(m.state().robot(1).damage == 0);   // (4,4) is not on row 5
    CHECK(m.state().robot(2).damage == 1);
    auto hit = events_of(m, "LASER_HIT");
    REQUIRE(hit.size() == 1);
    CHECK(pi(*hit[0], "emitter_x") == 10);
}

TEST_CASE("G2-B lasers: a disabled robot drops its crate on the tile where it fell") {
    Match m = setup(2, {{3, 5, 2, 1}, {6, 5}});   // P1 at damage 2 carrying crate 1, in the lit lane
    run_tick(m, 1);
    CHECK(m.state().robot(1).respawn_pending);
    CHECK(m.state().robot(1).carrying == 0);
    auto dropped = events_of(m, "CRATE_DROPPED");
    REQUIRE(dropped.size() == 1);
    CHECK(pi(*dropped[0], "crate") == crate_base());
    CHECK(pi(*dropped[0], "x") == 3);
    CHECK(pi(*dropped[0], "y") == 5);
    const Crate* c = m.state().crate(crate_base());
    REQUIRE(c);
    CHECK_FALSE(c->carried);
    CHECK((c->pos == Pos{3, 5}));
    // the loose crate is not damaged by the beam on later ticks
    CHECK(m.run_next_tick());
    CHECK(m.state().crate(crate_base()) != nullptr);
}

TEST_CASE("G2-B lasers: if a loose crate already lies where the robot falls, the carried crate is destroyed") {
    Match m = setup(2, {{3, 5, 2, 2}, {6, 5}}, {{1, 3, 5}});   // crate 1 loose at (3,5); P1 carries crate 2
    run_tick(m, 1);
    auto gone = events_of(m, "CRATE_DESTROYED");
    REQUIRE(gone.size() == 1);
    CHECK(pi(*gone[0], "crate") == crate_base() + 1);
    CHECK(gone[0]->params.get_string("reason").value_or("") == "DROP_BLOCKED");
    CHECK(m.state().crate(crate_base() + 1) == nullptr);
    CHECK(m.state().crate(crate_base()) != nullptr);
}

TEST_CASE("G2-B lasers: the disabled robot respawns at round end with damage cleared; damage_taken stays") {
    Match m = setup(2, {{3, 5}, {6, 5}});
    const int round = m.state().round;
    for (int p = 1; p <= 2; ++p) m.submit_program(p, round, 1, {"WAIT", "WAIT", "WAIT"}, true, "");
    m.run_round();
    CHECK_FALSE(m.state().robot(1).respawn_pending);
    CHECK(m.state().robot(1).damage == 0);
    CHECK(m.state().robot(1).damage_taken == 3);
    CHECK((m.state().robot(1).pos == Pos{1, 1}));   // back on its spawn
}

TEST_CASE("G2-B playback: laser hits, the disable, the crate drop and the respawn replay to the authoritative board") {
    Match m = setup(2, {{3, 5, 1, 1}, {6, 5}});   // damage 1 + crate: hit at tick 1 (2), tick 2 (3) -> disable and drop
    const ui::BoardView before = ui::read_board(m.snapshot());
    const int version = m.state_version();
    const int round = m.state().round;
    for (int p = 1; p <= 2; ++p) m.submit_program(p, round, 1, {"WAIT", "WAIT", "WAIT"}, true, "");
    m.run_round();
    const auto evs = m.events_since(version);
    REQUIRE(evs.has_value());
    Json events = Json::array();
    for (const auto& e : *evs) events.push(event_to_json(e, "g2b"));

    ui::Playback pb;
    pb.set_speed(8);
    pb.start(before, events);
    int lasers = 0, disables = 0, drops = 0, respawns = 0;
    std::size_t last = static_cast<std::size_t>(-1);
    while (pb.active()) {
        const ui::Step* st = pb.current();
        if (pb.step_index() != last) {
            last = pb.step_index();
            lasers += st->kind == ui::Step::Laser;
            disables += st->kind == ui::Step::Disable;
            drops += st->kind == ui::Step::Drop;
            respawns += st->kind == ui::Step::Respawn;
        }
        pb.advance(50);
    }
    CHECK(lasers == 2);        // tick 1 and tick 2 hits (the third tick has the robot already gone)
    CHECK(disables == 1);
    CHECK(drops == 1);
    CHECK(respawns == 1);
    const ui::BoardView after = ui::read_board(m.snapshot());
    CHECK_MESSAGE(pb.board().equals(after), pb.board().diff(after));
}

TEST_CASE("G2-B: bots play laserworks-01 to the end deterministically with 2-4 players") {
    for (int players = 2; players <= 4; ++players) {
        sim::SimOptions o;
        o.players = players;
        o.seed = 5;
        o.map_id = "laserworks-01";
        sim::SimResult a = sim::run_simulation(o), b = sim::run_simulation(o);
        REQUIRE_MESSAGE(a.ok, a.error);
        CHECK(a.rounds_played == 12);
        CHECK(a.state_hash == b.state_hash);
        CHECK(a.event_hash == b.event_hash);
    }
}

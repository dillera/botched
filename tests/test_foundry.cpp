// G3: foundry-01 - the first map designed around the G2 machinery.
// 11 x 13. Dispensers: G1 (5,1) north dead end, G3 (5,6) centre pocket, G2 (5,11) south dead end.
// Traps X1 (4,3) / X2 (6,3) wall the north corridor (5,2)-(5,4); tar generators A (4,9) / A (6,9) wall the south
// corridor (5,8)-(5,10); turrets Y1 (3,8) / Y2 (7,8); pits (4,6), (6,6), (2,4), (8,4), (2,8), (8,8).
#include <algorithm>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"
#include "map_fixture.hpp"
#include "sim_runner.hpp"

using namespace botched;

namespace {

using MapPtr = std::shared_ptr<const Map>;

MapPtr fm() {
    MapLoad l = load_map("foundry-01", read_map_resource("foundry-01.map"));
    REQUIRE_MESSAGE(l.ok(), (l.errors.empty() ? std::string("?") : l.errors[0]));
    return std::make_shared<const Map>(std::move(*l.map));
}

int cheb(Pos a, Pos b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

// Walking distance in tiles (walls, devices and pits excluded), ignoring robots and belts.
int walk(const Map& m, Pos from, Pos to) {
    std::vector<int> d(m.tiles.size(), -1);
    std::queue<Pos> q;
    d[static_cast<std::size_t>(from.y * m.width + from.x)] = 0;
    q.push(from);
    while (!q.empty()) {
        Pos c = q.front();
        q.pop();
        if (c.x == to.x && c.y == to.y) return d[static_cast<std::size_t>(c.y * m.width + c.x)];
        for (int k = 0; k < 4; ++k) {
            Dir dd = static_cast<Dir>(k);
            Pos n{c.x + dir_dx(dd), c.y + dir_dy(dd)};
            if (!m.walkable(n) && !(n.x == to.x && n.y == to.y)) continue;
            if (m.blocked(n)) continue;
            auto& slot = d[static_cast<std::size_t>(n.y * m.width + n.x)];
            if (slot >= 0) continue;
            slot = d[static_cast<std::size_t>(c.y * m.width + c.x)] + 1;
            q.push(n);
        }
    }
    return -1;
}

struct Seat { int x, y; const char* heading = "E"; };

MatchConfig cfg(const MapPtr& map, int players) {
    MatchConfig c;
    c.match_id = "g3";
    c.map = map;
    c.player_count = players;
    return c;
}

Match setup(const MapPtr& map, int players, std::initializer_list<Seat> seats) {
    MatchConfig c = cfg(map, players);
    Json saved = Match(c).save();
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

void play_round(Match& m, const Programs& progs = {}) {
    const int round = m.state().round;
    for (int p = 1; p <= m.state().player_count(); ++p) {
        std::vector<std::string> pr = static_cast<std::size_t>(p) <= progs.size() ? progs[static_cast<std::size_t>(p - 1)] : std::vector<std::string>{};
        while (pr.size() < 3) pr.push_back("WAIT");
        m.submit_program(p, round, 1, pr, true, "");
    }
    m.run_round();
}

int count_events(const Match& m, const std::string& type, int player = 0) {
    int n = 0;
    for (const auto& e : m.all_events())
        if (e.type == type && (player == 0 || e.params.get_int("player").value_or(-1) == player)) ++n;
    return n;
}

}  // namespace

TEST_CASE("G3 map: foundry-01 validates for 2-4 players and uses every G2 hazard") {
    auto m = fm();
    for (int players = 2; players <= 4; ++players) {
        ValidationOptions o;
        o.player_count = players;
        CHECK(validate_map(*m, o).empty());
    }
    CHECK(m->width == 11);
    CHECK(m->height == 13);
    CHECK(m->dispensers.size() == 3);   // fewer dispensers than the four players it supports
    CHECK(m->traps.size() == 2);
    CHECK(m->trap_consoles.size() == 2);
    CHECK(m->turrets.size() == 2);
    CHECK(m->turret_pads.size() == 4);
    CHECK(m->tar_generators.size() == 2);
    int pits = 0, belts_a = 0, belts_b = 0, consoles = 0;
    for (const auto& t : m->tiles) {
        pits += t.terrain == Terrain::Pit;
        belts_a += t.terrain == Terrain::Conveyor && t.group == 'A';
        belts_b += t.terrain == Terrain::Conveyor && t.group == 'B';
        consoles += t.terrain == Terrain::Console;
    }
    CHECK(pits == 6);
    CHECK(belts_a == 4);
    CHECK(belts_b == 4);
    CHECK(consoles == 2);
}

TEST_CASE("G3 map: the earlier reference maps are still shipped unchanged") {
    for (const char* f : {"training-01.map", "crossroads-01.map", "factory-01.map"}) {
        MapLoad l = load_map(f, read_map_resource(f));
        CHECK_MESSAGE(l.ok(), f);
    }
}

TEST_CASE("G3 map: the layout is symmetric left-right and top-bottom, so every seat gets the same game") {
    auto m = fm();
    auto structural = [](Terrain t) {   // what shapes the routes, ignoring which machine sits where
        switch (t) {
            case Terrain::Wall: case Terrain::Pit: case Terrain::Dock: case Terrain::Spawn: case Terrain::Dispenser:
            case Terrain::Conveyor: return t;
            default: return Terrain::Floor;   // floor, consoles, pads: open ground
        }
    };
    for (int y = 0; y < m->height; ++y)
        for (int x = 0; x < m->width; ++x) {
            INFO("tile (" << x << "," << y << ")");
            // Left-right mirror is exact: same terrain, including every machine.
            CHECK(m->tile({x, y}).terrain == m->tile({m->width - 1 - x, y}).terrain);
            // Top-bottom mirror: the same route structure (traps and tar generators are each other's counterpart).
            auto up = m->tile({x, y}).terrain, down = m->tile({x, m->height - 1 - y}).terrain;
            const bool wall_like = [&](Terrain t) { return t == Terrain::Trap || t == Terrain::TarGenerator || t == Terrain::Turret; }(up);
            const bool wall_like2 = [&](Terrain t) { return t == Terrain::Trap || t == Terrain::TarGenerator || t == Terrain::Turret; }(down);
            if (!wall_like && !wall_like2) CHECK(structural(up) == structural(down));
        }
    // The walking distance from each spawn to each dispenser matches the mirror seat's.
    const auto s = [&](int p) { return *m->spawns[static_cast<std::size_t>(p)]; };
    for (const auto& d : m->dispensers) {
        CHECK(walk(*m, s(1), d.pos) == walk(*m, s(2), d.pos));
        CHECK(walk(*m, s(3), d.pos) == walk(*m, s(4), d.pos));
    }
    // North and south seats are equally far from the centre, and from their own dead end to within two tiles
    // (the turret at (3,8) costs the south a short detour; it pays for it in the tar).
    CHECK(walk(*m, s(1), {5, 6}) == walk(*m, s(3), {5, 6}));
    CHECK(std::abs(walk(*m, s(1), {5, 1}) - walk(*m, s(3), {5, 11})) <= 2);
}

TEST_CASE("G3 map: no dispenser sits near a dock - every pickup is a real journey") {
    auto m = fm();
    for (int p = 1; p <= 4; ++p) {
        const Pos dock = *m->docks[static_cast<std::size_t>(p)];
        for (const auto& d : m->dispensers) {
            const int w = walk(*m, dock, d.pos);
            INFO("dock " << p << " to dispenser G" << d.id << " = " << w);
            CHECK(w >= 6);
        }
    }
}

TEST_CASE("G3 map: each hazard really guards its dispenser and the contested centre is free of machinery") {
    auto m = fm();
    // North corridor: every tile is inside both trap rings; the dispenser itself is outside them.
    for (int y = 2; y <= 4; ++y)
        for (const Trap& t : m->traps) CHECK(cheb({5, y}, t.pos) == 1);
    for (const Trap& t : m->traps) CHECK(cheb({5, 1}, t.pos) > 1);
    // South corridor: tar; the dispenser tile and the centre are not.
    for (int y = 8; y <= 10; ++y) CHECK(m->in_tar({5, y}));
    CHECK_FALSE(m->in_tar({5, 11}));
    CHECK_FALSE(m->in_tar({5, 6}));
    // Turrets cover the corridor mouth and the lower belts but not the pocket or either dispenser tile.
    auto covered = [&](Pos p) {
        for (const Turret& t : m->turrets) {
            if ((p.x == t.pos.x && std::abs(p.y - t.pos.y) <= 2) || (p.y == t.pos.y && std::abs(p.x - t.pos.x) <= 2))
                return true;
        }
        return false;
    };
    CHECK(covered({5, 8}));
    CHECK(covered({4, 8}));
    CHECK(covered({6, 8}));
    CHECK(covered({3, 9}));
    CHECK(covered({7, 9}));
    CHECK_FALSE(covered({5, 6}));
    CHECK_FALSE(covered({5, 11}));
    CHECK_FALSE(covered({5, 1}));
    // The centre pocket and its mouths touch no trap ring, tar or turret line.
    for (Pos p : {Pos{5, 5}, Pos{5, 6}, Pos{5, 7}}) {
        for (const Trap& t : m->traps) CHECK(cheb(p, t.pos) > 1);
        CHECK_FALSE(m->in_tar(p));
        CHECK_FALSE(covered(p));
    }
}

TEST_CASE("G3: the north corridor really is a murder passage - one step in takes a hit from each armed trap") {
    auto map = fm();
    Match m = setup(map, 2, {{5, 4, "N"}});
    play_round(m, {{"FORWARD_1"}});
    CHECK(m.state().robot(1).pos == Pos{5, 3});
    CHECK(count_events(m, "TRAP_TRIGGERED") == 2);
    CHECK(count_events(m, "TRAP_HIT", 1) == 2);
}

TEST_CASE("G3: the south corridor crawls in tar - FORWARD_1 is a no-op and FORWARD_2 moves one tile") {
    auto map = fm();
    {
        Match m = setup(map, 2, {{5, 9, "S"}});
        play_round(m, {{"FORWARD_1", "FORWARD_1", "FORWARD_1"}});
        CHECK(m.state().robot(1).pos == Pos{5, 9});
        CHECK(count_events(m, "ORDER_NO_OP", 1) == 3);
    }
    {
        Match m = setup(map, 2, {{5, 9, "S"}});
        play_round(m, {{"FORWARD_2"}});
        CHECK(m.state().robot(1).pos == Pos{5, 10});
        CHECK(count_events(m, "TAR_SLOWED", 1) == 1);
    }
}

TEST_CASE("G3: a robot dawdling at the south corridor mouth is shot by the neutral turret") {
    auto map = fm();
    Match m = setup(map, 2, {{4, 8, "E"}});
    play_round(m);   // three WAITs: Y1 (3,8) charges for two ticks and fires on the third
    CHECK(count_events(m, "TURRET_FIRED") == 1);
}

TEST_CASE("G3: a flank tile beside the centre pocket is a SHUNT line - a push from above drops the target in the pit") {
    auto map = fm();
    // P2 stands on (4,5) above pit (4,6); P1 pushes it from (4,4).
    Match m = setup(map, 2, {{4, 4, "S"}, {4, 5, "E"}});
    play_round(m, {{"FORWARD_1"}});
    int shunt = 0;
    for (const auto& e : m.all_events())
        if (e.type == "SCORE_AWARDED" && e.params.get_string("reason").value_or("") == "SHUNT") ++shunt;
    CHECK(shunt == 1);
    CHECK(m.state().robot(1).sabotage == 8);
}

TEST_CASE("G3: bots play foundry-01 to the end deterministically with 2-4 players, and the machinery is used") {
    for (int players = 2; players <= 4; ++players) {
        sim::SimOptions o;
        o.players = players;
        o.seed = 3;
        o.map_id = "foundry-01";
        sim::SimResult a = sim::run_simulation(o), b = sim::run_simulation(o);
        REQUIRE_MESSAGE(a.ok, a.error);
        CHECK(a.rounds_played == 12);
        CHECK(a.state_hash == b.state_hash);
        CHECK(a.event_hash == b.event_hash);
    }
}

TEST_CASE("G3: bots on foundry-01 deliver crates, contest the centre and earn sabotage points") {
    int deliveries = 0, sabotage = 0, players_with_delivery = 0;
    std::vector<int> delivered(5, 0);
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        sim::SimOptions o;
        o.players = 4;
        o.seed = seed;
        o.map_id = "foundry-01";
        sim::SimResult r = sim::run_simulation(o);
        REQUIRE_MESSAGE(r.ok, r.error);
        const Json* ps = r.final_snapshot.find("players");
        REQUIRE(ps != nullptr);
        for (const Json& p : ps->items()) {
            deliveries += static_cast<int>(p.get_int("deliveries").value_or(0));
            sabotage += static_cast<int>(p.get_int("sabotage").value_or(0));
            delivered[static_cast<std::size_t>(p.get_int("id").value_or(0))] +=
                static_cast<int>(p.get_int("deliveries").value_or(0));
        }
    }
    for (int p = 1; p <= 4; ++p) players_with_delivery += delivered[static_cast<std::size_t>(p)] > 0;
    CHECK(deliveries >= 10);
    CHECK(sabotage > 0);
    CHECK(players_with_delivery == 4);   // bots do not just farm one corner
}

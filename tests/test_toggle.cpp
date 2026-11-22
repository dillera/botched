// Multi-console TOGGLE: one TOGGLE activates every console (belt or trap) on the robot's tile or the four
// orthogonally adjacent tiles, once each, in row-major order. Uses foundry-01: K1 (1,5), TA (3,5), TB (7,5), K2 (9,5).
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "botched/match.hpp"
#include "doctest.h"
#include "map_fixture.hpp"

using namespace botched;

namespace {

using MapPtr = std::shared_ptr<const Map>;

MapPtr load(const std::string& text) {
    MapLoad l = load_map("toggle-test", text);
    REQUIRE_MESSAGE(l.ok(), (l.errors.empty() ? std::string("?") : l.errors[0]));
    return std::make_shared<const Map>(std::move(*l.map));
}

MatchConfig cfg(const MapPtr& map) {
    MatchConfig c;
    c.match_id = "toggle";
    c.map = map;
    c.player_count = 2;
    return c;
}

Match setup(const MapPtr& map, int x, int y) {
    Json saved = Match(cfg(map)).save();
    Json& p = const_cast<Json&>(saved.find("players")->items()[0]);
    p.set("x", x);
    p.set("y", y);
    std::string err;
    auto m = Match::restore(cfg(map), saved, err);
    REQUIRE_MESSAGE(m.has_value(), err);
    return std::move(*m);
}

void toggle_once(Match& m) {
    const int round = m.state().round;
    m.submit_program(1, round, 1, {"TOGGLE", "WAIT", "WAIT"}, true, "");
    m.submit_program(2, round, 1, {"WAIT", "WAIT", "WAIT"}, true, "");
    m.run_round();
}

// The activation events of tick 1 in stream order: "TRAP_ARMED:<trap>" or "GROUP_TOGGLED:<console id>".
std::vector<std::string> activations(const Match& m) {
    std::vector<std::string> v;
    for (const auto& e : m.all_events()) {
        if (e.type == "TRAP_ARMED") v.push_back("TRAP_ARMED:" + std::to_string(e.params.get_int("trap").value_or(-1)));
        if (e.type == "GROUP_TOGGLED") v.push_back("GROUP_TOGGLED:" + e.params.get_string("console").value_or("?"));
    }
    return v;
}

int count(const Match& m, const std::string& type) {
    int n = 0;
    for (const auto& e : m.all_events()) n += e.type == type;
    return n;
}

MapPtr foundry() { return load(read_map_resource("foundry-01.map")); }

}  // namespace

TEST_CASE("multi-console TOGGLE: one tile between K1 and TA arms the trap and reverses the belts, in row-major order") {
    Match m = setup(foundry(), 2, 5);   // K1 (1,5) west, TA (3,5) east
    toggle_once(m);
    CHECK(count(m, "ORDER_EXECUTED") >= 1);
    CHECK(count(m, "ORDER_NO_OP") == 0);
    const auto a = activations(m);
    REQUIRE(a.size() == 2);
    CHECK(a[0] == "TRAP_ARMED:1");          // (1,5) before (3,5)
    CHECK(a[1].rfind("GROUP_TOGGLED:", 0) == 0);
    CHECK(m.state().trap(1)->armed);
    CHECK(m.state().trap(1)->owner == 1);
    CHECK(m.state().group('A')->reversed);
    CHECK_FALSE(m.state().group('B')->reversed);
    CHECK(m.state().trap(2)->owner == 0);   // K2 was out of range
}

TEST_CASE("multi-console TOGGLE: a console on the robot's own tile counts, and further ones out of range do not") {
    Match m = setup(foundry(), 1, 5);   // standing on K1; TA at (3,5) is two tiles away
    toggle_once(m);
    const auto a = activations(m);
    REQUIRE(a.size() == 1);
    CHECK(a[0] == "TRAP_ARMED:1");
    CHECK_FALSE(m.state().group('A')->reversed);
}

TEST_CASE("multi-console TOGGLE: consoles above and below resolve by row, then column") {
    // Put a second belt console (group B) directly above (4,5) so the robot at (4,5) has TB(4,4) north and TA(3,5) west.
    std::string text = read_map_resource("foundry-01.map");
    const std::string row4 = "#   .   O   .   .   .   .   .   O   .   #";
    const std::string row4_tb = "#   .   O   .   TB  .   .   .   O   .   #";
    const auto at = text.find(row4);
    REQUIRE(at != std::string::npos);
    text.replace(at, row4.size(), row4_tb);
    Match m = setup(load(text), 4, 5);
    toggle_once(m);
    const auto a = activations(m);
    REQUIRE(a.size() == 2);
    // The scan-order ids follow (y,x): the console at row 4 sorts before the one at row 5.
    CHECK(a[0] < a[1]);
    CHECK(m.state().group('A')->reversed);
    CHECK(m.state().group('B')->reversed);
}

TEST_CASE("multi-console TOGGLE: each console is activated once per instruction; two of one group cancel out") {
    // Two group-A consoles in range of one tile: each flips the group once, so the net effect is unchanged.
    std::string text = read_map_resource("foundry-01.map");
    const std::string row4 = "#   .   O   .   .   .   .   .   O   .   #";
    const std::string row4_ta = "#   .   O   .   T   .   .   .   O   .   #";
    text.replace(text.find(row4), row4.size(), row4_ta);
    Match m = setup(load(text), 4, 5);
    toggle_once(m);
    CHECK(count(m, "GROUP_TOGGLED") == 2);
    CHECK_FALSE(m.state().group('A')->reversed);
}

TEST_CASE("multi-console TOGGLE: with no console in range it is still a NO_CONSOLE no-op") {
    Match m = setup(foundry(), 5, 2);
    toggle_once(m);
    CHECK(activations(m).empty());
    bool noop = false;
    for (const auto& e : m.all_events())
        if (e.type == "ORDER_NO_OP" && e.params.get_string("reason").value_or("") == "NO_CONSOLE") noop = true;
    CHECK(noop);
}

TEST_CASE("multi-console TOGGLE: a lone belt console and a lone trap console behave exactly as before") {
    Match belt = setup(foundry(), 4, 5);    // TA (3,5) only
    toggle_once(belt);
    CHECK(activations(belt).size() == 1);
    CHECK(belt.state().group('A')->reversed);
    CHECK(belt.state().trap(1)->owner == 0);
    Match trap = setup(foundry(), 1, 4);    // K1 (1,5) only
    toggle_once(trap);
    CHECK(activations(trap) == std::vector<std::string>{"TRAP_ARMED:1"});
    CHECK_FALSE(trap.state().group('A')->reversed);
}

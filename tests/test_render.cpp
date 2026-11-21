// Playtest board renderer (apps/ascii_render): fixed cell width, robots replace terrain,
// Unicode/colour/ASCII variants, legend and status text.
#include <sstream>
#include <string>
#include <vector>

#include "ascii_render.hpp"
#include "botched/match.hpp"
#include "doctest.h"
#include "map_fixture.hpp"

using namespace botched;
using namespace botched::sim;

namespace {

// Terminal columns of a line: skips ANSI SGR sequences, counts one per UTF-8 character.
std::size_t columns(const std::string& s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0x1b) { while (i < s.size() && s[i] != 'm') ++i; continue; }
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string l; std::getline(in, l);) out.push_back(l);
    return out;
}

Match fresh() {
    MatchConfig c;
    c.match_id = "m";
    c.map = std::make_shared<const Map>(training01());
    c.player_count = 2;
    return Match(c);
}

}  // namespace

TEST_CASE("renderer: every cell is 3 columns in all style combinations") {
    Match m = fresh();
    for (int u = 0; u < 2; ++u)
        for (int c = 0; c < 2; ++c) {
            auto rows = split(render_board_styled(m.map(), m.snapshot(), Style{u != 0, c != 0}));
            REQUIRE(rows.size() == static_cast<std::size_t>(m.map().height) + 1);
            for (const auto& r : rows) CHECK(columns(r) == 4 + 3 * static_cast<std::size_t>(m.map().width));
        }
}

TEST_CASE("renderer: ASCII fallback has no escapes or non-ASCII; Unicode uses solid walls") {
    Match m = fresh();
    std::string ascii = render_board_styled(m.map(), m.snapshot(), Style{false, false});
    for (unsigned char ch : ascii) CHECK((ch < 0x80 && ch != 0x1b));
    CHECK(ascii.find("###") != std::string::npos);

    std::string uni = render_board_styled(m.map(), m.snapshot(), Style{true, false});
    CHECK(uni.find("\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88") != std::string::npos);  // full blocks
    CHECK(uni.find('#') == std::string::npos);
    CHECK(uni.find('\x1b') == std::string::npos);  // colour is independent

    std::string col = render_board_styled(m.map(), m.snapshot(), Style{true, true});
    CHECK(col.find("\x1b[") != std::string::npos);
}

TEST_CASE("renderer: dispenser cells carry no crate marker; robots replace the tile under them") {
    Match m = fresh();
    // Fresh match: every dispenser holds a crate, which stands on the dispenser tile.
    Json snap = m.snapshot();
    auto rows = split(render_board_styled(m.map(), snap, Style{false, false}));
    // Row 1 holds P1 spawn (1), D1 (2) and G1 (5): the crate on G1 is drawn as a crate, never "G1*".
    CHECK(rows[2].find("G1 ") != std::string::npos);  // loaded dispenser: upper case, no '*'
    CHECK(rows[2].find("G1*") == std::string::npos);
    CHECK(rows[2].find("g1") == std::string::npos);

    // Move P1 onto a belt tile by editing the snapshot: robot glyph replaces the belt arrow.
    Json edited = Json::parse(snap.dump()).value();
    Json players = Json::array();
    for (const auto& p : snap.find("players")->items()) {
        Json q = p;
        if (p.get_int("id").value_or(0) == 1) { q.set("x", 4); q.set("y", 3); q.set("heading", "E"); }
        players.push(std::move(q));
    }
    edited.set("players", std::move(players));
    auto row3 = split(render_board_styled(m.map(), edited, Style{false, false}))[4];
    CHECK(row3.find("1> ") != std::string::npos);
    CHECK(row3.find(" >  > ") != std::string::npos);
}

TEST_CASE("renderer: legend reports dispenser contents and belt group state") {
    Match m = fresh();
    Json snap = m.snapshot();
    std::string leg = render_legend(m.map(), snap, Style{false, false});
    CHECK(leg.find("G1(5,1) crate ready") != std::string::npos);
    CHECK(leg.find("group A: normal") != std::string::npos);

    // Remove the crate at G1 and reverse group A.
    Json crates = Json::array();
    for (const auto& c : snap.find("crates")->items())
        if (!(c.get_int("x").value_or(-1) == 5 && c.get_int("y").value_or(-1) == 1)) crates.push(c);
    snap.set("crates", std::move(crates));
    Json groups = Json::array();
    Json g = Json::object();
    g.set("id", "A");
    g.set("reversed", true);
    groups.push(std::move(g));
    snap.set("groups", std::move(groups));
    leg = render_legend(m.map(), snap, Style{false, false});
    CHECK(leg.find("G1(5,1) empty") != std::string::npos);
    CHECK(leg.find("group A: REVERSED") != std::string::npos);
    CHECK(split(render_board_styled(m.map(), snap, Style{false, false}))[2].find("g1 ") != std::string::npos);  // empty

    // A reversed belt is drawn with its effective direction.
    auto rows = split(render_board_styled(m.map(), snap, Style{false, false}));
    CHECK(rows[4].find(" <  <  < ") != std::string::npos);
}

TEST_CASE("renderer: describe_tile names the terrain under a robot") {
    Match m = fresh();
    Json snap = m.snapshot();
    CHECK(describe_tile(m.map(), snap, {1, 1}, 1) == "your spawn");
    CHECK(describe_tile(m.map(), snap, {1, 1}, 2) == "spawn of P1");
    CHECK(describe_tile(m.map(), snap, {4, 3}, 1).find("conveyor heading E (group A, normal)") == 0);
    CHECK(describe_tile(m.map(), snap, {5, 4}, 1) == "pit");
    CHECK(describe_tile(m.map(), snap, {5, 5}, 1) == "console, belt group A");
    CHECK(describe_tile(m.map(), snap, {5, 1}, 1) == "dispenser G1 (crate here)");
    CHECK(describe_tile(m.map(), snap, {2, 1}, 1).find("your dock") == 0);
}

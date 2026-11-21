// Map parsing/validation (spec 7-8) and the JSON codec.
#include "botched/json.hpp"
#include "botched/map.hpp"
#include "doctest.h"
#include "map_fixture.hpp"

using namespace botched;

namespace {
std::string replace_first(std::string s, const std::string& from, const std::string& to) {
    auto i = s.find(from);
    REQUIRE(i != std::string::npos);
    s.replace(i, from.size(), to);
    return s;
}
bool has_error(const std::vector<std::string>& errs, const std::string& needle) {
    for (const auto& e : errs) if (e.find(needle) != std::string::npos) return true;
    return false;
}
}  // namespace

TEST_CASE("map: training-01 parses, validates and has the documented features") {
    MapLoad r = load_map("training-01", training01_text());
    REQUIRE_MESSAGE(r.ok(), (r.errors.empty() ? "" : r.errors[0]));
    const Map& m = *r.map;
    CHECK(m.width == 12);
    CHECK(m.height == 10);
    CHECK(*m.spawns[1] == Pos{1, 1});
    CHECK(*m.docks[2] == Pos{9, 1});
    CHECK(*m.spawns[4] == Pos{10, 8});
    CHECK(m.dispensers.size() == 4);
    CHECK(m.dispensers[0].pos == Pos{5, 1});
    CHECK(m.dispensers[1].pos == Pos{9, 3});
    CHECK(m.dispensers[2].pos == Pos{3, 5});
    CHECK(m.dispensers[3].pos == Pos{9, 7});
    CHECK(m.tile({5, 4}).terrain == Terrain::Pit);
    CHECK(m.tile({5, 5}).terrain == Terrain::Console);
    CHECK(m.tile({4, 3}).dir == Dir::E);
    CHECK(m.tile({4, 6}).dir == Dir::W);
    CHECK(m.lasers.empty());  // the learning map has no lasers
    CHECK(m.groups == std::vector<char>{'A'});
    // Initial headings: P1 E, P2 W, P3 E, P4 W.
    CHECK(m.tile(*m.spawns[1]).dir == Dir::E);
    CHECK(m.tile(*m.spawns[2]).dir == Dir::W);
    CHECK(m.tile(*m.spawns[3]).dir == Dir::E);
    CHECK(m.tile(*m.spawns[4]).dir == Dir::W);
}

TEST_CASE("map: validates for 2 and 3 players (extra docks are harmless)") {
    for (int n : {2, 3, 4}) {
        ValidationOptions o;
        o.player_count = n;
        CHECK(validate_map(training01(), o).empty());
    }
}

TEST_CASE("map: text and JSON definitions round-trip") {
    const Map& m = training01();
    MapLoad again = parse_map("training-01", map_to_text(m));
    REQUIRE(again.map.has_value());
    CHECK(map_to_text(*again.map) == map_to_text(m));
    MapLoad viaJson = map_from_json(map_to_json(m));
    REQUIRE(viaJson.map.has_value());
    CHECK(map_to_text(*viaJson.map) == map_to_text(m));
    CHECK(viaJson.map->id == "training-01");
}

TEST_CASE("map: parse errors are reported") {
    CHECK_FALSE(parse_map("x", "# # #\n# ? #\n").ok());
    CHECK_FALSE(parse_map("x", "# # #\n# #\n").ok());   // ragged
    CHECK_FALSE(parse_map("x", "").ok());
    CHECK_FALSE(parse_map("x", "S9 #\n").ok());          // owner out of range
    CHECK_FALSE(parse_map("x", "C? #\n").ok());
}

TEST_CASE("map validator: section 8 rejections") {
    std::string good = training01_text();
    ValidationOptions o;

    SUBCASE("dimensions") {
        auto r = parse_map("tiny", "# # #\n# . #\n# # #\n");
        REQUIRE(r.map.has_value());
        CHECK(has_error(validate_map(*r.map, o), "dimensions"));
    }
    SUBCASE("missing dock") {
        auto r = parse_map("x", replace_first(good, "D3", " ."));
        CHECK(has_error(validate_map(*r.map, o), "player 3 needs exactly one dock"));
    }
    SUBCASE("duplicate spawn") {
        auto r = parse_map("x", replace_first(good, "G3", "S1"));
        CHECK(has_error(validate_map(*r.map, o), "player 1 needs exactly one spawn"));
    }
    SUBCASE("dispenser count and id uniqueness") {
        auto r = parse_map("x", replace_first(replace_first(good, "G2", "."), "G3", "."));
        r = parse_map("x", replace_first(replace_first(replace_first(good, "G2", " ."), "G3", " ."), "G4", " ."));
        CHECK(has_error(validate_map(*r.map, o), "at least two dispensers"));
        auto dup = parse_map("x", replace_first(good, "G2", "G1"));
        CHECK(has_error(validate_map(*dup.map, o), "duplicate dispenser id"));
    }
    SUBCASE("console group without conveyor and vice versa") {
        auto r = parse_map("x", replace_first(good, " T ", " TB"));
        auto errs = validate_map(*r.map, o);
        CHECK(has_error(errs, "console group B has no conveyor"));
        CHECK(has_error(errs, "conveyor group A has no console"));
    }
    SUBCASE("spawn cannot reach its dock") {
        auto r = parse_map("x",
            "# # # # # # # #\n"
            "# S1 # D1 G1 . . #\n"
            "# # # . . . . #\n"
            "# S2 D2 G2 . . . #\n"
            "# . . . . . . #\n"
            "# . . . . . . #\n"
            "# . . . . . . #\n"
            "# # # # # # # #\n");
        REQUIRE(r.map.has_value());
        ValidationOptions two;
        two.player_count = 2;
        CHECK(has_error(validate_map(*r.map, two), "player 1 spawn cannot reach own dock"));
    }
    SUBCASE("conveyor facing out of bounds") {
        auto r = parse_map("x", "# # # # # # # #\n# S1 D1 G1 G2 . . C>\n# S2 D2 . . . . #\n"
                                "# . . . . . . #\n# . . . . . . #\n# . . . . . . #\n# . . . . . . #\n# # # # # # # #\n");
        REQUIRE(r.map.has_value());
        ValidationOptions two;
        two.player_count = 2;
        CHECK(has_error(validate_map(*r.map, two), "out of bounds"));
        two.strict_first_party = false;
        CHECK_FALSE(has_error(validate_map(*r.map, two), "out of bounds"));
    }
}

TEST_CASE("json: canonical dump keeps insertion order and escapes") {
    Json j = Json::object();
    j.set("b", 1);
    j.set("a", "x\"y\\z\n");
    j.set("n", nullptr);
    j.set("t", true);
    Json arr = Json::array();
    arr.push(1);
    arr.push("two");
    j.set("arr", arr);
    CHECK(j.dump() == "{\"b\":1,\"a\":\"x\\\"y\\\\z\\n\",\"n\":null,\"t\":true,\"arr\":[1,\"two\"]}");
    j.set("b", 2);  // replacing keeps position
    CHECK(j.dump().rfind("{\"b\":2", 0) == 0);
}

TEST_CASE("json: parse round trip and rejection of malformed input") {
    const std::string text = "{\"a\":[1,-2,{\"k\":\"v\"}],\"b\":false,\"c\":null,\"d\":\"\\u0041\"}";
    auto j = Json::parse(text);
    REQUIRE(j.has_value());
    CHECK(j->get_string("d") == "A");
    CHECK(j->find("a")->items()[1].as_int() == -2);
    CHECK(Json::parse(j->dump())->dump() == j->dump());
    for (const char* bad : {"", "{", "{\"a\":}", "[1,]", "{\"a\":1} x", "01x", "1.5", "\"unterminated",
                            "{\"a\" 1}", "tru", "[1 2]"}) {
        CHECK_FALSE(Json::parse(bad).has_value());
    }
    std::string deep(200, '[');
    CHECK_FALSE(Json::parse(deep).has_value());
}

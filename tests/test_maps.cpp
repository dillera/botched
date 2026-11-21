// File-based map resources: loading, metadata, diagnostics, LIST_MAPS, selection, path resolution.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <sstream>
#include <unistd.h>

#include "sim_runner.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"
#include "map_fixture.hpp"
#include "server.hpp"

using namespace botched;
using namespace botched::server;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("botched-maps-test-" + std::to_string(::getpid()) + "-" + std::to_string(++n));
        fs::create_directories(path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    void write(const std::string& name, const std::string& text) const { std::ofstream(path / name) << text; }
};

// A resource built from the real training-01 tiles with a custom header.
std::string with_header(const std::string& header) {
    std::string rows;
    std::istringstream in(training01_text());
    for (std::string l; std::getline(in, l);)
        if (!l.empty() && l[0] != ';') rows += l + "\n";
    return header + rows;
}
std::string good_header(const std::string& id, const std::string& players = "2-4") {
    return "; id: " + id + "\n; name: Test " + id + "\n; description: a test map\n; players: " + players + "\n";
}

Json req(const char* type) {
    Json j = Json::object();
    j.set("type", type);
    return j;
}

}  // namespace

TEST_CASE("maps: training-01 and factory-01 load from files with their metadata") {
    MapDirReport rep = load_map_dir(BOTCHED_TEST_MAPS_DIR);
    CHECK(rep.errors.empty());
    REQUIRE(rep.maps.size() >= 2);  // more maps may be added to the directory over time
    auto find = [&](const char* id) -> const LoadedMap* {
        for (const auto& m : rep.maps) if (m.info.id == id) return &m;
        return nullptr;
    };
    for (std::size_t i = 1; i < rep.maps.size(); ++i) CHECK(rep.maps[i - 1].info.file < rep.maps[i].info.file);  // ascending file name
    const LoadedMap* f = find("factory-01");
    REQUIRE(f);
    CHECK(f->info.name == "Factory Floor");
    CHECK(f->info.min_players == 2);
    CHECK(f->info.max_players == 4);
    CHECK(f->map->width == 14);
    CHECK(f->map->height == 11);
    CHECK(f->map->groups == std::vector<char>{'A', 'B'});  // C>A / C<B conveyors with TA / TB consoles
    const LoadedMap* t = find("training-01");
    REQUIRE(t);
    CHECK(t->info.name == "Training Floor");
    CHECK(t->map->id == "training-01");
}

TEST_CASE("maps: training-01 from its resource file is byte-for-byte the map it always was") {
    // Hashes of the canonical forms recorded from the previously compiled-in definition.
    CHECK(hex64(fnv1a64(map_to_json(training01()).dump())) == "df14f4c151088c8f");
    CHECK(hex64(fnv1a64(map_to_text(training01()))) == "fb1e47a7efd680f4");
}

TEST_CASE("maps: the server registers resource maps and LIST_MAPS is compact and deterministic") {
    ManualClock clock;
    GameServer a(ServerConfig{}, clock), b(ServerConfig{}, clock);
    Json la = a.handle(req("LIST_MAPS")), lb = b.handle(req("LIST_MAPS"));
    CHECK(la.dump() == lb.dump());
    CHECK(la.get_bool("ok").value_or(false));
    const Json* maps = la.find("maps");
    REQUIRE(maps != nullptr);
    REQUIRE(maps->items().size() >= 2);
    const Json* training = nullptr;
    std::string prev;
    bool has_factory = false;
    for (const auto& m : maps->items()) {
        const std::string id = m.get_string("map_id").value_or("");
        CHECK(prev < id);  // ascending id
        prev = id;
        has_factory = has_factory || id == "factory-01";
        if (id == "training-01") training = &m;
    }
    CHECK(has_factory);
    REQUIRE(training);
    CHECK(training->get_string("name").value_or("") == "Training Floor");
    CHECK(training->get_int("min_players").value_or(0) == 2);
    CHECK(training->get_int("max_players").value_or(0) == 4);
    CHECK(la.dump().size() < 200 * maps->items().size());  // small enough for constrained clients
    CHECK(a.map_report().errors.empty());
}

TEST_CASE("maps: matches can be created and played to the end on either map; joiners get the map via GET_MAP") {
    for (const char* id : {"training-01", "factory-01"}) {
        sim::SimOptions o;
        o.map_id = id;
        o.players = 3;
        sim::SimResult r = sim::run_simulation(o);
        CHECK_MESSAGE(r.ok, id << ": " << r.error);
        CHECK(r.rounds_played == 12);
        const Json* b = r.final_snapshot.find("board");
        REQUIRE(b != nullptr);
        CHECK(b->get_string("map_id").value_or("") == id);
    }
    // default map is unchanged, unknown ids are refused
    ManualClock clock;
    GameServer srv(ServerConfig{}, clock);
    Json c = req("CREATE_MATCH");
    c.set("player_count", 2);
    Json created = srv.handle(c);
    CHECK(created.get_bool("ok").value_or(false));
    Json m = req("GET_MAP");
    m.set("match_id", *created.get_string("match_id"));
    CHECK(srv.handle(m).find("map")->get_string("map_id").value_or("") == "training-01");
    c.set("map_id", "no-such-map");
    CHECK(srv.handle(c).get_string("code").value_or("") == "UNKNOWN_MAP");
    c.set("map_id", "factory-01");
    Json f = srv.handle(c);
    CHECK(f.get_bool("ok").value_or(false));
    m.set("match_id", *f.get_string("match_id"));
    Json got = srv.handle(m);
    MapLoad ml = map_from_json(*got.find("map"));  // what a client with no local resources does
    REQUIRE(ml.map.has_value());
    CHECK(ml.map->id == "factory-01");
    CHECK(ml.map->width == 14);
}

TEST_CASE("maps: malformed files and duplicate ids are reported by file name; valid maps stay available") {
    TempDir d;
    d.write("a-good.map", with_header(good_header("alpha")));
    d.write("b-dup.map", with_header(good_header("alpha")));                       // duplicate id
    d.write("c-noid.map", with_header("; name: No Id\n"));                         // missing id
    d.write("d-badtile.map", good_header("delta") + "# # # # # # # # #\n# S1 D1 X . . . . #\n");  // unknown token
    d.write("e-badid.map", with_header("; id: bad id!\n; name: Bad\n"));
    d.write("f-players.map", with_header(good_header("foxtrot", "1-9")));
    d.write("g-unreachable.map", good_header("golf") +
            "# # # # # # # # # #\n# S1 D1 # . G1 . G2 . #\n# . . # . . . . . #\n# . . # . . . . . #\n"
            "# . . # . . . . . #\n# . . # . . . . . #\n# S2 D2 # . . . . . #\n# . . # . . . . . #\n# # # # # # # # # #\n");
    d.write("h-ok.map", with_header(good_header("hotel", "2-3")));
    d.write("ignored.txt", "not a map");
    MapDirReport rep = load_map_dir(d.path.string());
    REQUIRE(rep.maps.size() == 2);
    CHECK(rep.maps[0].info.id == "alpha");
    CHECK(rep.maps[0].info.file == "a-good.map");
    CHECK(rep.maps[1].info.id == "hotel");
    CHECK(rep.maps[1].info.max_players == 3);
    auto mentions = [&](const std::string& file, const std::string& what) {
        for (const auto& e : rep.errors) if (e.rfind(file + ": ", 0) == 0 && e.find(what) != std::string::npos) return true;
        return false;
    };
    CHECK(mentions("b-dup.map", "duplicate map id 'alpha' (already defined by a-good.map)"));
    CHECK(mentions("c-noid.map", "missing required metadata '; id"));
    CHECK(mentions("d-badtile.map", "unknown token"));
    CHECK(mentions("e-badid.map", "id 'bad id!'"));
    CHECK(mentions("f-players.map", "players '1-9'"));
    CHECK(mentions("g-unreachable.map", "players"));  // section 8 validation failure names the player count
    for (const auto& e : rep.errors) CHECK(e.find("ignored.txt") == std::string::npos);

    // The server keeps serving the valid maps and exposes the report.
    ManualClock clock;
    ServerConfig cfg;
    cfg.maps_dir = d.path.string();
    GameServer srv(cfg, clock);
    CHECK(srv.list_maps().size() == 2);
    CHECK_FALSE(srv.map_report().errors.empty());
    Json c = req("CREATE_MATCH");
    c.set("map_id", "hotel");
    c.set("player_count", 4);  // hotel supports 2-3 only
    CHECK_FALSE(srv.handle(c).get_bool("ok").value_or(true));
    c.set("player_count", 3);
    CHECK(srv.handle(c).get_bool("ok").value_or(false));

    // A missing directory is reported, not fatal.
    ServerConfig missing;
    missing.maps_dir = (d.path / "nope").string();
    GameServer none(missing, clock);
    CHECK(none.list_maps().empty());
    REQUIRE(none.map_report().errors.size() == 1);
    CHECK(none.map_report().errors[0].find("maps directory not found") != std::string::npos);
}

TEST_CASE("maps: registered maps are immutable and never replaced; running matches keep their own map") {
    ManualClock clock;
    GameServer srv(ServerConfig{}, clock);
    Json c = req("CREATE_MATCH");
    c.set("player_count", 2);
    const std::string id = *srv.handle(c).get_string("match_id");
    Json g = req("GET_MAP");
    g.set("match_id", id);
    const std::string before = srv.handle(g).dump();
    // Registering another map under an existing id is refused (a later runtime-registration feature would
    // add new ids; it can never swap the map of a registered id or of a running match).
    auto other = std::make_shared<const Map>(training01());
    CHECK_FALSE(srv.register_map(other, MapInfo{}));
    CHECK(srv.handle(g).dump() == before);
    ServerConfig empty;
    empty.load_maps = false;
    GameServer bare(empty, clock);
    CHECK(bare.list_maps().empty());
    CHECK(bare.register_map(other, MapInfo{}));
    CHECK(bare.list_maps()[0].name == "training-01");  // name defaults to the id
}

TEST_CASE("maps: the resource directory is found without depending on the working directory") {
    TempDir d;
    fs::create_directories(d.path / "bin");
    fs::create_directories(d.path / "share" / "botched" / "maps");
    const std::string exe = (d.path / "bin" / "botched-tcp-server").string();
    ::unsetenv("BOTCHED_MAPS_DIR");
    // installed layout: <prefix>/bin/exe -> <prefix>/share/botched/maps
    CHECK(fs::equivalent(resolve_maps_dir("", exe), d.path / "share" / "botched" / "maps"));
    // a maps directory next to the executable wins over the installed layout
    fs::create_directories(d.path / "bin" / "resources" / "maps");
    CHECK(fs::equivalent(resolve_maps_dir("", exe), d.path / "bin" / "resources" / "maps"));
    // environment overrides the executable-relative search; an explicit directory overrides everything
    ::setenv("BOTCHED_MAPS_DIR", "/from/env", 1);
    CHECK(resolve_maps_dir("", exe) == "/from/env");
    CHECK(resolve_maps_dir("/explicit", exe) == "/explicit");
    ::unsetenv("BOTCHED_MAPS_DIR");
}

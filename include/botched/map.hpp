// Immutable board definition: parser, validator and canonical serialisation.
//
// Text format: one row per line, whitespace-separated tokens (spec section 7).
//   #  wall            .  floor            O  pit
//   S<n>[dir]  spawn of player n (dir N/E/S/W; default E for odd n, W for even n)
//   D<n>       dock of player n   G<n>  dispenser with id n
//   C<d>[grp]  conveyor, d in > < ^ v, group letter (default A)
//   T[grp]     console, group letter (default A)
//   L<d>       laser emitter, d in > < ^ v
// Blank lines and lines starting with ';' are ignored.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "botched/json.hpp"
#include "botched/types.hpp"

namespace botched {

struct Tile {
    Terrain terrain = Terrain::Floor;
    int owner = 0;       // spawn/dock: player id; dispenser: dispenser id
    Dir dir = Dir::N;    // conveyor / laser direction; spawn: initial heading
    char group = 'A';    // conveyor / console group
};

struct Console {
    std::string id;  // "T001"... assigned in (y,x) scan order; compared lexicographically
    Pos pos;
    char group;
};

struct Dispenser {
    int id;
    Pos pos;
};

struct Map {
    std::string id;
    int width = 0;
    int height = 0;
    std::vector<Tile> tiles;  // row-major

    // Derived indexes, filled by finalize().
    std::array<std::optional<Pos>, kMaxPlayers + 1> spawns{};
    std::array<std::optional<Pos>, kMaxPlayers + 1> docks{};
    std::vector<Dispenser> dispensers;  // ascending id
    std::vector<Console> consoles;      // ascending id
    std::vector<Pos> lasers;            // ascending (y,x)
    std::vector<char> groups;           // ascending, every group referenced by any tile

    bool in_bounds(Pos p) const { return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height; }
    const Tile& tile(Pos p) const { return tiles[static_cast<std::size_t>(p.y * width + p.x)]; }
    Tile& tile(Pos p) { return tiles[static_cast<std::size_t>(p.y * width + p.x)]; }

    // Blocked for robots, pushes, crates and laser rays: out of bounds, wall, laser emitter.
    bool blocked(Pos p) const;
    bool walkable(Pos p) const;  // in bounds and not blocked and not a pit

    void finalize();
};

struct MapLoad {
    std::optional<Map> map;
    std::vector<std::string> errors;
    bool ok() const { return map.has_value() && errors.empty(); }
};

// Parse only; reports syntax errors. Calls finalize() on success.
MapLoad parse_map(std::string_view id, std::string_view text);

struct ValidationOptions {
    int player_count = kMaxPlayers;  // spawn/dock/path checks apply to players 1..player_count
    bool strict_first_party = true;  // reject conveyors that face out of bounds
};

// Section 8 checks. Empty result means valid.
std::vector<std::string> validate_map(const Map& map, const ValidationOptions& opt = {});

// Parse + validate.
MapLoad load_map(std::string_view id, std::string_view text, const ValidationOptions& opt = {});

// Canonical definition for GET_MAP: rows of the same tokens parse_map accepts.
Json map_to_json(const Map& map);
std::string map_to_text(const Map& map);
// Inverse of map_to_json (what a client does with a GET_MAP response). Parse only.
MapLoad map_from_json(const Json& j);

// The section 7 example map.
extern const char* const kTraining01Text;
const Map& training01();

}  // namespace botched

// Character-grid reference renderer. Consumes only the public map definition and a
// public snapshot, like any first-class ASCII client (spec section 10).
#pragma once

#include <string>

#include "botched/json.hpp"
#include "botched/map.hpp"
#include "botched/types.hpp"

namespace botched::sim {

// Cell = 5 chars: terrain, owner/id digit, crate '*', robot digit, heading arrow.
std::string render_board(const Map& map, const Json& snapshot);
std::string render_scores(const Json& snapshot);

// ---- playtest presentation (terminal client) --------------------------------------------------
// Every cell is exactly 3 columns. A robot REPLACES the terrain it stands on (digit, heading arrow,
// carry marker); a loose crate replaces the terrain glyph too. Unicode blocks/arrows and ANSI
// colour are independent options; with both off the board is plain ASCII.
struct Style {
    bool unicode = false;
    bool color = false;
};

std::string render_board_styled(const Map& map, const Json& snapshot, const Style& style);
std::string render_scores_styled(const Json& snapshot, const Style& style);
// Symbol key plus live state: belt groups (normal/reversed), dispenser contents, robot markers.
std::string render_legend(const Map& map, const Json& snapshot, const Style& style);
// "conveyor heading E (group A, normal)", "your dock", "dispenser G2 (crate waiting)", ...
std::string describe_tile(const Map& map, const Json& snapshot, Pos pos, int viewer);

}  // namespace botched::sim

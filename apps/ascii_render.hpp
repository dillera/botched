// Character-grid reference renderer. Consumes only the public map definition and a
// public snapshot, like any first-class ASCII client (spec section 10).
#pragma once

#include <string>

#include "botched/json.hpp"
#include "botched/map.hpp"

namespace botched::sim {

// Cell = 5 chars: terrain, owner/id digit, crate '*', robot digit, heading arrow.
std::string render_board(const Map& map, const Json& snapshot);
std::string render_scores(const Json& snapshot);

}  // namespace botched::sim

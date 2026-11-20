// Semantic validation of a restored world (M0-H1). Used only at the restoration boundary;
// the simulation hot path assumes a state that passed this check or was built by the engine.
#pragma once

#include <string>

#include "botched/map.hpp"
#include "botched/state.hpp"

namespace botched {

// Checks that `s` is a state the engine could actually have produced for `map`:
// player/crate identity and ownership, positions and occupancy, carrying relationships,
// group state, damage/score invariants and round/tick/phase consistency.
// Returns false with a human-readable reason in `error`; never throws, never mutates.
bool validate_world(const GameState& s, const Map& map, int player_count, int rounds, std::string& error);

}  // namespace botched

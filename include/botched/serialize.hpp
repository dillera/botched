// Canonical JSON for world state and events (spec section 9). Key order is fixed, so
// dump() of these values is byte-stable.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "botched/json.hpp"
#include "botched/state.hpp"

namespace botched {

// Adds the world fields (phase, round, tick, players, crates, groups, ...) to `out`
// in canonical order. Match::snapshot() wraps this with match/board/planning fields.
void write_world(const GameState& s, Json& out);

// Inverse of write_world(); accepts any object carrying those fields (a full snapshot).
bool read_world(const Json& in, GameState& s, std::string& error);

Json event_to_json(const Event& e, std::string_view match_id);

std::uint64_t fnv1a64(std::string_view data);
std::string hex64(std::uint64_t v);

}  // namespace botched

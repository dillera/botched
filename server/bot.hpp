// Simulated player, used by the server for bot seats and by the demo clients/simulator.
// Sees only what a real client sees: the map definition and public snapshots.
//
// Planning: a depth-3 search over the player's own orders, scoring each candidate by running the
// real rules engine on a copy of the snapshot's world (other robots assumed to WAIT). That makes
// it conveyor-, pit- and laser-aware without re-implementing any rule. Decisions are
// deliberately imperfect (seeded noise and occasional blunders) but purposeful: collect the
// nearest crate, carry it to the own dock, stay out of pits and laser beams.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "botched/json.hpp"
#include "botched/map.hpp"

namespace botched::sim {

// Small deterministic PRNG (xorshift64*), identical on every platform.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : s_(seed ? seed : 0x9e3779b97f4a7c15ULL) {}
    std::uint64_t next() {
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return s_ * 2685821657736338717ULL;
    }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
private:
    std::uint64_t s_;
};

class Bot {
public:
    Bot(int player_id, const Map& map, std::uint64_t seed) : id_(player_id), map_(map), rng_(seed) {}

    // Plan three order tokens from a public snapshot: head for a crate, then for the own
    // dock, with a little seeded noise so matches differ.
    std::array<std::string, 3> plan(const Json& snapshot);

    // Tuning (percent): 0 noise = best-scoring programme every time.
    void set_blunder_percent(int p) { blunder_percent_ = p; }
    void set_fumble_percent(int p) { fumble_percent_ = p; }

    int id() const { return id_; }

private:
    std::array<std::string, 3> plan_by_path(const Json& snapshot);  // fallback: shortest path, no lookahead

    int id_;
    const Map& map_;
    Rng rng_;
    int blunder_percent_ = 12;
    int fumble_percent_ = 10;
};

}  // namespace botched::sim

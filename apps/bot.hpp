// Simulated player. Sees only what a real client sees: the map definition and public
// snapshots. It never touches engine state.
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

    int id() const { return id_; }

private:
    int id_;
    const Map& map_;
    Rng rng_;
};

}  // namespace botched::sim

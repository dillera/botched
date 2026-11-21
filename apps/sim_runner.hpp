// Runs a complete match headlessly: GameServer + simulated players talking the public
// JSON API, with a manual clock so planning timeouts happen instantly and reproducibly.
#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "botched/json.hpp"

namespace botched::sim {

struct SimOptions {
    int players = 4;
    std::uint64_t seed = 1;
    int rounds = 12;              // fixed length, or the safety cap when target_score > 0
    int target_score = 0;         // 0 = fixed rounds; N > 0 = first to N points
    int flaky_percent = 0;        // chance per bot per round that it never submits (disconnect/lag)
    std::string map_file;         // custom map text file (overrides map_id)
    std::string map_id = "training-01";  // map resource id
    std::string maps_dir;         // empty = default resource directory
    bool verbose = false;         // board + scores after every round
    bool print_events = false;    // one JSON event per line
    std::ostream* out = nullptr;  // where verbose/event output goes (nullptr = silent)
};

struct SimResult {
    bool ok = false;
    std::string error;
    std::string state_hash;   // canonical final snapshot hash
    std::string event_hash;   // hash of the complete event stream
    Json final_snapshot;
    std::vector<int> winners;
    int rounds_played = 0;
    int timeouts = 0;         // programmes replaced by WAIT x3
    int events = 0;
};

SimResult run_simulation(const SimOptions& opt);

}  // namespace botched::sim

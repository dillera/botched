// Deterministic rules engine (spec section 5). Pure functions over (Map, GameState).
#pragma once

#include <vector>

#include "botched/map.hpp"
#include "botched/state.hpp"

namespace botched {

// Programmes indexed by player id - 1; one entry per player.
using Programmes = std::vector<Programme>;

// Players in resolution order for the 1-based absolute tick index (spec 5).
std::vector<int> priority_order(int absolute_tick, int player_count);
inline int absolute_tick(int round, int tick) { return (round - 1) * kRegisters + tick; }

// Initial world: robots on their spawns, groups from the map, no crates, round 0.
GameState make_initial_state(const Map& map, int player_count, int rounds = kDefaultRounds);

// Players sharing the best (score, deliveries, -damage_taken), ascending id.
std::vector<int> compute_winners(const GameState& s);

class Engine {
public:
    Engine(const Map& map, EventLog& log) : map_(map), log_(log) {}

    // Section 6.1: dispenser spawns, retry pending respawns, ROUND_STARTED. round += 1.
    void start_round(GameState& s);

    // Section 5.1 phases 1-5 for tick 1..3 of s.round. Sets s.tick.
    void run_tick(GameState& s, const Programmes& programmes, int tick);

    // After tick 3: respawns, ROUND_ENDED, MATCH_ENDED when the last round is done.
    // Sets phase to Finished after the last round; otherwise leaves the caller to start_round.
    void end_round(GameState& s);

    // Respawn all pending robots in ascending player id order (spec 5.5).
    void process_respawns(GameState& s);

private:
    enum class Cause { Order, Conveyor };

    bool move_robot(GameState& s, int pid, Dir d, Cause cause);
    void resolve_tile(GameState& s, int pid, Pos last_safe);
    void pick_up_and_deliver(GameState& s, int pid);
    void deliver_if_on_dock(GameState& s, int pid);
    void disable_robot(GameState& s, int pid, Pos drop_cell, const char* cause);
    void run_order(GameState& s, int pid, Order o, int slot);
    void toggle(GameState& s, int pid);
    void drop(GameState& s, int pid);
    void phase_conveyors(GameState& s);
    void phase_lasers(GameState& s);
    void phase_disable(GameState& s);

    const Map& map_;
    EventLog& log_;
    std::vector<int> moved_this_phase_;  // robots displaced during the conveyor phase
};

}  // namespace botched

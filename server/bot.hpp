// Simulated player, used by the server for bot seats and by the demo clients/simulator.
// Sees only what a real client sees: the map definition and public snapshots.
//
// Planning: a depth-3 search over the player's own orders, scoring each candidate by running the
// real rules engine on a copy of the snapshot's world (other robots assumed to WAIT). That makes
// it conveyor-, pit-, laser-, trap- and turret-aware without re-implementing any rule.
//
// G4: each bot has a personality (hauler / opportunist / saboteur) and a persistent goal (haul a crate, contest
// a resource, arm a trap, claim a turret, chase a rival) chosen at random from the personality's weights and kept
// until done, impossible, timed out or disrupted. The goal steers the search through a hazard-weighted distance
// field: tar slows, lasers/armed traps/ready turrets cost "hits", and a hit costs more as health runs low, so a
// healthy bot may take a risky shortcut that a wounded one refuses. Sabotage points and owning machinery are
// rewarded. Decisions stay deliberately imperfect (seeded noise, blunders, fumbled orders).
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

// G4: who the bot is. Each personality weighs the three kinds of goal it may pick next, how much damage it is
// willing to risk, and how much it values sabotage points.
enum class Personality { Hauler, Opportunist, Saboteur };
const char* to_string(Personality p);
Personality personality_for(std::uint64_t key);  // deterministic: key % 3

// Server-side tuning only; never serialized as client metadata.
struct Traits {
    int w_haul, w_mixed, w_hunt, risk_pct, sabotage_pct, blunder, fumble, patience;
    bool operator==(const Traits&) const = default;
};
Traits traits_of(Personality);
struct PersonalityBlend {
    // Normalized millionths, computed once. Construction validates finite weights
    // in [0,1], sum within 1e-6; largest remainders break ties H/O/S.
    std::array<int,3> millionths;
    static PersonalityBlend from_weights(const std::array<double,3>&);
    Traits traits() const;
    std::string describe() const;
};
struct NamedBot {
    std::string name;
    PersonalityBlend blend;
};

// What the bot is currently trying to do. It is kept until it completes, becomes impossible, times out or
// something disrupts the plan; the details (target tiles, route) are recomputed from the world every round.
enum class GoalKind {
    None,
    Haul,         // fetch a crate and deliver it to the own dock
    Contest,      // opportunist: the cheapest of a crate, a device to control, or a loaded rival
    ArmTrap,      // walk to a trap's console and TOGGLE: own and arm the trap
    ClaimTurret,  // step onto a turret pad: own the turret
    Chase         // pressure a rival (prefer one carrying a crate), looking for pushes into pits and hazards
};
const char* to_string(GoalKind k);

class Bot {
public:
    Bot(int player_id, const Map& map, std::uint64_t seed) : Bot(player_id, map, seed, personality_for(seed)) {}
    Bot(int player_id, const Map& map, std::uint64_t seed, Personality p);

    Bot(int player_id, const Map& map, std::uint64_t seed, Personality legacy,
        const NamedBot& definition);
    const Traits& traits() const { return traits_; }
    std::string identity() const;

    // Plan three order tokens from a public snapshot. The bot keeps a current goal between rounds, re-evaluates
    // it against the current world at the start of every round (rival positions, hazards, crates), and picks a
    // new goal when the old one completes, becomes impossible, times out, or the last round did not go to plan.
    std::array<std::string, 3> plan(const Json& snapshot);

    // Tuning (percent): 0 noise = best-scoring programme every time. Defaults come from the personality.
    void set_blunder_percent(int p) { blunder_percent_ = p; }
    void set_fumble_percent(int p) { fumble_percent_ = p; }

    int id() const { return id_; }
    // Frozen status compatibility category; named bots use traits_, not this label.
    Personality personality() const { return personality_; }
    GoalKind goal_kind() const { return goal_.kind; }
    int goals_chosen() const { return goals_chosen_; }      // how many times a goal was (re)chosen
    int disruptions() const { return disruptions_; }        // rounds that did not go to plan
    std::string describe() const;                          // e.g. "saboteur: arm trap X1"

    // Diagnostics: when on, plan() records how it reached its orders (situation, why the goal was kept or
    // re-chosen, candidate goals and weights, best programmes and their scores, blunders/fumbles, prediction).
    // Pure bookkeeping: it never touches the rng or any decision.
    void set_explain(bool on) { explain_ = on; }
    const std::string& reasoning() const { return reasoning_; }

private:
    struct Ctx;
    struct Goal {
        GoalKind kind = GoalKind::None;
        int target = 0;       // crate id / trap id / turret id / rival player id
        int age = 0;          // rounds spent on this goal
        int deliveries0 = 0;  // deliveries and sabotage when it was chosen, to spot completion
        int sabotage0 = 0;
    };
    std::array<std::string, 3> plan_by_path(const Json& snapshot);  // fallback: shortest path, no lookahead
    void choose_goal(Ctx& c, GoalKind family, int exclude_crate = 0);
    bool goal_done(const Ctx& c) const;
    bool goal_impossible(const Ctx& c) const;
    std::vector<Pos> goal_targets(const Ctx& c) const;

    void note(const std::string& line) { if (explain_) { reasoning_ += "    "; reasoning_ += line; reasoning_ += '\n'; } }
    std::string goal_text(const Goal& g) const;

    bool explain_ = false;
    std::string reasoning_;
    int id_;
    const Map& map_;
    Rng rng_;
    Personality personality_;
    Traits traits_;
    std::string identity_;
    int blunder_percent_;
    int fumble_percent_;

    Goal goal_;
    int goals_chosen_ = 0;
    int disruptions_ = 0;
    bool have_prediction_ = false;   // what this bot expects to see at the start of next round
    Pos pred_pos_;
    bool pred_carrying_ = false;
    int pred_damage_ = 0;
    bool was_disabled_ = false;
};

}  // namespace botched::sim

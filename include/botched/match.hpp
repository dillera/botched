// One match: world state + planning lifecycle + versioned event log (spec sections 6, 9).
// Contains no clocks, sockets or rendering. The server decides *when* to call
// close_planning(); the outcome never depends on call timing.
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "botched/engine.hpp"
#include "botched/json.hpp"
#include "botched/map.hpp"
#include "botched/state.hpp"

namespace botched {

struct MatchConfig {
    std::string match_id;
    std::shared_ptr<const Map> map;
    int player_count = kMaxPlayers;
    int rounds = kDefaultRounds;
    int event_retention_versions = 512;  // older events answer GET_EVENTS with SNAPSHOT_REQUIRED
};

struct SubmitResult {
    bool ok = false;
    std::string code;     // "OK" or a rejection code
    std::string message;
    int round = 0;
    int revision = 0;
    int state_version = 0;
};

class Match {
public:
    // Builds the initial state and starts round 1 (planning open).
    explicit Match(MatchConfig cfg);

    // ---- commands -------------------------------------------------------------
    // Rejection codes: UNKNOWN_PLAYER, PLANNING_CLOSED, WRONG_ROUND, PROGRAM_LOCKED,
    // INVALID_ORDERS, INVALID_REVISION, STALE_REVISION, REVISION_CONFLICT.
    SubmitResult submit_program(int player, int round, int revision, const std::vector<std::string>& orders,
                                bool ready, const std::string& request_id);

    bool all_ready() const;
    // Lock programmes (missing -> WAIT x3) and publish PLANNING_CLOSED. Planning only.
    bool close_planning();
    // Execute one tick (and, after tick 3, round end + next round start). Executing only.
    bool run_next_tick();
    // close_planning() if needed, then run all remaining ticks of the round.
    bool run_round();
    // Run rounds until the match is finished, using WAIT for anything not submitted.
    void play_to_end();

    void set_planning_deadline(std::string utc) { deadline_utc_ = std::move(utc); }

    // ---- queries --------------------------------------------------------------
    const MatchConfig& config() const { return cfg_; }
    const Map& map() const { return *cfg_.map; }
    const GameState& state() const { return state_; }
    int state_version() const { return version_; }
    bool finished() const { return state_.phase == Phase::Finished; }

    // Public snapshot (spec 9.1). Never contains programmes.
    Json snapshot() const;
    Json status() const;
    std::string state_hash() const;  // fnv1a64 of the canonical snapshot text

    // Events with version > since. nullopt => SNAPSHOT_REQUIRED.
    std::optional<std::vector<Event>> events_since(int since_version) const;
    const std::deque<Event>& all_events() const { return events_; }

    // Full server-side save including secret programmes; restore() resumes exactly.
    Json save() const;
    static std::optional<Match> restore(MatchConfig cfg, const Json& saved, std::string& error);

private:
    struct Entry {
        bool has = false;
        int revision = 0;
        Programme orders{};
        bool ready = false;
    };

    template <class F>
    void publish(F&& body);
    void begin_round_planning();
    void trim_events();

    MatchConfig cfg_;
    GameState state_;
    EventLog log_;
    std::deque<Event> events_;
    int version_ = 0;
    int trimmed_below_ = 0;  // events with version <= this have been discarded
    std::vector<Entry> entries_;
    std::string deadline_utc_;
    std::map<std::pair<int, std::string>, SubmitResult> request_cache_;
};

}  // namespace botched

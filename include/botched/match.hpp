// One match: world state + planning lifecycle + versioned event log (spec sections 6, 9).
// Contains no clocks, sockets or rendering. The server decides *when* to call
// close_planning(); the outcome never depends on call timing.
#pragma once

#include <cstdint>
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

// Request-id idempotency (docs/request-id-contract.md). Every keyed SUBMIT_PROGRAM outcome is
// remembered for the life of the match; memory is bounded by rationing NEW ids per player per
// round instead of evicting old ones.
inline constexpr int kDefaultRequestBudget = 32;  // new request ids per player per round
inline constexpr int kMinRequestBudget = 8;       // configurable range; out-of-range values are clamped
inline constexpr int kMaxRequestBudget = 128;

struct MatchConfig {
    std::string match_id;
    std::shared_ptr<const Map> map;
    int player_count = kMaxPlayers;
    int rounds = kDefaultRounds;
    int event_retention_versions = 512;  // older events answer GET_EVENTS with SNAPSHOT_REQUIRED
    int request_budget_per_round = kDefaultRequestBudget;  // clamped to [kMinRequestBudget, kMaxRequestBudget]
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
    // INVALID_ORDERS, INVALID_REVISION, STALE_REVISION, REVISION_CONFLICT, plus the idempotency
    // refusals REQUEST_ID_REUSED and REQUEST_LIMIT (never recorded; see the contract).
    SubmitResult submit_program(int player, int round, int revision, const std::vector<std::string>& orders,
                                bool ready, const std::string& request_id);

    // Idempotency store introspection (bounded: players x rounds x budget records).
    std::size_t request_record_count() const;
    std::size_t request_store_bytes() const;  // records plus heap held by long ids (excl. ~10% container overhead)
    int request_budget() const { return cfg_.request_budget_per_round; }

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
        Programme orders{Order::Wait, Order::Wait, Order::Wait};  // canonical for "no programme"
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
    // Outcomes a recorded request can have (the reply is rebuilt from this, byte for byte).
    enum class Outcome : std::uint8_t {
        Accepted, Unchanged, PlanningClosed, WrongRound, ProgramLocked, InvalidOrdersCount,
        InvalidOrdersToken, InvalidRevision, StaleRevision, RevisionConflict
    };
    // Exact record of one keyed request: the id verbatim and the payload field by field.
    struct RequestRecord {
        std::string id;
        std::int32_t round = 0;       // payload
        std::int32_t revision = 0;    // payload
        std::int32_t reply_round = 0; // response
        std::int32_t state_version = 0;  // response
        std::uint8_t orders[3] = {0, 0, 0};  // payload: Order value, 0xFE absent, 0xFF unrecognised
        std::uint8_t order_count = 0;        // payload (saturating)
        std::uint8_t outcome = 0;            // Outcome
        bool ready = false;                  // payload
    };
    std::vector<std::deque<RequestRecord>> request_records_;   // per player, first-seen order (deque: no 2x growth slack)
    std::vector<std::vector<int>> budget_used_;                 // [player-1][round]
};

}  // namespace botched

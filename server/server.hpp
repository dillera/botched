// Headless server: match rooms, sessions, planning deadlines and the logical API of
// spec section 9. It speaks JSON values in / JSON values out and knows nothing about
// sockets, SLIP, FujiBus or rendering: a NIO transport adapter wraps handle() later.
//
// Layering:   transport  ->  GameServer (this)  ->  Match / Engine (rules, no clocks)
#pragma once

#include <cstdint>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "botched/json.hpp"
#include "botched/match.hpp"
#include "bot.hpp"
#include "map_loader.hpp"

namespace botched::server {

class Clock {
public:
    virtual ~Clock() = default;
    virtual std::int64_t now_unix_ms() const = 0;
};

class SystemClock final : public Clock {
public:
    std::int64_t now_unix_ms() const override;
};

// Deterministic clock for tests and simulations.
class ManualClock final : public Clock {
public:
    explicit ManualClock(std::int64_t start_ms = 1'795'000'000'000) : now_(start_ms) {}
    std::int64_t now_unix_ms() const override { return now_; }
    void advance_ms(std::int64_t ms) { now_ += ms; }
private:
    std::int64_t now_;
};

// "2026-11-20T20:40:00Z"
std::string format_utc(std::int64_t unix_ms);

// Hard limits on untrusted requests (documented in docs/protocol-limits.md). They are sized
// for constrained clients: the largest legal request (SUBMIT_PROGRAM with a 64-character
// request_id and a 64-character session) is about 350 bytes.
inline constexpr std::size_t kMaxRequestBytes = 2048;
inline constexpr std::size_t kMaxIdLength = 64;      // request_id, session
inline constexpr std::size_t kMaxMatchIdLength = 16;
inline constexpr std::size_t kMaxNameLength = 32;    // request type, map_id
inline constexpr int kMaxPlanningSeconds = 86400;

// Parser limits for one request line: flat object, one short array.
ParseLimits request_parse_limits();

struct ServerConfig {
    int default_planning_seconds = 60;  // spec 2 suggestion; 0 disables the timer
    int max_matches = 16;               // simultaneously hosted matches (spec 14: deployment setting)
    int request_budget_per_round = kDefaultRequestBudget;  // new request ids per player per round (8-128)
    // Session tokens come from OS entropy (std::random_device, 256 bits). Set this ONLY for
    // reproducible tests/simulations: it makes tokens predictable.
    std::optional<std::uint64_t> deterministic_token_seed;
    // Seeds the server-managed bot seats (CREATE_MATCH "bots"). Bot decisions depend only on this seed,
    // the match id, the seat and the game state, so a match with the same human inputs replays identically.
    std::uint64_t bot_seed = 1;
    // Optional diagnostics: when set, every bot seat writes one line per round (personality, goal, chosen
    // orders) here as the round opens. Read-only; it never affects play or determinism.
    std::ostream* bot_log = nullptr;
    // Map resources. Empty maps_dir = resolve_maps_dir() (env, executable-relative, compile-time default).
    // load_maps=false starts with an empty registry (tests that register their own maps).
    std::string maps_dir;
    bool load_maps = true;
};

class GameServer {
public:
    GameServer(ServerConfig cfg, const Clock& clock);

    // Adds a map to the registry. Maps are immutable (shared_ptr<const Map>) and matches keep their own
    // reference, so registering never affects running matches. Returns false (and changes nothing)
    // when the id is already registered; a missing name defaults to the id.
    bool register_map(std::shared_ptr<const Map> map, MapInfo info = {});
    // What was found when the maps directory was loaded at construction (empty if load_maps=false).
    const MapDirReport& map_report() const { return map_report_; }
    std::vector<MapInfo> list_maps() const;  // ascending id

    // Dispatches one request. Never throws; malformed input yields an ERROR response.
    //   Lobby/admin:  LIST_MAPS, CREATE_MATCH (optional map_id), JOIN_MATCH, START_MATCH
    //   Spec 9.2:     SUBMIT_PROGRAM, GET_SNAPSHOT, GET_EVENTS, GET_MAP, GET_MATCH_STATUS
    Json handle(const Json& request);
    Json handle_text(std::string_view line);  // parse + handle + (caller dumps)

    // Applies planning deadlines. Call whenever time may have advanced.
    void poll();

    // Direct access for hosts/tests.
    const Match* find_match(const std::string& id) const;

private:
    struct Room {
        std::string id;
        std::shared_ptr<const Map> map;
        int capacity = 0;
        int planning_seconds = 0;
        int rounds = kDefaultRounds;      // fixed length, or the safety cap when target_score > 0
        int target_score = 0;             // 0 = fixed-round mode
        int bots = 0;                     // seats played by the server (always the highest player ids)
        std::vector<std::string> tokens;  // human seats only: index = player id - 1
        std::vector<std::unique_ptr<sim::Bot>> ai;  // created at start; ai[i] plays player (humans + 1 + i)
        std::unique_ptr<Match> match;
        std::int64_t deadline_ms = 0;     // 0 = none
    };

    Json list_maps_request(const Json& req);
    Json create(const Json& req);
    Json join(const Json& req);
    Json start(const Json& req);
    Json submit(const Json& req);
    Json get_snapshot(const Json& req);
    Json get_events(const Json& req);
    Json get_map(const Json& req);
    Json get_status(const Json& req);

    Room* room_for(const Json& req, Json& err);
    bool start_room(Room& r, std::string& error);
    void open_planning(Room& r);
    void plan_bots(Room& r);
    void advance(Room& r);

    ServerConfig cfg_;
    const Clock& clock_;
    std::map<std::string, LoadedMap> maps_;
    MapDirReport map_report_;
    std::map<std::string, Room> rooms_;
    int next_room_ = 1;
    std::uint64_t token_counter_ = 0;
    std::string make_token(const std::string& room, int pid);
};

Json make_error(std::string code, std::string message, const Json* request = nullptr);

}  // namespace botched::server

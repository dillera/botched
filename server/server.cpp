#include "server.hpp"

#include <chrono>
#include <charconv>
#include <limits>
#include <algorithm>
#include <random>
#include <cstdio>
#include <string_view>

#include "ascii_render.hpp"
#include "botched/serialize.hpp"

namespace botched::server {

std::int64_t SystemClock::now_unix_ms() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string format_utc(std::int64_t unix_ms) {
    std::int64_t secs = unix_ms / 1000;
    std::int64_t days = secs / 86400;
    std::int64_t rem = secs % 86400;
    if (rem < 0) { rem += 86400; --days; }
    // Civil-from-days (Howard Hinnant), proleptic Gregorian.
    std::int64_t z = days + 719468;
    std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    std::int64_t doe = z - era * 146097;
    std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t y = yoe + era * 400;
    std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    std::int64_t mp = (5 * doy + 2) / 153;
    std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
    std::int64_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    char buf[96];
    std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lldZ", static_cast<long long>(y),
                  static_cast<long long>(m), static_cast<long long>(d), static_cast<long long>(rem / 3600),
                  static_cast<long long>(rem % 3600 / 60), static_cast<long long>(rem % 60));
    return buf;
}

ParseLimits request_parse_limits() {
    ParseLimits l;
    l.max_bytes = kMaxRequestBytes;
    l.max_depth = 4;
    l.max_string = 256;
    l.max_array = 8;
    l.max_members = 16;
    l.max_nodes = 64;
    return l;
}

namespace {

// Identifier alphabet: letters, digits and . _ : - (all ASCII, no spaces or controls).
bool token_ok(const std::string& s, std::size_t max_len) {
    if (s.empty() || s.size() > max_len) return false;
    for (unsigned char c : s) {
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' ||
                  c == '_' || c == ':' || c == '-';
        if (!ok) return false;
    }
    return true;
}

constexpr std::int64_t kInt32Min = -2147483647LL - 1;
constexpr std::int64_t kInt32Max = 2147483647LL;

// Field readers. A field that is present must have the right type and range: it is never
// silently replaced by a default, and a 64-bit value is never narrowed into a valid-looking
// 32-bit one. On failure they fill `err` with a BAD_REQUEST and return false.
bool int_field(const Json& req, const char* key, std::int64_t lo, std::int64_t hi, bool required,
               std::int64_t fallback, std::int64_t& out, Json& err) {
    const Json* j = req.find(key);
    if (!j) {
        if (required) { err = make_error("BAD_REQUEST", std::string(key) + " is required", &req); return false; }
        out = fallback;
        return true;
    }
    if (!j->is_int() || j->as_int() < lo || j->as_int() > hi) {
        err = make_error("BAD_REQUEST",
                         std::string(key) + " must be an integer in [" + std::to_string(lo) + "," + std::to_string(hi) + "]", &req);
        return false;
    }
    out = j->as_int();
    return true;
}

bool bool_field(const Json& req, const char* key, bool fallback, bool& out, Json& err) {
    const Json* j = req.find(key);
    if (!j) { out = fallback; return true; }
    if (!j->is_bool()) { err = make_error("BAD_REQUEST", std::string(key) + " must be a boolean", &req); return false; }
    out = j->as_bool();
    return true;
}

// Optional token-shaped string. `allow_empty` lets "" through (request_id: no idempotency key).
bool token_field(const Json& req, const char* key, std::size_t max_len, bool allow_empty, const std::string& fallback,
                 std::string& out, Json& err) {
    const Json* j = req.find(key);
    if (!j) { out = fallback; return true; }
    if (!j->is_string() || !((allow_empty && j->as_string().empty()) || token_ok(j->as_string(), max_len))) {
        err = make_error("BAD_REQUEST",
                         std::string(key) + " must be 1-" + std::to_string(max_len) + " characters of [A-Za-z0-9._:-]", &req);
        return false;
    }
    out = j->as_string();
    return true;
}

}  // namespace

Json make_error(std::string code, std::string message, const Json* request) {
    Json j = Json::object();
    j.set("type", "ERROR");
    j.set("ok", false);
    j.set("code", std::move(code));
    j.set("message", std::move(message));
    if (request) {
        // Echo only well-formed identifiers: never reflect arbitrary or oversized client text.
        if (auto t = request->get_string("type"); t && token_ok(*t, kMaxNameLength)) j.set("request_type", *t);
        if (auto id = request->get_string("request_id"); id && token_ok(*id, kMaxIdLength)) j.set("request_id", *id);
    }
    return j;
}

GameServer::GameServer(ServerConfig cfg, const Clock& clock) : cfg_(cfg), clock_(clock) {
    if (cfg_.load_maps) {
        map_report_ = load_map_dir(resolve_maps_dir(cfg_.maps_dir));
        for (const auto& lm : map_report_.maps) register_map(lm.map, lm.info);
    }
}

bool GameServer::register_map(std::shared_ptr<const Map> map, MapInfo info) {
    info.id = map->id;
    if (info.name.empty()) info.name = info.id;
    if (maps_.count(info.id)) return false;
    LoadedMap lm;
    lm.info = std::move(info);
    lm.map = std::move(map);
    maps_.emplace(lm.info.id, std::move(lm));
    return true;
}

std::vector<MapInfo> GameServer::list_maps() const {
    std::vector<MapInfo> out;
    for (const auto& kv : maps_) out.push_back(kv.second.info);
    return out;
}

Json GameServer::list_maps_request(const Json&) {
    Json arr = Json::array();
    for (const auto& kv : maps_) {  // std::map: ascending id, deterministic
        const MapInfo& i = kv.second.info;
        Json m = Json::object();
        m.set("map_id", i.id);
        m.set("name", i.name);
        m.set("description", i.description);
        m.set("min_players", i.min_players);
        m.set("max_players", i.max_players);
        arr.push(std::move(m));
    }
    Json out = Json::object();
    out.set("type", "MAPS");
    out.set("ok", true);
    out.set("maps", std::move(arr));
    return out;
}

const Match* GameServer::find_match(const std::string& id) const {
    auto it = rooms_.find(id);
    return it == rooms_.end() ? nullptr : it->second.match.get();
}

Json GameServer::handle_text(std::string_view line) {
    if (line.size() > kMaxRequestBytes)
        return make_error("REQUEST_TOO_LARGE", "request exceeds " + std::to_string(kMaxRequestBytes) + " bytes");
    auto j = Json::parse(line, request_parse_limits());
    if (!j || !j->is_object()) return make_error("BAD_REQUEST", "request is not a valid JSON object within limits");
    return handle(*j);
}

Json GameServer::handle(const Json& req) {
    poll();
    auto type = req.get_string("type");
    if (!type || !token_ok(*type, kMaxNameLength)) return make_error("BAD_REQUEST", "missing or malformed request type", &req);
    Json out;
    if (*type == "LIST_MAPS") out = list_maps_request(req);
    else if (*type == "CREATE_MATCH") out = create(req);
    else if (*type == "JOIN_MATCH") out = join(req);
    else if (*type == "START_MATCH") out = start(req);
    else if (*type == "SUBMIT_PROGRAM") out = submit(req);
    else if (*type == "GET_SNAPSHOT") out = get_snapshot(req);
    else if (*type == "GET_EVENTS") out = get_events(req);
    else if (*type == "GET_MAP") out = get_map(req);
    else if (*type == "GET_MATCH_STATUS") out = get_status(req);
    else return make_error("UNKNOWN_REQUEST", "unknown request type", &req);
    poll();  // a request may have changed deadlines or completed planning
    return out;
}

GameServer::Room* GameServer::room_for(const Json& req, Json& err) {
    const Json* mj = req.find("match_id");
    if (!mj || !mj->is_string() || !token_ok(mj->as_string(), kMaxMatchIdLength)) {
        err = make_error("BAD_REQUEST", "match_id is required: 1-16 characters of [A-Za-z0-9._:-]", &req);
        return nullptr;
    }
    const std::string* id = &mj->as_string();
    auto it = rooms_.find(*id);
    if (it == rooms_.end()) { err = make_error("UNKNOWN_MATCH", "no such match", &req); return nullptr; }
    return &it->second;
}

// ------------------------------------------------------------------ lobby

Json GameServer::create(const Json& req, bool validate_only) {
    Json err;
    std::string map_id;
    std::int64_t players = 0, rounds = 0, secs = 0;
    if (!token_field(req, "map_id", kMaxNameLength, false, "training-01", map_id, err)) return err;
    if (!int_field(req, "player_count", 2, kMaxPlayers, false, kMaxPlayers, players, err)) return err;
    std::int64_t target = 0;
    if (!int_field(req, "target_score", 0, kMaxTargetScore, false, 0, target, err)) return err;
    // Fixed-round mode (no target): "rounds" is the length. Score-target mode: it is the safety cap.
    if (!int_field(req, "rounds", 1, kMaxRounds, false, target > 0 ? kDefaultRoundCap : kDefaultRounds, rounds, err)) return err;
    std::int64_t bots = 0;
    if (!int_field(req, "bots", 0, kMaxPlayers - 1, false, 0, bots, err)) return err;
    if (bots > players - 1) return make_error("BAD_REQUEST", "at least one seat must be left for a human player", &req);
    if (!int_field(req, "planning_seconds", 0, kMaxPlanningSeconds, false, cfg_.default_planning_seconds, secs, err))
        return err;
    auto mit = maps_.find(map_id);
    if (mit == maps_.end()) return make_error("UNKNOWN_MAP", "no such map", &req);
    if (players < mit->second.info.min_players || players > mit->second.info.max_players)
        return make_error("BAD_REQUEST", "map " + map_id + " supports " + std::to_string(mit->second.info.min_players) + "-" +
                                             std::to_string(mit->second.info.max_players) + " players", &req);
    if (static_cast<int>(rooms_.size()) >= cfg_.max_matches)
        return make_error("TOO_MANY_MATCHES", "this server hosts at most " + std::to_string(cfg_.max_matches) + " matches", &req);
    ValidationOptions vo;
    vo.player_count = static_cast<int>(players);
    auto errors = validate_map(*mit->second.map, vo);
    if (!errors.empty()) return make_error("INVALID_MAP", errors.front(), &req);

    if (validate_only) { Json ok = Json::object(); ok.set("ok",true); return ok; }
    Room r;
    r.id = "m" + std::to_string(next_room_++);
    r.map = mit->second.map;
    r.capacity = static_cast<int>(players);
    r.bots = static_cast<int>(bots);
    r.rounds = static_cast<int>(rounds);
    r.target_score = static_cast<int>(target);
    r.planning_seconds = static_cast<int>(secs);
    std::string id = r.id;
    rooms_.emplace(id, std::move(r));
    Json out = Json::object();
    out.set("type", "MATCH_CREATED");
    out.set("ok", true);
    out.set("match_id", id);
    out.set("player_count", players);
    out.set("bots", bots);
    out.set("rounds", rounds);
    out.set("target_score", target);
    return out;
}

bool GameServer::provision_pool(const std::vector<Offering>& offerings, std::ostream& report, std::string& error) {
    if (!rooms_.empty()) { error = "pool provisioning requires an empty server"; return false; }
    if (cfg_.max_matches < 1) { error = "max_concurrent_matches must be positive"; return false; }
    uint64_t total = 0;
    std::vector<Json> requests;
    for (const auto& o : offerings) {
        if (o.instances < 1 || uint64_t(o.instances) > uint64_t(cfg_.max_matches) - total) {
            error = "offering " + o.id + ": instances exceed max_concurrent_matches"; return false;
        }
        total += o.instances;
        Json req = o.rules;
        auto map_id = req.get_string("map_id").value_or("");
        auto map = maps_.find(map_id);
        if (map == maps_.end()) { error = "offering " + o.id + ": unknown map " + map_id; return false; }
        if (!req.find("player_count")) {
            if (map->second.info.min_players != map->second.info.max_players) {
                error = "offering " + o.id + ": player_count required for a map with a capacity range"; return false;
            }
            req.set("player_count", map->second.info.min_players);
        }
        req.set("bots", int(o.bots.size()));
        auto checked = create(req, true);
        if (!checked.get_bool("ok").value_or(false)) {
            error = "offering " + o.id + ": " + checked.get_string("message").value_or(checked.dump()); return false;
        }
        requests.push_back(std::move(req));
    }
    // Every declaration passed the same validator used by CREATE_MATCH. No listeners
    // or clients exist yet; capacity was checked for the complete batch above.
    for (size_t i=0; i<offerings.size(); ++i) {
        const auto& o=offerings[i];
        for (int instance=1; instance<=o.instances; ++instance) {
            auto made=create(requests[i]);
            auto id=made.get_string("match_id").value();
            auto& r=rooms_.at(id); r.offering=o.id; r.named_bots=o.bots;
            Json line=Json::object();
            line.set("offering",o.id); line.set("name",o.name); line.set("instance",instance);
            line.set("match_id",id); line.set("map_id",r.map->id); line.set("player_count",r.capacity);
            line.set("bots",r.bots); line.set("state","LOBBY");
            Json names=Json::array(), blends=Json::array();
            for(const auto& bot:o.bots) { names.push(bot.name); blends.push(bot.blend.describe()); }
            line.set("bot_names",names); line.set("bot_blends",blends);
            report << "pool " << line.dump() << '\n';
        }
    }
    report << "pool warning: S1 retains finished matches; no recycling. Configured and ad-hoc rooms share max_concurrent_matches="
           << cfg_.max_matches << '\n' << std::flush;
    return true;
}

std::string GameServer::make_token(const std::string& room, int pid) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    if (cfg_.deterministic_token_seed) {
        std::string seed = std::to_string(*cfg_.deterministic_token_seed) + ":" + std::to_string(++token_counter_) +
                           ":" + room + ":" + std::to_string(pid);
        return hex64(fnv1a64(seed)) + hex64(fnv1a64(seed + "/2"));
    }
    std::random_device rd;
    for (int i = 0; i < 8; ++i) {  // 8 x 32 bits = 256 bits
        std::uint32_t v = rd();
        for (int sh = 28; sh >= 0; sh -= 4) out += hex[(v >> sh) & 15];
    }
    return out;
}

namespace {
// Length-independent-ish constant-time equality for session tokens.
bool token_equal(const std::string& a, const std::string& b) {
    unsigned diff = a.size() ^ b.size();
    for (std::size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i % (b.empty() ? 1 : b.size())]);
    return diff == 0;
}
}  // namespace

void GameServer::visit_map_info(const std::function<void(const MapInfo &)> &visitor) const {
    for (const auto &[id, loaded] : maps_)
        visitor(loaded.info);
}
const Map *GameServer::registered_map(const std::string &id) const {
    auto i = maps_.find(id);
    return i == maps_.end() ? nullptr : i->second.map.get();
}
const Map *GameServer::room_map(const std::string &id) const {
    auto i = rooms_.find(id);
    return i == rooms_.end() ? nullptr : i->second.map.get();
}
std::string GameServer::check_seat(const std::string &id, int player, const std::string &token) const {
    auto i = rooms_.find(id);
    if (i == rooms_.end())
        return "UNKNOWN_MATCH";
    const auto &r = i->second;
    int count = r.match ? r.match->config().player_count : r.capacity;
    if (player < 1 || player > count)
        return "UNKNOWN_PLAYER";
    if (player > (int)r.tokens.size() || !token_equal(token, r.tokens[(size_t)player - 1]))
        return "UNAUTHORIZED";
    return {};
}
std::pair<std::uint32_t, bool> GameServer::numeric_key_hint(const std::string &id, int player) const {
    auto i = rooms_.find(id);
    if (i == rooms_.end() || player < 1 || player > 4)
        return {0, false};
    auto n = i->second.numeric_high[(size_t)player - 1];
    if (!n)
        return {0, false};
    return *n == UINT32_MAX ? std::pair<std::uint32_t, bool>{0, true}
                            : std::pair<std::uint32_t, bool>{*n + 1, false};
}
void GameServer::observe_history(Room &r) {
    if (!r.match)
        return;
    const auto &events = r.match->all_events();
    // At most five new event-bearing publications per server action; retention is 512.
    // Track version numbers only, so a binary query never copies the full retained event log.
    std::int64_t first = events.empty() ? static_cast<std::int64_t>(r.match->state_version()) + 1
                                        : events.front().state_version;
    while (!r.observed_event_versions.empty() && r.observed_event_versions.front() < first) {
        r.observed_trimmed_below = r.observed_event_versions.front();
        r.observed_event_versions.pop_front();
    }
    int last =
        r.observed_event_versions.empty() ? r.observed_trimmed_below : r.observed_event_versions.back();
    for (const auto &e : events)
        if (e.state_version > last) {
            r.observed_event_versions.push_back(e.state_version);
            last = e.state_version;
        }
}
bool GameServer::retained_events_available(const std::string &id, int since) const {
    auto i = rooms_.find(id);
    return i != rooms_.end() && i->second.match && since >= i->second.observed_trimmed_below;
}

Json GameServer::join(const Json &req) {
    Json err;
    Room* r = room_for(req, err);
    if (!r) return err;
    if (r->match) return make_error("ALREADY_STARTED", "match already started", &req);
    const int human_seats = r->capacity - r->bots;
    if (static_cast<int>(r->tokens.size()) >= human_seats) return make_error("MATCH_FULL", "no free slot", &req);
    int pid = static_cast<int>(r->tokens.size()) + 1;
    std::string token = make_token(r->id, pid);
    r->tokens.push_back(token);
    Json out = Json::object();
    out.set("type", "JOINED");
    out.set("ok", true);
    out.set("match_id", r->id);
    out.set("player_id", pid);
    out.set("session", token);
    if (static_cast<int>(r->tokens.size()) == human_seats) {
        std::string e;
        if (!start_room(*r, e)) return make_error("START_FAILED", e, &req);
        out.set("started", true);
    } else {
        out.set("started", false);
    }
    return out;
}

bool GameServer::start_room(Room& r, std::string& error) {
    if (r.match) { error = "already started"; return false; }
    const int humans = static_cast<int>(r.tokens.size());
    const int joined = humans + r.bots;  // bot seats take the ids after the humans
    if (!r.offering.empty() && joined != r.capacity) {
        error = "configured offering waits for all human seats"; return false;
    }
    if (humans < 1) { error = "at least one human player required"; return false; }
    if (joined < 2) { error = "at least two players required"; return false; }
    if (auto mi = maps_.find(r.map->id); mi != maps_.end() && joined < mi->second.info.min_players) {
        error = "map " + r.map->id + " needs at least " + std::to_string(mi->second.info.min_players) + " players";
        return false;
    }
    ValidationOptions vo;
    vo.player_count = joined;
    auto errors = validate_map(*r.map, vo);
    if (!errors.empty()) { error = errors.front(); return false; }
    MatchConfig mc;
    mc.match_id = r.id;
    mc.map = r.map;
    mc.player_count = joined;  // unoccupied slots are disabled
    mc.rounds = r.rounds;
    mc.target_score = r.target_score;
    mc.request_budget_per_round = cfg_.request_budget_per_round;
    r.match = std::make_unique<Match>(std::move(mc));
    r.ai.clear();
    // Legacy numeric bots: the seats cycle hauler -> opportunist -> saboteur from a per-match seeded starting point.
    const std::uint64_t personality_base = (fnv1a64(r.id) ^ (cfg_.bot_seed * 0x9e3779b97f4a7c15ULL)) >> 8;
    for (int i = 0; i < r.bots; ++i) {
        const int pid = humans + 1 + i;
        const std::uint64_t seed = fnv1a64(r.id + ":" + std::to_string(pid)) ^ (cfg_.bot_seed * 0x9e3779b97f4a7c15ULL);
        const auto legacy = sim::personality_for(personality_base + static_cast<std::uint64_t>(i));
        if (r.named_bots.empty()) r.ai.push_back(std::make_unique<sim::Bot>(pid, *r.map, seed, legacy));
        else r.ai.push_back(std::make_unique<sim::Bot>(pid, *r.map, seed, r.named_bots[i]));
    }
    if (cfg_.trace) {
        std::ostream& t = *cfg_.trace;
        t << "\n##### MATCH " << r.id << " START: map " << r.map->id << ", " << joined << " seats (" << humans << " human, " << r.bots
          << " bot), " << r.rounds << " rounds" << (r.target_score ? ", target score " + std::to_string(r.target_score) : std::string())
          << ", planning " << r.planning_seconds << " s\n";
        for (auto& b : r.ai) {
            b->set_explain(true);
            t << "  P" << b->id() << " is a bot: " << b->identity() << "\n";
        }
        t << std::flush;
    }
    open_planning(r);
    return true;
}

Json GameServer::start(const Json& req) {
    Json err;
    Room* r = room_for(req, err);
    if (!r) return err;
    std::string e;
    if (!start_room(*r, e)) return make_error("START_FAILED", e, &req);
    Json out = Json::object();
    out.set("type", "MATCH_STARTED");
    out.set("ok", true);
    out.set("match_id", r->id);
    out.set("player_count", r->match->config().player_count);
    return out;
}

// ------------------------------------------------------------------ orchestration

void GameServer::open_planning(Room& r) {
    if (r.match->finished() || r.planning_seconds <= 0) {
        r.deadline_ms = 0;
        r.match->set_planning_deadline("");
    } else {
        r.deadline_ms = clock_.now_unix_ms() + static_cast<std::int64_t>(r.planning_seconds) * 1000;
        r.match->set_planning_deadline(format_utc(r.deadline_ms));
    }
    plan_bots(r);
}

// Bot seats submit ordinary locked-in programmes through Match::submit_program, exactly like a human
// client would, as soon as a round opens. They see only the public snapshot.
void GameServer::plan_bots(Room& r) {
    if (r.ai.empty() || r.match->finished()) return;
    const Json snap = r.match->snapshot();
    const int round = r.match->state().round;
    if (cfg_.trace) {
        *cfg_.trace << "\n=== match " << r.id << " round " << round << " planning opens ===\n"
                    << sim::render_board(*r.map, snap) << sim::render_scores(snap) << std::flush;
    }
    for (auto& bot : r.ai) {
        auto prog = bot->plan(snap);
        if (cfg_.trace) {
            *cfg_.trace << "  P" << bot->id() << " [" << bot->describe() << "] reasoning:\n" << bot->reasoning()
                        << "  P" << bot->id() << " submits: " << prog[0] << ' ' << prog[1] << ' ' << prog[2] << '\n' << std::flush;
        }
        if (cfg_.bot_log) {
            *cfg_.bot_log << "[bot] match " << r.id << " round " << round << " P" << bot->id() << " "
                          << bot->describe() << " -> " << prog[0] << ' ' << prog[1] << ' '
                          << prog[2] << " (goals " << bot->goals_chosen() << ", disruptions "
                          << bot->disruptions() << ")\n" << std::flush;
        }
        r.match->submit_program(bot->id(), round, 1, {prog[0], prog[1], prog[2]}, true,
                                "bot-p" + std::to_string(bot->id()) + "-r" + std::to_string(round));
    }
}

void GameServer::advance(Room& r) {
    const int before_version = r.match->state_version();
    if (cfg_.trace) {
        const GameState& st = r.match->state();
        *cfg_.trace << "--- match " << r.id << " round " << st.round << " executes (planning closed)";
        *cfg_.trace << "\n";
    }
    r.match->run_round();  // close planning (missing -> WAIT x3), execute, start next round
    if (cfg_.trace) {
        auto ev = r.match->events_since(before_version);
        *cfg_.trace << "--- events of that round (" << (ev ? ev->size() : 0) << "):\n";
        if (ev) for (const Event& e : *ev) *cfg_.trace << "  " << event_to_json(e, r.id).dump() << '\n';
        if (r.match->finished()) {
            *cfg_.trace << "\n##### MATCH " << r.id << " FINISHED\n"
                        << sim::render_board(*r.map, r.match->snapshot()) << sim::render_scores(r.match->snapshot());
        }
        *cfg_.trace << std::flush;
    }
    open_planning(r);
}

void GameServer::poll() {
    const std::int64_t now = clock_.now_unix_ms();
    for (auto& kv : rooms_) {
        Room& r = kv.second;
        if (!r.match) continue;
        if (!r.match->finished() && r.deadline_ms != 0 && now >= r.deadline_ms) advance(r);
        observe_history(r);
    }
}

// ------------------------------------------------------------------ spec 9.2 API

Json GameServer::submit(const Json& req) {
    Json err;
    Room* r = room_for(req, err);
    if (!r) return err;
    if (!r->match) return make_error("NOT_STARTED", "match has not started", &req);
    Json err2;
    std::int64_t player = 0, round = 0, revision = 0;
    if (!int_field(req, "player_id", kInt32Min, kInt32Max, true, 0, player, err2)) return err2;
    if (!int_field(req, "round", kInt32Min, kInt32Max, true, 0, round, err2)) return err2;
    if (!int_field(req, "revision", kInt32Min, kInt32Max, true, 0, revision, err2)) return err2;
    bool ready = false;
    std::string request_id;
    if (!bool_field(req, "ready", false, ready, err2)) return err2;
    if (!token_field(req, "request_id", kMaxIdLength, true, "", request_id, err2)) return err2;
    const Json* orders = req.find("orders");
    if (!orders || !orders->is_array()) return make_error("BAD_REQUEST", "orders must be an array", &req);
    if (player < 1 || player > r->match->config().player_count)
        return make_error("UNKNOWN_PLAYER", "no such player slot", &req);
    const Json* sj = req.find("session");
    if (player > static_cast<std::int64_t>(r->tokens.size()))  // bot seats cannot be driven by clients
        return make_error("UNAUTHORIZED", "session does not own this player slot", &req);
    if (!sj || !sj->is_string() || !token_equal(sj->as_string(), r->tokens[static_cast<std::size_t>(player - 1)]))
        return make_error("UNAUTHORIZED", "session does not own this player slot", &req);
    if (static_cast<int>(orders->items().size()) != kRegisters)
        return make_error("INVALID_ORDERS", "exactly 3 orders required", &req);
    std::vector<std::string> toks;
    for (const auto& o : orders->items()) {
        if (!o.is_string()) return make_error("INVALID_ORDERS", "orders must be strings", &req);
        toks.push_back(o.as_string());
    }

    SubmitResult res = r->match->submit_program(static_cast<int>(player), static_cast<int>(round),
                                                static_cast<int>(revision), toks, ready, request_id);
    if (request_id.starts_with("b:")) {
        std::uint32_t value = 0;
        auto parsed = std::from_chars(request_id.data() + 2, request_id.data() + request_id.size(), value);
        if (parsed.ec == std::errc{} && parsed.ptr == request_id.data() + request_id.size() &&
            request_id == "b:" + std::to_string(value)) {
            auto &high = r->numeric_high[(size_t)player - 1];
            if (!high || value > *high)
                high = value;
        }
    }
    Json out = Json::object();
    out.set("type", "SUBMIT_PROGRAM");
    out.set("ok", res.ok);
    out.set("code", res.code);
    out.set("message", res.message);
    out.set("match_id", r->id);
    out.set("player_id", static_cast<int>(player));
    out.set("round", static_cast<int>(round));
    out.set("revision", static_cast<int>(revision));
    out.set("state_version", res.state_version);
    if (!request_id.empty()) out.set("request_id", request_id);
    if (cfg_.trace) {
        *cfg_.trace << "  human P" << player << " submit round " << round << " rev " << revision << ": ";
        for (const auto& t : toks) *cfg_.trace << t << ' ';
        *cfg_.trace << (ready ? "(ready)" : "(draft)") << " -> " << res.code << '\n' << std::flush;
    }
    if (res.ok && r->match->all_ready()) advance(*r);
    return out;
}

Json GameServer::get_snapshot(const Json& req) {
    Json err;
    Room* r = room_for(req, err);
    if (!r) return err;
    if (!r->match) return make_error("NOT_STARTED", "match has not started", &req);
    Json out = Json::object();
    out.set("type", "SNAPSHOT");
    out.set("ok", true);
    out.set("snapshot", r->match->snapshot());
    return out;
}

Json GameServer::get_events(const Json& req) {
    Json err;
    Room* r = room_for(req, err);
    if (!r) return err;
    if (!r->match) return make_error("NOT_STARTED", "match has not started", &req);
    Json err2;
    std::int64_t since = 0;
    if (!int_field(req, "since_version", kInt32Min, kInt32Max, true, 0, since, err2)) return err2;
    auto ev = r->match->events_since(static_cast<int>(since));
    if (!ev) return make_error("SNAPSHOT_REQUIRED", "requested events are no longer retained", &req);
    Json arr = Json::array();
    for (const auto& e : *ev) arr.push(event_to_json(e, r->id));
    Json out = Json::object();
    out.set("type", "EVENTS");
    out.set("ok", true);
    out.set("match_id", r->id);
    out.set("state_version", r->match->state_version());
    out.set("events", std::move(arr));
    return out;
}

Json GameServer::get_map(const Json& req) {
    Json err;
    Room* r = room_for(req, err);
    if (!r) return err;
    Json out = Json::object();
    out.set("type", "MAP");
    out.set("ok", true);
    out.set("match_id", r->id);
    out.set("map", map_to_json(*r->map));
    return out;
}

Json GameServer::get_status(const Json& req) {
    Json err;
    Room* r = room_for(req, err);
    if (!r) return err;
    Json out = Json::object();
    out.set("type", "MATCH_STATUS");
    out.set("ok", true);
    if (r->match) {
        Json st = r->match->status();
        st.set("bots", r->bots);
        Json ids = Json::array();
        for (const auto& b : r->ai) ids.push(b->id());
        st.set("bot_players", std::move(ids));
        // Frozen v1 compatibility metadata: named blends report the dominant original
        // weight (ties H/O/S). Decisions use full traits; names/weights stay server-side.
        Json kinds = Json::array();
        for (const auto& b : r->ai) kinds.push(sim::to_string(b->personality()));
        st.set("bot_personalities", std::move(kinds));
        out.set("status", std::move(st));
    } else {
        Json st = Json::object();
        st.set("match_id", r->id);
        st.set("phase", "LOBBY");
        st.set("joined", static_cast<int>(r->tokens.size()) + r->bots);  // seats filled, bots included
        st.set("capacity", r->capacity);
        st.set("bots", r->bots);
        st.set("rounds_total", r->rounds);
        st.set("target_score", r->target_score);
        out.set("status", std::move(st));
    }
    return out;
}

}  // namespace botched::server

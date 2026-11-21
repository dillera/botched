#include "server.hpp"

#include <chrono>
#include <random>
#include <cstdio>
#include <string_view>

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
    register_map(std::make_shared<const Map>(training01()));
}

void GameServer::register_map(std::shared_ptr<const Map> map) { maps_[map->id] = std::move(map); }

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
    if (*type == "CREATE_MATCH") out = create(req);
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

Json GameServer::create(const Json& req) {
    Json err;
    std::string map_id;
    std::int64_t players = 0, rounds = 0, secs = 0;
    if (!token_field(req, "map_id", kMaxNameLength, false, "training-01", map_id, err)) return err;
    if (!int_field(req, "player_count", 2, kMaxPlayers, false, kMaxPlayers, players, err)) return err;
    if (!int_field(req, "rounds", 1, 99, false, kDefaultRounds, rounds, err)) return err;
    std::int64_t bots = 0;
    if (!int_field(req, "bots", 0, kMaxPlayers - 1, false, 0, bots, err)) return err;
    if (bots > players - 1) return make_error("BAD_REQUEST", "at least one seat must be left for a human player", &req);
    if (!int_field(req, "planning_seconds", 0, kMaxPlanningSeconds, false, cfg_.default_planning_seconds, secs, err))
        return err;
    auto mit = maps_.find(map_id);
    if (mit == maps_.end()) return make_error("UNKNOWN_MAP", "no such map", &req);
    if (static_cast<int>(rooms_.size()) >= cfg_.max_matches)
        return make_error("TOO_MANY_MATCHES", "this server hosts at most " + std::to_string(cfg_.max_matches) + " matches", &req);
    ValidationOptions vo;
    vo.player_count = static_cast<int>(players);
    auto errors = validate_map(*mit->second, vo);
    if (!errors.empty()) return make_error("INVALID_MAP", errors.front(), &req);

    Room r;
    r.id = "m" + std::to_string(next_room_++);
    r.map = mit->second;
    r.capacity = static_cast<int>(players);
    r.bots = static_cast<int>(bots);
    r.rounds = static_cast<int>(rounds);
    r.planning_seconds = static_cast<int>(secs);
    std::string id = r.id;
    rooms_.emplace(id, std::move(r));
    Json out = Json::object();
    out.set("type", "MATCH_CREATED");
    out.set("ok", true);
    out.set("match_id", id);
    out.set("player_count", players);
    out.set("bots", bots);
    return out;
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

Json GameServer::join(const Json& req) {
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
    if (humans < 1) { error = "at least one human player required"; return false; }
    if (joined < 2) { error = "at least two players required"; return false; }
    ValidationOptions vo;
    vo.player_count = joined;
    auto errors = validate_map(*r.map, vo);
    if (!errors.empty()) { error = errors.front(); return false; }
    MatchConfig mc;
    mc.match_id = r.id;
    mc.map = r.map;
    mc.player_count = joined;  // unoccupied slots are disabled
    mc.rounds = r.rounds;
    mc.request_budget_per_round = cfg_.request_budget_per_round;
    r.match = std::make_unique<Match>(std::move(mc));
    r.ai.clear();
    for (int i = 0; i < r.bots; ++i) {
        const int pid = humans + 1 + i;
        const std::uint64_t seed = fnv1a64(r.id + ":" + std::to_string(pid)) ^ (cfg_.bot_seed * 0x9e3779b97f4a7c15ULL);
        r.ai.push_back(std::make_unique<sim::Bot>(pid, *r.map, seed));
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
    for (auto& bot : r.ai) {
        auto prog = bot->plan(snap);
        r.match->submit_program(bot->id(), round, 1, {prog[0], prog[1], prog[2]}, true,
                                "bot-p" + std::to_string(bot->id()) + "-r" + std::to_string(round));
    }
}

void GameServer::advance(Room& r) {
    r.match->run_round();  // close planning (missing -> WAIT x3), execute, start next round
    open_planning(r);
}

void GameServer::poll() {
    const std::int64_t now = clock_.now_unix_ms();
    for (auto& kv : rooms_) {
        Room& r = kv.second;
        if (!r.match || r.match->finished()) continue;
        if (r.deadline_ms != 0 && now >= r.deadline_ms) advance(r);
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
        out.set("status", std::move(st));
    } else {
        Json st = Json::object();
        st.set("match_id", r->id);
        st.set("phase", "LOBBY");
        st.set("joined", static_cast<int>(r->tokens.size()) + r->bots);  // seats filled, bots included
        st.set("capacity", r->capacity);
        st.set("bots", r->bots);
        out.set("status", std::move(st));
    }
    return out;
}

}  // namespace botched::server

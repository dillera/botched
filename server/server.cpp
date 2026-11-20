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

Json make_error(std::string code, std::string message, const Json* request) {
    Json j = Json::object();
    j.set("type", "ERROR");
    j.set("ok", false);
    j.set("code", std::move(code));
    j.set("message", std::move(message));
    if (request) {
        if (auto t = request->get_string("type")) j.set("request_type", *t);
        if (auto id = request->get_string("request_id")) j.set("request_id", *id);
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
    auto j = Json::parse(line);
    if (!j || !j->is_object()) return make_error("BAD_REQUEST", "request is not a JSON object");
    return handle(*j);
}

Json GameServer::handle(const Json& req) {
    poll();
    auto type = req.get_string("type");
    if (!type) return make_error("BAD_REQUEST", "missing request type", &req);
    Json out;
    if (*type == "CREATE_MATCH") out = create(req);
    else if (*type == "JOIN_MATCH") out = join(req);
    else if (*type == "START_MATCH") out = start(req);
    else if (*type == "SUBMIT_PROGRAM") out = submit(req);
    else if (*type == "GET_SNAPSHOT") out = get_snapshot(req);
    else if (*type == "GET_EVENTS") out = get_events(req);
    else if (*type == "GET_MAP") out = get_map(req);
    else if (*type == "GET_MATCH_STATUS") out = get_status(req);
    else return make_error("UNKNOWN_REQUEST", "unknown request type " + *type, &req);
    poll();  // a request may have changed deadlines or completed planning
    return out;
}

GameServer::Room* GameServer::room_for(const Json& req, Json& err) {
    auto id = req.get_string("match_id");
    if (!id) { err = make_error("BAD_REQUEST", "missing match_id", &req); return nullptr; }
    auto it = rooms_.find(*id);
    if (it == rooms_.end()) { err = make_error("UNKNOWN_MATCH", "no such match", &req); return nullptr; }
    return &it->second;
}

// ------------------------------------------------------------------ lobby

Json GameServer::create(const Json& req) {
    std::string map_id = req.get_string("map_id").value_or("training-01");
    auto mit = maps_.find(map_id);
    if (mit == maps_.end()) return make_error("UNKNOWN_MAP", "no such map", &req);
    int players = static_cast<int>(req.get_int("player_count").value_or(kMaxPlayers));
    if (players < 2 || players > kMaxPlayers) return make_error("BAD_REQUEST", "player_count must be 2-4", &req);
    int rounds = static_cast<int>(req.get_int("rounds").value_or(kDefaultRounds));
    if (rounds < 1 || rounds > 99) return make_error("BAD_REQUEST", "rounds must be 1-99", &req);
    int secs = static_cast<int>(req.get_int("planning_seconds").value_or(cfg_.default_planning_seconds));
    if (secs < 0) return make_error("BAD_REQUEST", "planning_seconds must be >= 0", &req);
    ValidationOptions vo;
    vo.player_count = players;
    auto errors = validate_map(*mit->second, vo);
    if (!errors.empty()) return make_error("INVALID_MAP", errors.front(), &req);

    Room r;
    r.id = "m" + std::to_string(next_room_++);
    r.map = mit->second;
    r.capacity = players;
    r.rounds = rounds;
    r.planning_seconds = secs;
    std::string id = r.id;
    rooms_.emplace(id, std::move(r));
    Json out = Json::object();
    out.set("type", "MATCH_CREATED");
    out.set("ok", true);
    out.set("match_id", id);
    out.set("player_count", players);
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
    if (static_cast<int>(r->tokens.size()) >= r->capacity) return make_error("MATCH_FULL", "no free slot", &req);
    int pid = static_cast<int>(r->tokens.size()) + 1;
    std::string token = make_token(r->id, pid);
    r->tokens.push_back(token);
    Json out = Json::object();
    out.set("type", "JOINED");
    out.set("ok", true);
    out.set("match_id", r->id);
    out.set("player_id", pid);
    out.set("session", token);
    if (static_cast<int>(r->tokens.size()) == r->capacity) {
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
    int joined = static_cast<int>(r.tokens.size());
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
    r.match = std::make_unique<Match>(std::move(mc));
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
        return;
    }
    r.deadline_ms = clock_.now_unix_ms() + static_cast<std::int64_t>(r.planning_seconds) * 1000;
    r.match->set_planning_deadline(format_utc(r.deadline_ms));
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
    auto player = req.get_int("player_id");
    auto round = req.get_int("round");
    auto revision = req.get_int("revision");
    const Json* orders = req.find("orders");
    auto session = req.get_string("session");
    if (!player || !round || !revision || !orders || !orders->is_array())
        return make_error("BAD_REQUEST", "SUBMIT_PROGRAM needs player_id, round, revision, orders", &req);
    if (*player < 1 || *player > r->match->config().player_count)
        return make_error("UNKNOWN_PLAYER", "no such player slot", &req);
    if (!session || !token_equal(*session, r->tokens[static_cast<std::size_t>(*player - 1)]))
        return make_error("UNAUTHORIZED", "session does not own this player slot", &req);
    std::vector<std::string> toks;
    for (const auto& o : orders->items()) {
        if (!o.is_string()) return make_error("INVALID_ORDERS", "orders must be strings", &req);
        toks.push_back(o.as_string());
    }
    bool ready = req.get_bool("ready").value_or(false);
    std::string request_id = req.get_string("request_id").value_or("");

    SubmitResult res = r->match->submit_program(static_cast<int>(*player), static_cast<int>(*round),
                                                static_cast<int>(*revision), toks, ready, request_id);
    Json out = Json::object();
    out.set("type", "SUBMIT_PROGRAM");
    out.set("ok", res.ok);
    out.set("code", res.code);
    out.set("message", res.message);
    out.set("match_id", r->id);
    out.set("player_id", *player);
    out.set("round", *round);
    out.set("revision", *revision);
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
    auto since = req.get_int("since_version");
    if (!since) return make_error("BAD_REQUEST", "missing since_version", &req);
    auto ev = r->match->events_since(static_cast<int>(*since));
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
        out.set("status", r->match->status());
    } else {
        Json st = Json::object();
        st.set("match_id", r->id);
        st.set("phase", "LOBBY");
        st.set("joined", static_cast<int>(r->tokens.size()));
        st.set("capacity", r->capacity);
        out.set("status", std::move(st));
    }
    return out;
}

}  // namespace botched::server

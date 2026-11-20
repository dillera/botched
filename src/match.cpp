#include "botched/match.hpp"

#include <algorithm>

#include "botched/serialize.hpp"

namespace botched {

namespace {

Json int_array(const std::vector<int>& v) {
    Json a = Json::array();
    for (int x : v) a.push(x);
    return a;
}

}  // namespace

template <class F>
void Match::publish(F&& body) {
    ++version_;
    log_.set_version(version_);
    body();
    for (auto& e : log_.take()) events_.push_back(std::move(e));
    trim_events();
}

void Match::trim_events() {
    const int keep = cfg_.event_retention_versions;
    if (keep <= 0) return;
    while (!events_.empty() && events_.front().state_version <= version_ - keep) {
        trimmed_below_ = std::max(trimmed_below_, events_.front().state_version);
        events_.pop_front();
    }
}

Match::Match(MatchConfig cfg) : cfg_(std::move(cfg)) {
    state_ = make_initial_state(*cfg_.map, cfg_.player_count, cfg_.rounds);
    entries_.assign(static_cast<std::size_t>(cfg_.player_count), Entry{});
    Engine eng(*cfg_.map, log_);
    publish([&] { eng.start_round(state_); });
}

void Match::begin_round_planning() {
    entries_.assign(static_cast<std::size_t>(cfg_.player_count), Entry{});
}

bool Match::all_ready() const {
    if (state_.phase != Phase::Planning) return false;
    return std::all_of(entries_.begin(), entries_.end(), [](const Entry& e) { return e.has && e.ready; });
}

SubmitResult Match::submit_program(int player, int round, int revision, const std::vector<std::string>& orders,
                                   bool ready, const std::string& request_id) {
    auto key = std::make_pair(player, request_id);
    if (!request_id.empty()) {
        auto it = request_cache_.find(key);
        if (it != request_cache_.end()) return it->second;
    }
    auto reply = [&](bool ok, const char* code, const char* msg) {
        SubmitResult r;
        r.ok = ok;
        r.code = code;
        r.message = msg;
        r.round = state_.round;
        r.revision = revision;
        r.state_version = version_;
        if (!request_id.empty()) request_cache_[key] = r;
        return r;
    };

    if (player < 1 || player > cfg_.player_count) return reply(false, "UNKNOWN_PLAYER", "no such player slot");
    if (state_.phase != Phase::Planning) return reply(false, "PLANNING_CLOSED", "planning is closed");
    if (round != state_.round) return reply(false, "WRONG_ROUND", "programme is for a different round");
    Entry& cur = entries_[static_cast<std::size_t>(player - 1)];
    if (cur.has && cur.ready) return reply(false, "PROGRAM_LOCKED", "programme already marked ready");
    if (static_cast<int>(orders.size()) != kRegisters) return reply(false, "INVALID_ORDERS", "exactly 3 orders required");
    Programme prog{};
    for (std::size_t i = 0; i < orders.size(); ++i) {
        auto o = parse_order(orders[i]);
        if (!o) return reply(false, "INVALID_ORDERS", "unknown order token");
        prog[i] = *o;
    }
    if (revision < 1) return reply(false, "INVALID_REVISION", "revision must be >= 1");
    if (cur.has && revision < cur.revision) return reply(false, "STALE_REVISION", "revision lower than accepted");
    if (cur.has && revision == cur.revision) {
        if (cur.orders != prog || cur.ready != ready)
            return reply(false, "REVISION_CONFLICT", "same revision with different contents");
        return reply(true, "OK", "unchanged");  // identical resubmission under a new request id
    }
    cur.has = true;
    cur.revision = revision;
    cur.orders = prog;
    cur.ready = ready;
    publish([] {});  // submitted/ready sets changed: new authoritative state version
    return reply(true, "OK", "accepted");
}

bool Match::close_planning() {
    if (state_.phase != Phase::Planning) return false;
    Json missing = Json::array();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        Entry& e = entries_[i];
        if (!e.has) {
            e.has = true;
            e.orders = {Order::Wait, Order::Wait, Order::Wait};
            missing.push(static_cast<int>(i) + 1);
        }
        e.ready = true;
    }
    state_.phase = Phase::Executing;
    state_.tick = 0;
    publish([&] {
        log_.begin(state_.round, 0);
        Json p = Json::object();
        p.set("missing", std::move(missing));
        log_.emit("PLANNING_CLOSED", std::move(p));
    });
    return true;
}

bool Match::run_next_tick() {
    if (state_.phase != Phase::Executing) return false;
    Engine eng(*cfg_.map, log_);
    Programmes progs;
    for (const auto& e : entries_) progs.push_back(e.orders);
    const int tick = state_.tick + 1;
    publish([&] {
        eng.run_tick(state_, progs, tick);
        if (tick == kRegisters) eng.end_round(state_);
    });
    if (tick == kRegisters && state_.phase != Phase::Finished) {
        begin_round_planning();
        publish([&] { eng.start_round(state_); });
    }
    return true;
}

bool Match::run_round() {
    if (state_.phase == Phase::Finished) return false;
    if (state_.phase == Phase::Planning) close_planning();
    while (state_.phase == Phase::Executing) {
        int r = state_.round;
        run_next_tick();
        if (state_.round != r) break;
    }
    return true;
}

void Match::play_to_end() {
    while (run_round()) {}
}

Json Match::snapshot() const {
    Json j = Json::object();
    j.set("protocol", kProtocol);
    j.set("match_id", cfg_.match_id);
    j.set("rules_version", kRulesVersion);
    j.set("state_version", version_);
    Json board = Json::object();
    board.set("width", cfg_.map->width);
    board.set("height", cfg_.map->height);
    board.set("map_id", cfg_.map->id);
    write_world(state_, j);
    j.set("board", std::move(board));
    std::vector<int> submitted, ready;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].has) submitted.push_back(static_cast<int>(i) + 1);
        if (entries_[i].has && entries_[i].ready) ready.push_back(static_cast<int>(i) + 1);
    }
    j.set("submitted_players", int_array(submitted));
    j.set("ready_players", int_array(ready));
    j.set("planning_deadline_utc", deadline_utc_.empty() ? Json(nullptr) : Json(deadline_utc_));
    return j;
}

Json Match::status() const {
    Json j = Json::object();
    j.set("match_id", cfg_.match_id);
    j.set("phase", to_string(state_.phase));
    j.set("round", state_.round);
    j.set("rounds_total", state_.rounds_total);
    j.set("tick", state_.tick);
    j.set("state_version", version_);
    j.set("player_count", cfg_.player_count);
    std::vector<int> submitted, ready;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].has) submitted.push_back(static_cast<int>(i) + 1);
        if (entries_[i].has && entries_[i].ready) ready.push_back(static_cast<int>(i) + 1);
    }
    j.set("submitted_players", int_array(submitted));
    j.set("ready_players", int_array(ready));
    if (state_.phase == Phase::Finished) j.set("winners", int_array(compute_winners(state_)));
    return j;
}

std::string Match::state_hash() const { return hex64(fnv1a64(snapshot().dump())); }

std::optional<std::vector<Event>> Match::events_since(int since) const {
    if (since < trimmed_below_) return std::nullopt;
    std::vector<Event> out;
    for (const auto& e : events_)
        if (e.state_version > since) out.push_back(e);
    return out;
}

Json Match::save() const {
    Json j = snapshot();
    j.set("event_cursor", [&] {
        Json c = Json::object();
        c.set("round", log_.round());
        c.set("tick", log_.tick());
        c.set("sequence", log_.sequence());
        return c;
    }());
    Json progs = Json::array();
    for (const auto& e : entries_) {
        Json p = Json::object();
        p.set("has", e.has);
        p.set("revision", e.revision);
        Json orders = Json::array();
        for (Order o : e.orders) orders.push(to_string(o));
        p.set("orders", std::move(orders));
        p.set("ready", e.ready);
        progs.push(std::move(p));
    }
    j.set("programs", std::move(progs));
    return j;
}

std::optional<Match> Match::restore(MatchConfig cfg, const Json& saved, std::string& error) {
    Match m(std::move(cfg));
    GameState s;
    if (!read_world(saved, s, error)) return std::nullopt;
    if (s.player_count() != m.cfg_.player_count) { error = "player count mismatch"; return std::nullopt; }
    auto ver = saved.get_int("state_version");
    const Json* progs = saved.find("programs");
    const Json* cur = saved.find("event_cursor");
    if (!ver || !progs || !progs->is_array() || !cur) { error = "missing save fields"; return std::nullopt; }
    if (progs->items().size() != m.entries_.size()) { error = "programs size mismatch"; return std::nullopt; }
    for (std::size_t i = 0; i < m.entries_.size(); ++i) {
        const Json& p = progs->items()[i];
        Entry e;
        auto has = p.get_bool("has");
        auto rev = p.get_int("revision");
        auto ready = p.get_bool("ready");
        const Json* orders = p.find("orders");
        if (!has || !rev || !ready || !orders || !orders->is_array() ||
            static_cast<int>(orders->items().size()) != kRegisters) {
            error = "bad programme record";
            return std::nullopt;
        }
        e.has = *has;
        e.revision = static_cast<int>(*rev);
        e.ready = *ready;
        for (std::size_t k = 0; k < orders->items().size(); ++k) {
            const Json& o = orders->items()[k];
            auto parsed = o.is_string() ? parse_order(o.as_string()) : std::nullopt;
            if (!parsed) { error = "bad order token in save"; return std::nullopt; }
            e.orders[k] = *parsed;
        }
        m.entries_[i] = e;
    }
    m.state_ = std::move(s);
    m.version_ = static_cast<int>(*ver);
    m.trimmed_below_ = m.version_;  // earlier events are not part of a save
    m.events_.clear();
    m.log_.take();
    m.log_.restore_cursor(static_cast<int>(cur->get_int("round").value_or(0)),
                          static_cast<int>(cur->get_int("tick").value_or(0)),
                          static_cast<int>(cur->get_int("sequence").value_or(0)));
    if (auto d = saved.get_string("planning_deadline_utc")) m.deadline_utc_ = *d;
    return m;
}

}  // namespace botched

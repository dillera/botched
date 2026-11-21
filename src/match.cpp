#include "botched/match.hpp"

#include <algorithm>

#include "botched/serialize.hpp"
#include "botched/validate.hpp"

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
    cfg_.request_budget_per_round =
        std::clamp(cfg_.request_budget_per_round, kMinRequestBudget, kMaxRequestBudget);
    request_records_.assign(static_cast<std::size_t>(cfg_.player_count), {});
    budget_used_.assign(static_cast<std::size_t>(cfg_.player_count),
                        std::vector<int>(static_cast<std::size_t>(cfg_.rounds) + 2, 0));
    state_ = make_initial_state(*cfg_.map, cfg_.player_count, cfg_.rounds, cfg_.target_score);
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

namespace {

struct OutcomeInfo {
    bool ok;
    const char* code;
    const char* message;
};

}  // namespace

// Decision order for a keyed request (docs/request-id-contract.md section 4):
//   record + same payload -> replay; record + different payload -> REQUEST_ID_REUSED;
//   no record and budget used up -> REQUEST_LIMIT; otherwise decide by the rules and record.
SubmitResult Match::submit_program(int player, int round, int revision, const std::vector<std::string>& orders,
                                   bool ready, const std::string& request_id) {
    static const OutcomeInfo kInfo[] = {
        {true, "OK", "accepted"},
        {true, "OK", "unchanged"},
        {false, "PLANNING_CLOSED", "planning is closed"},
        {false, "WRONG_ROUND", "programme is for a different round"},
        {false, "PROGRAM_LOCKED", "programme already marked ready"},
        {false, "INVALID_ORDERS", "exactly 3 orders required"},
        {false, "INVALID_ORDERS", "unknown order token"},
        {false, "INVALID_REVISION", "revision must be >= 1"},
        {false, "STALE_REVISION", "revision lower than accepted"},
        {false, "REVISION_CONFLICT", "same revision with different contents"},
    };
    auto build = [&](const OutcomeInfo& i, int reply_round, int version) {
        SubmitResult r;
        r.ok = i.ok;
        r.code = i.code;
        r.message = i.message;
        r.round = reply_round;
        r.revision = revision;
        r.state_version = version;
        return r;
    };
    auto refusal = [&](const char* code, const char* msg) {  // never recorded, never charged
        return build(OutcomeInfo{false, code, msg}, state_.round, version_);
    };

    if (player < 1 || player > cfg_.player_count) return refusal("UNKNOWN_PLAYER", "no such player slot");

    // Canonical payload. An unrecognised token is one value: the rules answer every such payload
    // with the same rejection and no effect, so conflating them is safe (contract 3.1).
    RequestRecord probe;
    probe.round = round;
    probe.revision = revision;
    probe.ready = ready;
    probe.order_count = static_cast<std::uint8_t>(orders.size() > 255 ? 255 : orders.size());
    Programme prog{};
    bool tokens_ok = true;
    for (std::size_t i = 0; i < 3; ++i) {
        if (i >= orders.size()) { probe.orders[i] = 0xFE; continue; }
        auto o = parse_order(orders[i]);
        if (o) { probe.orders[i] = static_cast<std::uint8_t>(*o); prog[i] = *o; }
        else { probe.orders[i] = 0xFF; tokens_ok = false; }
    }
    for (std::size_t i = 3; i < orders.size() && tokens_ok; ++i)
        if (!parse_order(orders[i])) tokens_ok = false;

    const bool keyed = !request_id.empty();
    auto& records = request_records_[static_cast<std::size_t>(player - 1)];
    if (keyed) {
        for (const RequestRecord& rec : records) {
            if (rec.id != request_id) continue;
            const bool same = rec.round == probe.round && rec.revision == probe.revision && rec.ready == probe.ready &&
                              rec.order_count == probe.order_count && rec.orders[0] == probe.orders[0] &&
                              rec.orders[1] == probe.orders[1] && rec.orders[2] == probe.orders[2];
            if (!same) return refusal("REQUEST_ID_REUSED", "request_id was already used with different contents");
            return build(kInfo[rec.outcome], rec.reply_round, rec.state_version);  // replay: no effect at all
        }
        const int used = budget_used_[static_cast<std::size_t>(player - 1)][static_cast<std::size_t>(state_.round)];
        if (used >= cfg_.request_budget_per_round)
            return refusal("REQUEST_LIMIT", "too many new request ids this round");
    }

    // ---- the rules decide (unchanged from before the idempotency work) ----
    Outcome oc;
    Entry& cur = entries_[static_cast<std::size_t>(player - 1)];
    if (state_.phase != Phase::Planning) oc = Outcome::PlanningClosed;
    else if (round != state_.round) oc = Outcome::WrongRound;
    else if (cur.has && cur.ready) oc = Outcome::ProgramLocked;
    else if (static_cast<int>(orders.size()) != kRegisters) oc = Outcome::InvalidOrdersCount;
    else if (!tokens_ok) oc = Outcome::InvalidOrdersToken;
    else if (revision < 1) oc = Outcome::InvalidRevision;
    else if (cur.has && revision < cur.revision) oc = Outcome::StaleRevision;
    else if (cur.has && revision == cur.revision) {
        oc = (cur.orders != prog || cur.ready != ready) ? Outcome::RevisionConflict : Outcome::Unchanged;
    } else {
        cur.has = true;
        cur.revision = revision;
        cur.orders = prog;
        cur.ready = ready;
        publish([] {});  // submitted/ready sets changed: new authoritative state version
        oc = Outcome::Accepted;
    }

    SubmitResult res = build(kInfo[static_cast<std::size_t>(oc)], state_.round, version_);
    if (keyed) {
        probe.id = request_id;
        probe.reply_round = state_.round;
        probe.state_version = version_;
        probe.outcome = static_cast<std::uint8_t>(oc);
        records.push_back(std::move(probe));
        ++budget_used_[static_cast<std::size_t>(player - 1)][static_cast<std::size_t>(state_.round)];
    }
    return res;
}

std::size_t Match::request_record_count() const {
    std::size_t n = 0;
    for (const auto& v : request_records_) n += v.size();
    return n;
}

std::size_t Match::request_store_bytes() const {
    std::size_t bytes = 0;
    for (const auto& v : request_records_) {
        bytes += v.size() * sizeof(RequestRecord);
        for (const auto& r : v)
            if (r.id.capacity() > 15) bytes += r.id.capacity() + 1;
    }
    return bytes;
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
    j.set("target_score", state_.target_score);
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

namespace {

bool bad(std::string& error, std::string msg) {
    error = std::move(msg);
    return false;
}

bool int_in(const Json& o, const char* key, std::int64_t lo, std::int64_t hi, int& out, const std::string& ctx,
            std::string& error) {
    const Json* j = o.find(key);
    if (!j || !j->is_int()) return bad(error, ctx + "." + key + " must be an integer");
    if (j->as_int() < lo || j->as_int() > hi) return bad(error, ctx + "." + key + " is out of range");
    out = static_cast<int>(j->as_int());
    return true;
}

bool bool_of(const Json& o, const char* key, bool& out, const std::string& ctx, std::string& error) {
    const Json* j = o.find(key);
    if (!j || !j->is_bool()) return bad(error, ctx + "." + key + " must be a boolean");
    out = j->as_bool();
    return true;
}

// "YYYY-MM-DDTHH:MM:SSZ" (what server::format_utc writes); only the shape is checked.
bool utc_shape(const std::string& t) {
    if (t.size() != 20) return false;
    for (std::size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        bool digit = c >= '0' && c <= '9';
        switch (i) {
            case 4: case 7: if (c != '-') return false; break;
            case 10: if (c != 'T') return false; break;
            case 13: case 16: if (c != ':') return false; break;
            case 19: if (c != 'Z') return false; break;
            default: if (!digit) return false;
        }
    }
    return true;
}

}  // namespace

// Restoration is staged: configuration, header, world, programmes, version, cursor and
// deadline are all parsed and validated into locals first. Only when every check has passed
// is a Match constructed and filled, and that last step has no failure path. A rejected
// checkpoint therefore builds nothing and touches nothing (no half-restored Match exists).
std::optional<Match> Match::restore(MatchConfig cfg, const Json& saved, std::string& error) {
    error.clear();

    // 1. Configuration the checkpoint will be bound to.
    if (!cfg.map) { bad(error, "configuration has no map"); return std::nullopt; }
    if (cfg.player_count < 2 || cfg.player_count > kMaxPlayers) {
        bad(error, "configured player_count must be 2-4");
        return std::nullopt;
    }
    if (cfg.rounds < 1 || cfg.rounds > kMaxRounds) { bad(error, "configured rounds must be 1-200"); return std::nullopt; }
    if (cfg.target_score < 0 || cfg.target_score > kMaxTargetScore) { bad(error, "configured target_score must be 0-1000"); return std::nullopt; }
    {
        ValidationOptions vo;
        vo.player_count = cfg.player_count;
        auto errs = validate_map(*cfg.map, vo);
        if (!errs.empty()) { bad(error, "configured map is invalid: " + errs.front()); return std::nullopt; }
    }

    // 2. Header: protocol, rules version, identity and board.
    if (!saved.is_object()) { bad(error, "checkpoint must be a JSON object"); return std::nullopt; }
    auto proto = saved.get_string("protocol");
    if (!proto || *proto != kProtocol) { bad(error, std::string("protocol must be ") + kProtocol); return std::nullopt; }
    auto rules = saved.get_string("rules_version");
    if (!rules || *rules != kRulesVersion) {
        bad(error, std::string("rules_version must be ") + kRulesVersion + " (incompatible checkpoint)");
        return std::nullopt;
    }
    auto mid = saved.get_string("match_id");
    if (!mid || *mid != cfg.match_id) { bad(error, "match_id does not match the configuration"); return std::nullopt; }
    const Json* board = saved.find("board");
    if (!board || !board->is_object()) { bad(error, "board must be an object"); return std::nullopt; }
    auto bmap = board->get_string("map_id");
    if (!bmap || *bmap != cfg.map->id) { bad(error, "board.map_id does not match the configured map"); return std::nullopt; }
    auto bw = board->get_int("width");
    auto bh = board->get_int("height");
    if (!bw || !bh || *bw != cfg.map->width || *bh != cfg.map->height) {
        bad(error, "board dimensions differ from the configured map");
        return std::nullopt;
    }

    // 3. World (strict parse, then semantic validation against the map).
    GameState world;
    if (!read_world(saved, world, error)) return std::nullopt;
    if (!validate_world(world, *cfg.map, cfg.player_count, cfg.rounds, cfg.target_score, error)) return std::nullopt;

    // 4. Programmes (secret planning data) and their consistency with the phase.
    const Json* progs = saved.find("programs");
    if (!progs || !progs->is_array() || static_cast<int>(progs->items().size()) != cfg.player_count) {
        bad(error, "programs must be an array with one record per player");
        return std::nullopt;
    }
    std::vector<Entry> entries(static_cast<std::size_t>(cfg.player_count));
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Json& p = progs->items()[i];
        const std::string ctx = "programs[" + std::to_string(i) + "]";
        if (!p.is_object()) { bad(error, ctx + " must be an object"); return std::nullopt; }
        Entry& e = entries[i];
        if (!bool_of(p, "has", e.has, ctx, error)) return std::nullopt;
        if (!int_in(p, "revision", 0, 1'000'000, e.revision, ctx, error)) return std::nullopt;
        if (!bool_of(p, "ready", e.ready, ctx, error)) return std::nullopt;
        const Json* orders = p.find("orders");
        if (!orders || !orders->is_array() || static_cast<int>(orders->items().size()) != kRegisters) {
            bad(error, ctx + ".orders must be an array of exactly 3 order tokens");
            return std::nullopt;
        }
        bool all_wait = true;
        for (std::size_t k = 0; k < orders->items().size(); ++k) {
            const Json& o = orders->items()[k];
            auto parsed = o.is_string() ? parse_order(o.as_string()) : std::nullopt;
            if (!parsed) { bad(error, ctx + ".orders holds an invalid order token"); return std::nullopt; }
            e.orders[k] = *parsed;
            all_wait = all_wait && *parsed == Order::Wait;
        }
        if (!e.has) {
            if (e.revision != 0 || e.ready || !all_wait) {
                bad(error, ctx + ": a player without a programme must have revision 0, not ready, orders WAIT");
                return std::nullopt;
            }
        } else {
            if (world.phase == Phase::Planning && e.revision < 1) {
                bad(error, ctx + ": a submitted programme needs revision >= 1");
                return std::nullopt;
            }
            if (e.revision == 0 && !all_wait) {
                bad(error, ctx + ": revision 0 is reserved for the server's WAIT fill-in orders");
                return std::nullopt;
            }
        }
        if (world.phase != Phase::Planning && !(e.has && e.ready)) {
            bad(error, ctx + ": programme must be locked once planning has closed");
            return std::nullopt;
        }
    }

    // The save also carries the derived public sets; they must agree with the programme records.
    for (const char* key : {"submitted_players", "ready_players"}) {
        const bool want_ready = std::string(key) == "ready_players";
        const Json* arr = saved.find(key);
        if (!arr || !arr->is_array()) { bad(error, std::string(key) + " must be an array"); return std::nullopt; }
        Json expected = Json::array();
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].has && (!want_ready || entries[i].ready)) expected.push(static_cast<int>(i) + 1);
        if (!(*arr == expected)) { bad(error, std::string(key) + " does not match the programme records"); return std::nullopt; }
    }

    // 5. Version, event cursor, deadline.
    int version = 0;
    if (!int_in(saved, "state_version", 1, 1'000'000'000, version, "checkpoint", error)) return std::nullopt;
    {
        // Publications so far: 1 for round 1's start, then close + 3 ticks + next start per round.
        const int base = 1 + 5 * (world.round - 1);
        int min_version = base;
        if (world.phase == Phase::Executing) min_version = base + 1 + world.tick;
        if (world.phase == Phase::Finished) min_version = base + 4;
        if (version < min_version) {
            bad(error, "state_version is lower than the match history implies");
            return std::nullopt;
        }
    }
    const Json* cur = saved.find("event_cursor");
    if (!cur || !cur->is_object()) { bad(error, "event_cursor must be an object"); return std::nullopt; }
    int cur_round = 0, cur_tick = 0, cur_seq = 0;
    if (!int_in(*cur, "round", 0, 1'000'000, cur_round, "event_cursor", error)) return std::nullopt;
    if (!int_in(*cur, "tick", 0, kRegisters, cur_tick, "event_cursor", error)) return std::nullopt;
    if (!int_in(*cur, "sequence", 0, 1'000'000'000, cur_seq, "event_cursor", error)) return std::nullopt;
    if (cur_round != world.round || cur_tick != world.tick) {
        bad(error, "event_cursor does not match the world's round and tick");
        return std::nullopt;
    }
    std::string deadline;
    const Json* dl = saved.find("planning_deadline_utc");
    if (!dl || !(dl->is_null() || dl->is_string())) {
        bad(error, "planning_deadline_utc must be a string or null");
        return std::nullopt;
    }
    if (dl->is_string()) {
        if (!utc_shape(dl->as_string())) { bad(error, "planning_deadline_utc is not a UTC timestamp"); return std::nullopt; }
        deadline = dl->as_string();
    }

    // 6. Commit. Everything above passed; nothing below can reject.
    Match m(std::move(cfg));
    m.state_ = std::move(world);
    m.entries_ = std::move(entries);
    m.version_ = version;
    m.trimmed_below_ = version;  // earlier events are not part of a save
    m.events_.clear();
    // The idempotency store is not part of a checkpoint (D8): a restored match starts with an
    // empty store and fresh budgets. The constructor already produced exactly that.
    m.log_.take();
    m.log_.restore_cursor(cur_round, cur_tick, cur_seq);
    m.deadline_utc_ = std::move(deadline);
    return m;
}

}  // namespace botched

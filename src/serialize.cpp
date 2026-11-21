#include "botched/serialize.hpp"

#include <cstdio>
#include <optional>

namespace botched {

void write_world(const GameState& s, Json& out) {
    out.set("phase", to_string(s.phase));
    out.set("round", s.round);
    out.set("tick", s.tick);
    out.set("rounds_total", s.rounds_total);
    if (s.target_score > 0) out.set("target_score", s.target_score);  // absent in fixed-round mode (hashes unchanged)
    out.set("next_crate_id", s.next_crate_id);

    Json players = Json::array();
    for (const auto& r : s.robots) {
        Json p = Json::object();
        p.set("id", r.id);
        p.set("x", r.respawn_pending ? Json(nullptr) : Json(r.pos.x));
        p.set("y", r.respawn_pending ? Json(nullptr) : Json(r.pos.y));
        p.set("heading", to_string(r.heading));
        p.set("damage", r.damage);
        p.set("carrying", r.carrying ? Json(r.carrying) : Json(nullptr));
        p.set("score", r.score);
        p.set("deliveries", r.deliveries);
        p.set("damage_taken", r.damage_taken);
        p.set("respawn_pending", r.respawn_pending);
        if (!s.traps.empty()) p.set("sabotage", r.sabotage);  // only on maps with traps (hashes elsewhere unchanged)
        players.push(std::move(p));
    }
    out.set("players", std::move(players));

    Json crates = Json::array();
    for (const auto& c : s.crates) {
        Json j = Json::object();
        j.set("id", c.id);
        j.set("x", c.carried ? Json(nullptr) : Json(c.pos.x));
        j.set("y", c.carried ? Json(nullptr) : Json(c.pos.y));
        j.set("carrier_player_id", c.carried ? Json(c.carrier) : Json(nullptr));
        crates.push(std::move(j));
    }
    out.set("crates", std::move(crates));

    Json groups = Json::array();
    for (const auto& g : s.groups) {
        Json j = Json::object();
        j.set("id", std::string(1, g.id));
        j.set("reversed", g.reversed);
        groups.push(std::move(j));
    }
    out.set("groups", std::move(groups));

    if (!s.traps.empty()) {
        Json traps = Json::array();
        for (const auto& t : s.traps) {
            Json j = Json::object();
            j.set("id", t.id);
            j.set("armed", t.armed);
            j.set("owner", t.owner ? Json(t.owner) : Json(nullptr));
            traps.push(std::move(j));
        }
        out.set("traps", std::move(traps));
    }
}

namespace {

constexpr std::int64_t kMaxField = 1'000'000;  // far above any legal value; prevents int narrowing

bool fail(std::string& error, const std::string& what) {
    error = what;
    return false;
}

// Required integer field in [lo, hi]. Wrong type, missing or out of range -> error.
bool need_int(const Json& o, const char* key, std::int64_t lo, std::int64_t hi, int& out, const std::string& ctx,
              std::string& error) {
    const Json* j = o.find(key);
    if (!j || !j->is_int()) return fail(error, ctx + "." + key + " must be an integer");
    if (j->as_int() < lo || j->as_int() > hi)
        return fail(error, ctx + "." + key + " out of range [" + std::to_string(lo) + "," + std::to_string(hi) + "]");
    out = static_cast<int>(j->as_int());
    return true;
}

bool need_bool(const Json& o, const char* key, bool& out, const std::string& ctx, std::string& error) {
    const Json* j = o.find(key);
    if (!j || !j->is_bool()) return fail(error, ctx + "." + key + " must be a boolean");
    out = j->as_bool();
    return true;
}

// Optional integer: null -> absent. Any other non-integer is an error (never silently ignored).
bool opt_int(const Json& o, const char* key, std::int64_t lo, std::int64_t hi, std::optional<int>& out,
             const std::string& ctx, std::string& error) {
    const Json* j = o.find(key);
    if (!j) return fail(error, ctx + "." + key + " is missing");
    if (j->is_null()) { out.reset(); return true; }
    int v = 0;
    if (!need_int(o, key, lo, hi, v, ctx, error)) return false;
    out = v;
    return true;
}

}  // namespace

bool read_world(const Json& in, GameState& s, std::string& error) {
    if (!in.is_object()) return fail(error, "world must be a JSON object");
    GameState g;
    auto phase = in.get_string("phase");
    if (!phase) return fail(error, "phase must be a string");
    auto ph = parse_phase(*phase);
    if (!ph) return fail(error, "phase has an unknown value");
    g.phase = *ph;
    if (!need_int(in, "round", 0, kMaxField, g.round, "world", error)) return false;
    if (!need_int(in, "tick", 0, kRegisters, g.tick, "world", error)) return false;
    if (!need_int(in, "rounds_total", 1, kMaxRounds, g.rounds_total, "world", error)) return false;
    if (in.find("target_score") && !need_int(in, "target_score", 1, kMaxTargetScore, g.target_score, "world", error)) return false;
    if (!need_int(in, "next_crate_id", 1, kMaxField, g.next_crate_id, "world", error)) return false;

    const Json* players = in.find("players");
    const Json* crates = in.find("crates");
    const Json* groups = in.find("groups");
    if (!players || !players->is_array()) return fail(error, "players must be an array");
    if (!crates || !crates->is_array()) return fail(error, "crates must be an array");
    if (!groups || !groups->is_array()) return fail(error, "groups must be an array");
    if (players->items().size() < 2 || players->items().size() > static_cast<std::size_t>(kMaxPlayers))
        return fail(error, "players must hold 2-4 records");
    if (crates->items().size() > 20 * 16 + kMaxPlayers) return fail(error, "too many crates");
    if (groups->items().size() > 26) return fail(error, "too many groups");

    for (std::size_t i = 0; i < players->items().size(); ++i) {
        const Json& pj = players->items()[i];
        const std::string ctx = "players[" + std::to_string(i) + "]";
        if (!pj.is_object()) return fail(error, ctx + " must be an object");
        Robot r;
        if (!need_int(pj, "id", 0, kMaxField, r.id, ctx, error)) return false;
        if (r.id != static_cast<int>(i) + 1) return fail(error, ctx + ".id must be " + std::to_string(i + 1));
        auto heading = pj.get_string("heading");
        auto d = heading ? parse_dir(*heading) : std::nullopt;
        if (!d) return fail(error, ctx + ".heading must be N, E, S or W");
        r.heading = *d;
        if (!need_int(pj, "damage", 0, kMaxHealth, r.damage, ctx, error)) return false;
        if (!need_int(pj, "score", 0, kMaxField, r.score, ctx, error)) return false;
        if (!need_int(pj, "deliveries", 0, kMaxField, r.deliveries, ctx, error)) return false;
        if (!need_int(pj, "damage_taken", 0, kMaxField, r.damage_taken, ctx, error)) return false;
        if (!need_bool(pj, "respawn_pending", r.respawn_pending, ctx, error)) return false;
        if (pj.find("sabotage") && !need_int(pj, "sabotage", 0, kMaxField, r.sabotage, ctx, error)) return false;
        std::optional<int> x, y, carrying;
        if (!opt_int(pj, "x", 0, kMaxField, x, ctx, error)) return false;
        if (!opt_int(pj, "y", 0, kMaxField, y, ctx, error)) return false;
        if (!opt_int(pj, "carrying", 1, kMaxField, carrying, ctx, error)) return false;
        if (r.respawn_pending) {
            if (x || y) return fail(error, ctx + " is pending respawn and must have no position");
        } else {
            if (!x || !y) return fail(error, ctx + " is active and needs a position");
            r.pos = {*x, *y};
        }
        r.carrying = carrying.value_or(0);
        g.robots.push_back(r);
    }
    for (std::size_t i = 0; i < crates->items().size(); ++i) {
        const Json& cj = crates->items()[i];
        const std::string ctx = "crates[" + std::to_string(i) + "]";
        if (!cj.is_object()) return fail(error, ctx + " must be an object");
        Crate c;
        if (!need_int(cj, "id", 1, kMaxField, c.id, ctx, error)) return false;
        std::optional<int> x, y, carrier;
        if (!opt_int(cj, "x", 0, kMaxField, x, ctx, error)) return false;
        if (!opt_int(cj, "y", 0, kMaxField, y, ctx, error)) return false;
        if (!opt_int(cj, "carrier_player_id", 1, kMaxPlayers, carrier, ctx, error)) return false;
        if (carrier) {
            if (x || y) return fail(error, ctx + " is carried and must have no position");
            c.carried = true;
            c.carrier = *carrier;
        } else {
            if (!x || !y) return fail(error, ctx + " is loose and needs a position");
            c.pos = {*x, *y};
        }
        g.crates.push_back(c);
    }
    for (std::size_t i = 0; i < groups->items().size(); ++i) {
        const Json& gj = groups->items()[i];
        const std::string ctx = "groups[" + std::to_string(i) + "]";
        if (!gj.is_object()) return fail(error, ctx + " must be an object");
        auto id = gj.get_string("id");
        if (!id || id->size() != 1 || (*id)[0] < 'A' || (*id)[0] > 'Z')
            return fail(error, ctx + ".id must be one letter A-Z");
        bool rev = false;
        if (!need_bool(gj, "reversed", rev, ctx, error)) return false;
        g.groups.push_back({(*id)[0], rev});
    }
    if (const Json* traps = in.find("traps")) {
        if (!traps->is_array()) return fail(error, "traps must be an array");
        if (traps->items().size() > 9) return fail(error, "too many traps");
        for (std::size_t i = 0; i < traps->items().size(); ++i) {
            const Json& tj = traps->items()[i];
            const std::string ctx = "traps[" + std::to_string(i) + "]";
            if (!tj.is_object()) return fail(error, ctx + " must be an object");
            TrapState t;
            if (!need_int(tj, "id", 1, 9, t.id, ctx, error)) return false;
            if (!need_bool(tj, "armed", t.armed, ctx, error)) return false;
            std::optional<int> owner;
            if (!opt_int(tj, "owner", 1, kMaxPlayers, owner, ctx, error)) return false;
            t.owner = owner.value_or(0);
            g.traps.push_back(t);
        }
    }
    s = std::move(g);
    return true;
}

Json event_to_json(const Event& e, std::string_view match_id) {
    Json j = Json::object();
    j.set("match_id", std::string(match_id));
    j.set("state_version", e.state_version);
    j.set("round", e.round);
    j.set("tick", e.tick);
    j.set("sequence", e.sequence);
    j.set("type", e.type);
    j.set("params", e.params);
    return j;
}

std::uint64_t fnv1a64(std::string_view data) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : data) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string hex64(std::uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

}  // namespace botched

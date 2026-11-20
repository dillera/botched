#include "botched/serialize.hpp"

#include <cstdio>

namespace botched {

void write_world(const GameState& s, Json& out) {
    out.set("phase", to_string(s.phase));
    out.set("round", s.round);
    out.set("tick", s.tick);
    out.set("rounds_total", s.rounds_total);
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
}

namespace {
bool fail(std::string& error, const char* what) {
    error = what;
    return false;
}
}  // namespace

bool read_world(const Json& in, GameState& s, std::string& error) {
    GameState g;
    auto phase = in.get_string("phase");
    auto round = in.get_int("round");
    auto tick = in.get_int("tick");
    auto total = in.get_int("rounds_total");
    auto next = in.get_int("next_crate_id");
    if (!phase || !round || !tick || !total || !next) return fail(error, "missing world header field");
    auto ph = parse_phase(*phase);
    if (!ph) return fail(error, "bad phase");
    g.phase = *ph;
    g.round = static_cast<int>(*round);
    g.tick = static_cast<int>(*tick);
    g.rounds_total = static_cast<int>(*total);
    g.next_crate_id = static_cast<int>(*next);

    const Json* players = in.find("players");
    const Json* crates = in.find("crates");
    const Json* groups = in.find("groups");
    if (!players || !players->is_array() || !crates || !crates->is_array() || !groups || !groups->is_array())
        return fail(error, "missing players/crates/groups");

    for (const auto& pj : players->items()) {
        Robot r;
        auto id = pj.get_int("id");
        auto heading = pj.get_string("heading");
        auto damage = pj.get_int("damage");
        auto score = pj.get_int("score");
        auto deliveries = pj.get_int("deliveries");
        auto taken = pj.get_int("damage_taken");
        auto pending = pj.get_bool("respawn_pending");
        if (!id || !heading || !damage || !score || !deliveries || !taken || !pending)
            return fail(error, "bad player record");
        auto d = parse_dir(*heading);
        if (!d) return fail(error, "bad player heading");
        r.id = static_cast<int>(*id);
        r.heading = *d;
        r.damage = static_cast<int>(*damage);
        r.score = static_cast<int>(*score);
        r.deliveries = static_cast<int>(*deliveries);
        r.damage_taken = static_cast<int>(*taken);
        r.respawn_pending = *pending;
        if (!r.respawn_pending) {
            auto x = pj.get_int("x");
            auto y = pj.get_int("y");
            if (!x || !y) return fail(error, "active player without position");
            r.pos = {static_cast<int>(*x), static_cast<int>(*y)};
        }
        if (auto c = pj.get_int("carrying")) r.carrying = static_cast<int>(*c);
        if (r.id != static_cast<int>(g.robots.size()) + 1) return fail(error, "player ids must be 1..N in order");
        g.robots.push_back(r);
    }
    for (const auto& cj : crates->items()) {
        Crate c;
        auto id = cj.get_int("id");
        if (!id) return fail(error, "bad crate record");
        c.id = static_cast<int>(*id);
        if (auto carrier = cj.get_int("carrier_player_id")) {
            c.carried = true;
            c.carrier = static_cast<int>(*carrier);
        } else {
            auto x = cj.get_int("x");
            auto y = cj.get_int("y");
            if (!x || !y) return fail(error, "loose crate without position");
            c.pos = {static_cast<int>(*x), static_cast<int>(*y)};
        }
        g.crates.push_back(c);
    }
    for (const auto& gj : groups->items()) {
        auto id = gj.get_string("id");
        auto rev = gj.get_bool("reversed");
        if (!id || id->size() != 1 || !rev) return fail(error, "bad group record");
        g.groups.push_back({(*id)[0], *rev});
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

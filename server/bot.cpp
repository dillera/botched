#include "bot.hpp"

#include <algorithm>
#include <optional>
#include <queue>

#include "botched/engine.hpp"
#include "botched/serialize.hpp"
#include "botched/types.hpp"

namespace botched::sim {

namespace {

struct View {
    Pos pos;
    Dir heading = Dir::N;
    bool carrying = false;
    bool active = false;
};

// BFS over walkable (non-pit) tiles, ignoring robots. Returns the move directions of a
// shortest path from `from` to any tile in `goals`, or nullopt.
std::optional<std::vector<Dir>> path_to(const Map& m, Pos from, const std::vector<Pos>& goals) {
    auto idx = [&](Pos p) { return static_cast<std::size_t>(p.y * m.width + p.x); };
    std::vector<int> prev(m.tiles.size(), -2);  // -2 unseen, -1 root, else previous dir+... use parent index
    std::vector<std::size_t> parent(m.tiles.size(), 0);
    std::vector<Dir> via(m.tiles.size(), Dir::N);
    std::queue<Pos> q;
    prev[idx(from)] = -1;
    q.push(from);
    auto is_goal = [&](Pos p) { return std::find(goals.begin(), goals.end(), p) != goals.end(); };
    while (!q.empty()) {
        Pos c = q.front();
        q.pop();
        if (c != from && is_goal(c)) {
            std::vector<Dir> out;
            for (std::size_t i = idx(c); prev[i] != -1; i = parent[i]) out.push_back(via[i]);
            std::reverse(out.begin(), out.end());
            return out;
        }
        for (int d = 0; d < 4; ++d) {
            Dir dd = static_cast<Dir>(d);
            Pos n{c.x + dir_dx(dd), c.y + dir_dy(dd)};
            if (!m.walkable(n) || prev[idx(n)] != -2) continue;
            prev[idx(n)] = 0;
            parent[idx(n)] = idx(c);
            via[idx(n)] = dd;
            q.push(n);
        }
    }
    return std::nullopt;
}

const char* kNoise[] = {"FORWARD_1", "FORWARD_2", "BACK_1", "TURN_LEFT", "TURN_RIGHT", "TURN_AROUND",
                        "DROP",      "TOGGLE",    "WAIT"};

}  // namespace

std::array<std::string, 3> Bot::plan_by_path(const Json& snap) {
    View me;
    std::vector<Pos> crates;
    if (const Json* players = snap.find("players")) {
        for (const auto& p : players->items()) {
            if (p.get_int("id").value_or(0) != id_) continue;
            if (p.get_bool("respawn_pending").value_or(true)) break;
            me.active = true;
            me.pos = {static_cast<int>(p.get_int("x").value_or(0)), static_cast<int>(p.get_int("y").value_or(0))};
            me.heading = parse_dir(p.get_string("heading").value_or("N")).value_or(Dir::N);
            const Json* c = p.find("carrying");
            me.carrying = c && !c->is_null();
        }
    }
    if (const Json* cs = snap.find("crates")) {
        for (const auto& c : cs->items()) {
            if (c.find("carrier_player_id") && !c.find("carrier_player_id")->is_null()) continue;
            crates.push_back({static_cast<int>(c.get_int("x").value_or(0)), static_cast<int>(c.get_int("y").value_or(0))});
        }
    }

    std::array<std::string, 3> out{"WAIT", "WAIT", "WAIT"};
    if (!me.active) return out;

    std::vector<Pos> goals;
    if (me.carrying) {
        if (auto d = map_.docks[static_cast<std::size_t>(id_)]) goals.push_back(*d);
    } else {
        goals = crates;
    }
    std::vector<Dir> path;
    if (!goals.empty()) {
        if (auto p = path_to(map_, me.pos, goals)) path = *p;
    }

    Dir h = me.heading;
    std::size_t at = 0;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (at >= path.size()) break;
        Dir want = path[at];
        if (want == h) {
            if (at + 1 < path.size() && path[at + 1] == h) { out[slot] = "FORWARD_2"; at += 2; }
            else { out[slot] = "FORWARD_1"; at += 1; }
        } else if (want == turn_right(h)) {
            out[slot] = "TURN_RIGHT"; h = want;
        } else if (want == turn_left(h)) {
            out[slot] = "TURN_LEFT"; h = want;
        } else {  // directly behind
            out[slot] = "BACK_1"; at += 1;
        }
    }
    // Noise: occasionally replace a slot with a random legal order.
    for (auto& o : out) {
        if (rng_.below(10) == 0) o = kNoise[rng_.below(9)];
    }
    return out;
}


// ------------------------------------------------------------------ personalities

const char* to_string(Personality p) {
    switch (p) {
        case Personality::Hauler: return "hauler";
        case Personality::Opportunist: return "opportunist";
        case Personality::Saboteur: return "saboteur";
    }
    return "?";
}

Personality personality_for(std::uint64_t key) { return static_cast<Personality>(key % 3); }

const char* to_string(GoalKind k) {
    switch (k) {
        case GoalKind::None: return "none";
        case GoalKind::Haul: return "haul";
        case GoalKind::Contest: return "contest";
        case GoalKind::ArmTrap: return "arm trap";
        case GoalKind::ClaimTurret: return "claim turret";
        case GoalKind::Chase: return "chase";
    }
    return "?";
}

namespace {

constexpr int kFar = 100000;  // "unreachable" in the cost fields (tenths of a tile)

// What a personality is like. Weights are the chance (%) of choosing each family of goal next:
// haul / mixed (opportunist) / hunt (environmental disruption first, then chasing a rival).
struct Traits {
    int w_haul, w_mixed, w_hunt;
    int risk_pct;      // how much a hit counts against a route (100 = baseline; lower = bolder)
    int sabotage_pct;  // how much sabotage points and owning machinery are worth
    int blunder, fumble;
    int patience;      // rounds before an unfinished goal is dropped
};

Traits traits_of(Personality p) {
    switch (p) {
        case Personality::Hauler: return {80, 15, 5, 140, 60, 6, 6, 8};
        case Personality::Opportunist: return {50, 30, 20, 100, 100, 10, 8, 7};
        case Personality::Saboteur: return {20, 25, 55, 70, 180, 14, 10, 5};
    }
    return {50, 30, 20, 100, 100, 10, 8, 7};
}

// Cost of the next hit, in tiles of walking, by the damage already taken (3 damage = disabled). It grows steeply:
// a healthy robot will take a hit to save a long detour; a robot one hit from disabling will not.
int hit_cost_tiles(int damage, int risk_pct, bool carrying) {
    static const int kTable[3] = {6, 14, 80};
    int c = kTable[std::max(0, std::min(damage, 2))] * risk_pct / 100;
    if (carrying) c = c * 125 / 100;
    return std::max(1, c);
}

using Field = std::vector<int>;

int idx(const Map& m, Pos p) { return p.y * m.width + p.x; }

// Backward Dijkstra: for each tile, the cost (tenths of a tile) of walking from it to the nearest goal, where
// entering a tile costs tile_cost[tile]. Walls, devices and pits are never routed through.
Field backward_field(const Map& m, const std::vector<int>& tile_cost, const std::vector<Pos>& goals) {
    Field d(m.tiles.size(), kFar);
    using Item = std::pair<int, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (Pos g : goals)
        if (m.in_bounds(g)) { d[static_cast<std::size_t>(idx(m, g))] = 0; pq.push({0, idx(m, g)}); }
    while (!pq.empty()) {
        auto [dc, i] = pq.top();
        pq.pop();
        if (dc > d[static_cast<std::size_t>(i)]) continue;
        Pos c{i % m.width, i / m.width};
        for (int k = 0; k < 4; ++k) {
            Dir dd = static_cast<Dir>(k);
            Pos n{c.x + dir_dx(dd), c.y + dir_dy(dd)};
            if (!m.in_bounds(n) || !m.walkable(n)) continue;
            const int nd = dc + tile_cost[static_cast<std::size_t>(i)];  // n -> c enters c
            if (nd < d[static_cast<std::size_t>(idx(m, n))]) { d[static_cast<std::size_t>(idx(m, n))] = nd; pq.push({nd, idx(m, n)}); }
        }
    }
    return d;
}

// Forward Dijkstra from `from`: cost of walking there, entering a tile costs tile_cost[tile].
Field forward_field(const Map& m, const std::vector<int>& tile_cost, Pos from) {
    Field d(m.tiles.size(), kFar);
    using Item = std::pair<int, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    d[static_cast<std::size_t>(idx(m, from))] = 0;
    pq.push({0, idx(m, from)});
    while (!pq.empty()) {
        auto [dc, i] = pq.top();
        pq.pop();
        if (dc > d[static_cast<std::size_t>(i)]) continue;
        Pos c{i % m.width, i / m.width};
        for (int k = 0; k < 4; ++k) {
            Dir dd = static_cast<Dir>(k);
            Pos n{c.x + dir_dx(dd), c.y + dir_dy(dd)};
            if (!m.in_bounds(n) || !m.walkable(n)) continue;
            const int nd = dc + tile_cost[static_cast<std::size_t>(idx(m, n))];
            if (nd < d[static_cast<std::size_t>(idx(m, n))]) { d[static_cast<std::size_t>(idx(m, n))] = nd; pq.push({nd, idx(m, n)}); }
        }
    }
    return d;
}

Pos step(Pos p, Dir d) { return {p.x + dir_dx(d), p.y + dir_dy(d)}; }

int manhattan(Pos a, Pos b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

// A TOGGLE that changes no trap or belt-group state (e.g. re-arming a trap I already own and have armed) does
// nothing useful. The search charges a modest penalty per such toggle, just above the planning noise, so the
// bot will not pick it over an equivalent plan that does something, yet a tactical re-toggle is still possible
// whenever it genuinely scores more.
constexpr int kRedundantToggle = 400;

struct Candidate {
    std::array<Order, 3> orders;
    int score;
};

}  // namespace

// Everything the bot works out once per round from the public snapshot.
struct Bot::Ctx {
    GameState g0;
    int me_id = 0;
    std::vector<int> hits;        // expected hits (x10) a robot standing on each tile takes per tick
    std::vector<int> cost;        // tenths of a tile to enter each tile: distance + tar + hazards
    Field from_me;
    std::vector<Pos> loose;       // loose crates
    const Robot& me() const { return g0.robot(me_id); }
};

Bot::Bot(int player_id, const Map& map, std::uint64_t seed, Personality p)
    : id_(player_id), map_(map), rng_(seed), personality_(p), blunder_percent_(traits_of(p).blunder), fumble_percent_(traits_of(p).fumble) {}

std::string Bot::goal_text(const Goal& g) const {
    std::string s = to_string(g.kind);
    switch (g.kind) {
        case GoalKind::Haul: case GoalKind::Contest: if (g.target) s += " crate#" + std::to_string(g.target); break;
        case GoalKind::ArmTrap: s += " trap#" + std::to_string(g.target); break;
        case GoalKind::ClaimTurret: s += " turret#" + std::to_string(g.target); break;
        case GoalKind::Chase: s += " P" + std::to_string(g.target); break;
        default: break;
    }
    return s;
}

std::string Bot::describe() const {
    std::string g = to_string(goal_.kind);
    switch (goal_.kind) {
        case GoalKind::ArmTrap: g += " X" + std::to_string(goal_.target); break;
        case GoalKind::ClaimTurret: g += " Y" + std::to_string(goal_.target); break;
        case GoalKind::Chase: g += " P" + std::to_string(goal_.target); break;
        default: break;
    }
    return std::string(to_string(personality_)) + ": " + g;
}

// Tiles from which a TOGGLE reaches the console(s) of trap `trap`: the console's tile and its four neighbours.
static std::vector<Pos> trap_reach(const Map& m, int trap) {
    std::vector<Pos> out;
    for (const auto& c : m.trap_consoles) {
        if (c.trap != trap) continue;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (std::abs(dx) + std::abs(dy) > 1) continue;
                Pos p{c.pos.x + dx, c.pos.y + dy};
                if (m.walkable(p)) out.push_back(p);
            }
    }
    return out;
}

static std::vector<Pos> turret_pads(const Map& m, int turret) {
    std::vector<Pos> out;
    for (const auto& p : m.turret_pads) if (p.turret == turret && m.walkable(p.pos)) out.push_back(p.pos);
    return out;
}

bool Bot::goal_done(const Ctx& c) const {
    const Robot& me = c.me();
    switch (goal_.kind) {
        case GoalKind::Haul:
        case GoalKind::Contest:
            return me.deliveries > goal_.deliveries0;
        case GoalKind::ArmTrap: {
            const TrapState* t = c.g0.trap(goal_.target);
            return t && t->armed && t->owner == id_;
        }
        case GoalKind::ClaimTurret: {
            const TurretState* t = c.g0.turret(goal_.target);
            return t && t->owner == id_;
        }
        case GoalKind::Chase: {
            if (me.sabotage > goal_.sabotage0) return true;                       // it paid off
            const Robot& r = c.g0.robot(goal_.target);
            return r.respawn_pending;                                              // the quarry is out of the game
        }
        case GoalKind::None: return true;
    }
    return true;
}

bool Bot::goal_impossible(const Ctx& c) const {
    switch (goal_.kind) {
        case GoalKind::Haul:
        case GoalKind::Contest:
            if (goal_.target > 0) {   // a specific crate: gone (taken by someone else), unless I carry it
                const Crate* cr = c.g0.crate(goal_.target);
                if (!cr) return true;
                if (cr->carried && cr->carrier != id_) return true;
            }
            return false;
        case GoalKind::ArmTrap: return goal_targets(c).empty();
        case GoalKind::ClaimTurret: return goal_targets(c).empty();
        case GoalKind::Chase: return c.g0.robot(goal_.target).respawn_pending;
        case GoalKind::None: return true;
    }
    return true;
}

std::vector<Pos> Bot::goal_targets(const Ctx& c) const {
    std::vector<Pos> t;
    const Robot& me = c.me();
    if (me.carrying != 0 && (goal_.kind == GoalKind::Haul || goal_.kind == GoalKind::Contest)) {
        if (auto d = map_.docks[static_cast<std::size_t>(id_)]) t.push_back(*d);
        return t;
    }
    switch (goal_.kind) {
        case GoalKind::Haul:
        case GoalKind::Contest:
            if (goal_.target > 0) if (const Crate* cr = c.g0.crate(goal_.target)) if (!cr->carried) return {cr->pos};
            for (Pos p : c.loose) t.push_back(p);
            if (t.empty()) for (const auto& d : map_.dispensers) t.push_back(d.pos);   // nothing loose: wait where one will appear
            return t;
        case GoalKind::ArmTrap: return trap_reach(map_, goal_.target);
        case GoalKind::ClaimTurret: return turret_pads(map_, goal_.target);
        case GoalKind::Chase: {
            const Robot& r = c.g0.robot(goal_.target);
            if (!r.respawn_pending) t.push_back(r.pos);
            return t;
        }
        case GoalKind::None: return t;
    }
    return t;
}

// Pick a new goal at random from the personality's weights. `family` is Haul / Contest / Chase(=hunt) only to
// say which column of the table was rolled; candidates are weighted towards the cheap ones.
void Bot::choose_goal(Ctx& c, GoalKind family, int exclude_crate) {
    struct Cand { Goal g; int weight; };
    std::vector<Cand> cands;
    const Robot& me = c.me();
    auto weight_for = [](int cost) {
        const int w = 4'000'000 / ((cost + 20) * (cost + 20) / 100 + 1) / 100;
        return std::max(1, w);
    };
    auto fill_crates = [&](int mult) {
        for (const Crate& cr : c.g0.crates) {
            if (cr.carried || cr.id == exclude_crate) continue;
            const int cost = c.from_me[static_cast<std::size_t>(idx(map_, cr.pos))];
            if (cost >= kFar) continue;
            cands.push_back({{family == GoalKind::Contest ? GoalKind::Contest : GoalKind::Haul, cr.id, 0, me.deliveries, me.sabotage}, weight_for(cost) * mult});
        }
    };
    auto fill_devices = [&](int mult) {
        for (const Trap& t : map_.traps) {
            const TrapState* ts = c.g0.trap(t.id);
            if (ts && ts->armed && ts->owner == id_) continue;
            int best = kFar;
            for (Pos p : trap_reach(map_, t.id)) best = std::min(best, c.from_me[static_cast<std::size_t>(idx(map_, p))]);
            if (best < kFar) cands.push_back({{GoalKind::ArmTrap, t.id, 0, me.deliveries, me.sabotage}, weight_for(best) * mult});
        }
        for (const Turret& t : map_.turrets) {
            const TurretState* ts = c.g0.turret(t.id);
            if (ts && ts->owner == id_) continue;
            int best = kFar;
            for (Pos p : turret_pads(map_, t.id)) best = std::min(best, c.from_me[static_cast<std::size_t>(idx(map_, p))]);
            if (best < kFar) cands.push_back({{GoalKind::ClaimTurret, t.id, 0, me.deliveries, me.sabotage}, weight_for(best) * mult});
        }
    };
    auto fill_rivals = [&](int mult, bool loaded_only) {
        for (const Robot& r : c.g0.robots) {
            if (r.id == id_ || r.respawn_pending) continue;
            if (loaded_only && r.carrying == 0) continue;
            const int cost = c.from_me[static_cast<std::size_t>(idx(map_, r.pos))];
            if (cost >= kFar) continue;
            cands.push_back({{GoalKind::Chase, r.id, 0, me.deliveries, me.sabotage}, weight_for(r.carrying ? cost * 6 / 10 : cost) * mult});
        }
    };
    if (family == GoalKind::Haul) {
        fill_crates(1);
    } else if (family == GoalKind::Contest) {
        fill_crates(1); fill_devices(1); fill_rivals(1, true);
    } else {  // hunt: environmental disruption first (devices count x4), then a rival
        fill_devices(4); fill_rivals(1, false);
    }
    if (cands.empty()) fill_crates(1);          // nothing suitable: be useful
    ++goals_chosen_;
    if (cands.empty()) {                        // not even a crate: wait near a dispenser (Haul, target 0)
        goal_ = {GoalKind::Haul, 0, 0, me.deliveries, me.sabotage};
        note("goal candidates: none reachable -> Haul with no target (wait near a dispenser)");
        return;
    }
    int total = 0;
    for (const auto& k : cands) total += k.weight;
    if (explain_) {
        std::string l = std::string("goal candidates (family ") + to_string(family) + ", weight ~ 1/cost^2):";
        for (const auto& k : cands) l += " [" + goal_text(k.g) + " w" + std::to_string(k.weight) + "]";
        note(l);
    }
    int roll = rng_.below(std::max(1, total));
    const int roll0 = roll;
    for (const auto& k : cands) {
        if (roll < k.weight) { goal_ = k.g; note("rolled " + std::to_string(roll0) + "/" + std::to_string(total) + " -> " + goal_text(goal_)); return; }
        roll -= k.weight;
    }
    goal_ = cands.back().g;
    note("rolled " + std::to_string(roll0) + "/" + std::to_string(total) + " -> " + goal_text(goal_));
}

std::array<std::string, 3> Bot::plan(const Json& snap) {
    GameState g0;
    std::string err;
    reasoning_.clear();
    if (!read_world(snap, g0, err) || id_ < 1 || id_ > g0.player_count()) {
        note("could not read the snapshot (" + err + "): fallback shortest-path planner, no look-ahead");
        return plan_by_path(snap);
    }
    const Traits tr = traits_of(personality_);
    std::array<std::string, 3> wait{"WAIT", "WAIT", "WAIT"};
    {
        const Robot& m0 = g0.robot(id_);
        if (m0.respawn_pending || g0.phase == Phase::Finished) {
            goal_ = {};
            have_prediction_ = false;
            was_disabled_ = true;
            note(m0.respawn_pending ? "disabled / respawning: WAIT x3" : "match finished: WAIT x3");
            return wait;
        }
    }

    // ---- the world, as this bot sees it this round
    Ctx c;
    c.g0 = std::move(g0);
    c.me_id = id_;
    const Robot& me0 = c.me();
    const int tiles = static_cast<int>(map_.tiles.size());
    c.hits.assign(static_cast<std::size_t>(tiles), 0);
    for (Pos e : map_.lasers)   // continuous lasers hit whoever stands in the beam each tick
        for (Pos p = step(e, map_.tile(e).dir); !map_.blocked(p); p = step(p, map_.tile(e).dir)) c.hits[static_cast<std::size_t>(idx(map_, p))] += 10;
    for (const Trap& t : map_.traps) {   // an armed trap hits the eight tiles around it, once
        const TrapState* ts = c.g0.trap(t.id);
        if (!ts || !ts->armed) continue;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                Pos p{t.pos.x + dx, t.pos.y + dy};
                if ((dx || dy) && map_.in_bounds(p)) c.hits[static_cast<std::size_t>(idx(map_, p))] += 5;   // one burst, shared by the whole ring (about two tiles of it are crossed)
            }
    }
    for (const Turret& t : map_.turrets) {   // a turret shoots 1-2 tiles along each axis; a ready one is likelier to
        const TurretState* ts = c.g0.turret(t.id);
        const int heat = ts ? (ts->charge >= 2 ? 10 : ts->charge == 1 ? 8 : 6) : 6;   // it shoots whoever lingers in range
        for (int d = 0; d < 4; ++d) {
            Dir dd = static_cast<Dir>(d);
            for (int k = 1; k <= 2; ++k) {
                Pos p{t.pos.x + dir_dx(dd) * k, t.pos.y + dir_dy(dd) * k};
                if (map_.blocked(p)) break;
                c.hits[static_cast<std::size_t>(idx(map_, p))] += heat;
            }
        }
    }
    const int hit_tiles = hit_cost_tiles(me0.damage, tr.risk_pct, me0.carrying != 0);
    c.cost.assign(static_cast<std::size_t>(tiles), 10);
    for (int i = 0; i < tiles; ++i) {
        Pos p{i % map_.width, i / map_.width};
        int k = 10 + (map_.in_tar(p) ? 10 : 0) + c.hits[static_cast<std::size_t>(i)] * hit_tiles;
        c.cost[static_cast<std::size_t>(i)] = k;
    }
    c.from_me = forward_field(map_, c.cost, me0.pos);
    for (const auto& cr : c.g0.crates) if (!cr.carried) c.loose.push_back(cr.pos);

    note(std::string("situation: pos (") + std::to_string(me0.pos.x) + "," + std::to_string(me0.pos.y) + ") facing " +
         to_string(me0.heading) + ", carrying " + (me0.carrying ? "crate#" + std::to_string(me0.carrying) : std::string("nothing")) +
         ", damage " + std::to_string(me0.damage) + ", deliveries " + std::to_string(me0.deliveries) + ", sabotage " +
         std::to_string(me0.sabotage) + ", score " + std::to_string(me0.score) + "; hit cost " + std::to_string(hit_tiles) +
         " tiles (risk " + std::to_string(tr.risk_pct) + "%)");
    if (explain_) {
        std::string l = "rivals:";
        for (const Robot& r : c.g0.robots)
            if (r.id != id_) l += " P" + std::to_string(r.id) + (r.respawn_pending ? "(down)" : "@(" + std::to_string(r.pos.x) + "," + std::to_string(r.pos.y) + ")" + (r.carrying ? "+crate" : ""));
        l += "; loose crates:";
        for (const auto& cr : c.g0.crates) if (!cr.carried) l += " #" + std::to_string(cr.id) + "@(" + std::to_string(cr.pos.x) + "," + std::to_string(cr.pos.y) + ")";
        note(l);
    }

    // ---- did last round go to plan? (the prediction assumed the rivals would WAIT)
    bool disrupted = was_disabled_;
    if (have_prediction_)
        disrupted = disrupted || me0.pos != pred_pos_ || (me0.carrying != 0) != pred_carrying_ || me0.damage > pred_damage_;
    if (disrupted && goal_.kind != GoalKind::None) ++disruptions_;
    if (have_prediction_ || was_disabled_)
        note(std::string("last round ") + (disrupted ? "was DISRUPTED" : "went as predicted") + " (predicted (" + std::to_string(pred_pos_.x) + "," +
             std::to_string(pred_pos_.y) + ") carrying=" + (pred_carrying_ ? "yes" : "no") + " damage<=" + std::to_string(pred_damage_) +
             (was_disabled_ ? "; I was disabled" : "") + ")");
    have_prediction_ = false;
    was_disabled_ = false;

    // ---- goal maintenance: re-evaluated every round against the current world
    ++goal_.age;
    const bool carrying = me0.carrying != 0;
    const std::string before = goal_text(goal_);
    if (carrying && goal_.kind != GoalKind::Haul && goal_.kind != GoalKind::Contest) {
        goal_ = {GoalKind::Haul, me0.carrying, 0, me0.deliveries, me0.sabotage};   // a loaded robot goes home
        ++goals_chosen_;
        note("goal: was " + before + "; carrying a crate -> forced Haul");
    } else if (goal_.kind == GoalKind::None || disrupted || goal_done(c) || goal_impossible(c) || goal_.age > tr.patience) {
        if (explain_) {
            std::string why;
            if (goal_.kind == GoalKind::None) why += " no-goal";
            if (disrupted) why += " disrupted";
            if (goal_.kind != GoalKind::None && goal_done(c)) why += " done";
            if (goal_.kind != GoalKind::None && goal_impossible(c)) why += " impossible";
            if (goal_.age > tr.patience) why += " aged-out(" + std::to_string(goal_.age) + ">" + std::to_string(tr.patience) + ")";
            note("goal: was " + before + "; re-choosing because:" + why);
        }
        const int roll = rng_.below(100);
        GoalKind family = roll < tr.w_haul ? GoalKind::Haul : roll < tr.w_haul + tr.w_mixed ? GoalKind::Contest : GoalKind::Chase;
        note(std::string("goal family roll ") + std::to_string(roll) + " -> " + to_string(family));
        choose_goal(c, family);
    } else if (!carrying && (goal_.kind == GoalKind::Haul || goal_.kind == GoalKind::Contest) && goal_.target > 0) {
        // Rivals' positions matter: if a rival is clearly closer to my crate, look for another one.
        if (const Crate* cr = c.g0.crate(goal_.target)) {
            const int mine = c.from_me[static_cast<std::size_t>(idx(map_, cr->pos))];
            int best_rival = kFar;
            for (const Robot& r : c.g0.robots)
                if (r.id != id_ && !r.respawn_pending && r.carrying == 0) best_rival = std::min(best_rival, manhattan(r.pos, cr->pos) * 10);
            if (best_rival + 40 < mine && rng_.below(100) < 60 && c.loose.size() > 1) note("a rival is clearly closer to my crate: looking for another"), choose_goal(c, goal_.kind == GoalKind::Contest ? GoalKind::Contest : GoalKind::Haul, cr->id);
        }
    }
    if (goal_.kind == GoalKind::None) { goal_ = {GoalKind::Haul, 0, 0, me0.deliveries, me0.sabotage}; ++goals_chosen_; }
    if (explain_ && goal_text(goal_) == before) note("goal: kept " + before + " (age " + std::to_string(goal_.age) + "/" + std::to_string(tr.patience) + ")");

    // ---- hazard-weighted distance to the goal
    std::vector<Pos> targets = goal_targets(c);
    Field to_goal = backward_field(map_, c.cost, targets);
    if (to_goal[static_cast<std::size_t>(idx(map_, me0.pos))] >= kFar && goal_.kind != GoalKind::Haul) {
        // Cannot get there from here (never wait on an unreachable device): fall back to useful delivery work.
        note("goal unreachable from here: falling back to Haul");
        choose_goal(c, GoalKind::Haul);
        targets = goal_targets(c);
        to_goal = backward_field(map_, c.cost, targets);
    }
    auto dist_at = [&](Pos p) { return to_goal[static_cast<std::size_t>(idx(map_, p))]; };
    if (explain_) {
        std::string l = "goal " + goal_text(goal_) + ": " + std::to_string(targets.size()) + " target tile(s)";
        for (std::size_t i = 0; i < targets.size() && i < 4; ++i) l += " (" + std::to_string(targets[i].x) + "," + std::to_string(targets[i].y) + ")";
        const int d = dist_at(me0.pos);
        l += "; hazard-weighted distance from me " + (d >= kFar ? std::string("UNREACHABLE") : std::to_string(d / 10) + "." + std::to_string(d % 10) + " tiles");
        note(l);
    }
    auto my_machines = [&](const GameState& h) {
        int n = 0;
        for (const auto& t : h.traps) n += t.armed && t.owner == id_;
        for (const auto& t : h.turrets) n += t.owner == id_;
        return n;
    };
    const int machines0 = my_machines(c.g0);
    const bool device_goal = goal_.kind == GoalKind::ArmTrap || goal_.kind == GoalKind::ClaimTurret;

    EventLog log;
    Engine engine(map_, log);
    const std::size_t n = static_cast<std::size_t>(c.g0.player_count());
    Programmes progs(n, Programme{Order::Wait, Order::Wait, Order::Wait});
    auto near_console = [&](Pos p) {
        for (const auto& k : map_.consoles) if (manhattan(k.pos, p) <= 1) return true;
        for (const auto& k : map_.trap_consoles) if (manhattan(k.pos, p) <= 1) return true;
        return false;
    };

    auto score_state = [&](const GameState& h) {
        const Robot& b = h.robot(id_);
        int s = 0;
        if (b.respawn_pending) return -20000;
        // damage: each further hit costs more as health runs out
        // (the distance field already priced the hazard tile, so only half is charged again here)
        for (int d = me0.damage; d < b.damage; ++d) s -= 50 * hit_cost_tiles(d, tr.risk_pct, me0.carrying != 0);
        if (b.deliveries > me0.deliveries) s += 100000;                 // delivered (not sabotage points)
        if (me0.carrying == 0 && b.carrying != 0) s += 5000;           // picked a crate up
        if (me0.carrying != 0 && b.carrying == 0 && b.deliveries == me0.deliveries) s -= 5000;  // lost it
        s -= 10 * std::min(dist_at(b.pos), 6000);
        if (map_.tile(b.pos).terrain == Terrain::Conveyor) s -= 30;
        // sabotage: points earned (traps, turrets, shunts) and machinery owned
        s += (b.sabotage - me0.sabotage) * 120 * tr.sabotage_pct / 100;
        s += (my_machines(h) - machines0) * (device_goal ? 4000 : goal_.kind == GoalKind::Haul ? 0 : 1200) * tr.sabotage_pct / 100;   // a hauler on a delivery run does not detour to own machines
        if (goal_.kind == GoalKind::ArmTrap) {
            const TrapState* t = h.trap(goal_.target);
            if (t && t->armed && t->owner == id_) s += 8000;
        } else if (goal_.kind == GoalKind::ClaimTurret) {
            const TurretState* t = h.turret(goal_.target);
            if (t && t->owner == id_) s += 8000;
        }
        // exposure: a rival next to me with a pit behind me can shunt me
        for (const Robot& r : h.robots) {
            if (r.id == id_ || r.respawn_pending || manhattan(r.pos, b.pos) != 1) continue;
            Pos behind{b.pos.x + (b.pos.x - r.pos.x), b.pos.y + (b.pos.y - r.pos.y)};
            if (map_.in_bounds(behind) && map_.tile(behind).terrain == Terrain::Pit) s -= 2500;
        }
        return s;
    };

    std::vector<Candidate> all;
    all.reserve(600);
    std::array<Order, 3> cur{};
    static const Order kBase[] = {Order::Forward1, Order::Forward2, Order::Back1, Order::TurnLeft,
                                  Order::TurnRight, Order::TurnAround, Order::Wait};
    auto dfs = [&](auto&& self, int slot, const GameState& g, int redundant) -> void {
        const bool can_toggle = near_console(g.robot(id_).pos);
        for (int oi = 0; oi < 8; ++oi) {
            Order o = oi < 7 ? kBase[oi] : Order::Toggle;
            if (o == Order::Toggle && !can_toggle) continue;
            GameState h = g;
            progs[static_cast<std::size_t>(id_ - 1)][static_cast<std::size_t>(slot)] = o;
            engine.run_tick(h, progs, slot + 1);
            log.take();
            cur[static_cast<std::size_t>(slot)] = o;
            int red = redundant;
            if (o == Order::Toggle && h.traps == g.traps && h.groups == g.groups) ++red;   // nothing flipped, nothing armed
            if (slot < 2) { self(self, slot + 1, h, red); continue; }
            int s = score_state(h) - red * kRedundantToggle;
            if (!h.robot(id_).respawn_pending) {  // one more tick of machinery: does a belt carry us into trouble?
                GameState after = h;
                Programmes idle(n, Programme{Order::Wait, Order::Wait, Order::Wait});
                engine.run_tick(after, idle, 3);
                log.take();
                const Robot& a = after.robot(id_);
                if (a.respawn_pending) s -= 10000;
                else s -= 25 * (a.damage - h.robot(id_).damage) * hit_cost_tiles(h.robot(id_).damage, tr.risk_pct, me0.carrying != 0);
            }
            all.push_back({cur, s});
        }
    };
    dfs(dfs, 0, c.g0, 0);

    // Imperfection: seeded noise of up to ~2.5 tiles of progress, plus an occasional blunder.
    for (auto& cd : all) cd.score += rng_.below(250);
    std::stable_sort(all.begin(), all.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    std::size_t pick = 0;
    if (rng_.below(100) < blunder_percent_) pick = static_cast<std::size_t>(rng_.below(static_cast<int>(std::min<std::size_t>(30, all.size()))));
    std::array<Order, 3> chosen = all[pick].orders;
    std::array<std::string, 3> out;
    if (explain_) {
        note("evaluated " + std::to_string(all.size()) + " three-order programmes (depth-3 search on the real engine, rivals assumed WAIT). Top 6 (score incl. noise):");
        for (std::size_t i = 0; i < all.size() && i < 6; ++i)
            note(std::string("  ") + (i == pick ? "* " : "  ") + to_string(all[i].orders[0]) + " " + to_string(all[i].orders[1]) + " " +
                 to_string(all[i].orders[2]) + "  score " + std::to_string(all[i].score));
        note(pick == 0 ? "picked the best-scoring programme" : "BLUNDER: picked rank " + std::to_string(pick + 1) + " instead of the best");
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (rng_.below(100) < fumble_percent_) {
            const Order was = chosen[i];
            chosen[i] = kBase[rng_.below(7)];  // a slip of the finger
            note("FUMBLE: slot " + std::to_string(i + 1) + " " + to_string(was) + " replaced by " + to_string(chosen[i]));
        }
        out[i] = to_string(chosen[i]);
    }
    if (explain_) {
        auto cancels = [](Order a, Order b) {
            return (a == Order::TurnLeft && b == Order::TurnRight) || (a == Order::TurnRight && b == Order::TurnLeft) ||
                   (a == Order::Forward1 && b == Order::Back1) || (a == Order::Back1 && b == Order::Forward1) ||
                   (a == Order::TurnAround && b == Order::TurnAround);
        };
        if (cancels(chosen[0], chosen[1]) || cancels(chosen[1], chosen[2]))
            note("NOTE: two adjacent chosen orders cancel each other out (net effect nil)");
    }

    // ---- remember what I expect (rivals waiting), to notice next round if something interfered
    {
        GameState h = c.g0;
        for (int slot = 0; slot < 3; ++slot) {
            progs[static_cast<std::size_t>(id_ - 1)][static_cast<std::size_t>(slot)] = chosen[static_cast<std::size_t>(slot)];
            engine.run_tick(h, progs, slot + 1);
            log.take();
        }
        const Robot& b = h.robot(id_);
        have_prediction_ = !b.respawn_pending;
        pred_pos_ = b.pos;
        pred_carrying_ = b.carrying != 0;
        pred_damage_ = b.damage;
        note(std::string("expected after 3 ticks (rivals waiting): ") + (b.respawn_pending ? "disabled" : "(" + std::to_string(b.pos.x) + "," + std::to_string(b.pos.y) + ") facing " + to_string(b.heading)) +
             ", carrying=" + (b.carrying ? "yes" : "no") + ", damage " + std::to_string(b.damage) + ", distance to goal " +
             (b.respawn_pending ? std::string("-") : std::to_string(std::min(dist_at(b.pos), kFar) / 10) + " tiles"));
    }
    return out;
}

}  // namespace botched::sim

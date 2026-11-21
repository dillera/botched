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


// ------------------------------------------------------------------ lookahead planner

namespace {

constexpr int kUnreachable = 60;

// Walking distance (in tiles) from every tile to the nearest of `goals`; pits and walls excluded.
std::vector<int> distance_map(const Map& m, const std::vector<Pos>& goals) {
    std::vector<int> dist(m.tiles.size(), kUnreachable);
    std::queue<Pos> q;
    auto idx = [&](Pos p) { return static_cast<std::size_t>(p.y * m.width + p.x); };
    for (Pos g : goals) { dist[idx(g)] = 0; q.push(g); }
    while (!q.empty()) {
        Pos c = q.front();
        q.pop();
        for (int d = 0; d < 4; ++d) {
            Pos n{c.x + dir_dx(static_cast<Dir>(d)), c.y + dir_dy(static_cast<Dir>(d))};
            if (!m.in_bounds(n) || m.blocked(n) || dist[idx(n)] <= dist[idx(c)] + 1) continue;
            dist[idx(n)] = dist[idx(c)] + 1;
            if (m.walkable(n)) q.push(n);  // a pit may be a goal-adjacent cell but is never routed through
        }
    }
    return dist;
}

struct Candidate {
    std::array<Order, 3> orders;
    int score;
};

}  // namespace

std::array<std::string, 3> Bot::plan(const Json& snap) {
    GameState g0;
    std::string err;
    if (!read_world(snap, g0, err) || id_ < 1 || id_ > g0.player_count()) return plan_by_path(snap);
    const Robot& me0 = g0.robot(id_);
    std::array<std::string, 3> wait{"WAIT", "WAIT", "WAIT"};
    if (me0.respawn_pending || g0.phase == Phase::Finished) return wait;

    // Goals: the own dock when carrying, otherwise any loose crate.
    std::vector<Pos> crates;
    for (const auto& c : g0.crates) if (!c.carried) crates.push_back(c.pos);
    std::vector<Pos> docks;
    if (auto d = map_.docks[static_cast<std::size_t>(id_)]) docks.push_back(*d);
    const std::vector<int> to_crate = distance_map(map_, crates), to_dock = distance_map(map_, docks);
    auto dist_at = [&](const std::vector<int>& dm, Pos p) { return dm[static_cast<std::size_t>(p.y * map_.width + p.x)]; };

    EventLog log;
    Engine engine(map_, log);
    const std::size_t n = static_cast<std::size_t>(g0.player_count());
    Programmes progs(n, Programme{Order::Wait, Order::Wait, Order::Wait});
    static const Order kOptions[] = {Order::Forward1, Order::Forward2, Order::Back1, Order::TurnLeft,
                                     Order::TurnRight, Order::TurnAround, Order::Wait};

    auto score_state = [&](const GameState& h) {
        const Robot& b = h.robot(id_);
        int s = 0;
        if (b.respawn_pending) return -20000;
        s -= 3000 * std::max(0, b.damage - me0.damage);
        if (b.score > me0.score) s += 100000;                           // delivered
        if (me0.carrying == 0 && b.carrying != 0) s += 5000;           // picked a crate up
        if (me0.carrying != 0 && b.carrying == 0 && b.score == me0.score) s -= 5000;  // lost it
        const int d = b.carrying != 0 ? dist_at(to_dock, b.pos) : dist_at(to_crate, b.pos);
        s -= 100 * d;
        if (map_.tile(b.pos).terrain == Terrain::Conveyor) s -= 30;
        return s;
    };

    std::vector<Candidate> all;
    all.reserve(343);
    std::array<Order, 3> cur{};
    auto dfs = [&](auto&& self, int slot, const GameState& g) -> void {
        for (Order o : kOptions) {
            GameState h = g;
            progs[static_cast<std::size_t>(id_ - 1)][static_cast<std::size_t>(slot)] = o;
            engine.run_tick(h, progs, slot + 1);
            log.take();
            cur[static_cast<std::size_t>(slot)] = o;
            if (slot < 2) { self(self, slot + 1, h); continue; }
            int s = score_state(h);
            if (!h.robot(id_).respawn_pending) {  // one more tick of machinery: does a belt carry us into trouble?
                GameState after = h;
                Programmes idle(n, Programme{Order::Wait, Order::Wait, Order::Wait});
                engine.run_tick(after, idle, 3);
                log.take();
                const Robot& a = after.robot(id_);
                if (a.respawn_pending) s -= 10000;
                else s -= 1500 * std::max(0, a.damage - h.robot(id_).damage);
            }
            all.push_back({cur, s});
        }
    };
    dfs(dfs, 0, g0);

    // Imperfection: seeded noise of up to ~1.5 tiles of progress, plus an occasional blunder.
    for (auto& c : all) c.score += rng_.below(250);
    std::stable_sort(all.begin(), all.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    std::size_t pick = 0;
    if (rng_.below(100) < blunder_percent_) pick = static_cast<std::size_t>(rng_.below(static_cast<int>(std::min<std::size_t>(30, all.size()))));
    std::array<std::string, 3> out;
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = to_string(all[pick].orders[i]);
        if (rng_.below(100) < fumble_percent_) out[i] = to_string(kOptions[rng_.below(7)]);  // a slip of the finger
    }
    return out;
}

}  // namespace botched::sim

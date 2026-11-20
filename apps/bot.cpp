#include "bot.hpp"

#include <algorithm>
#include <optional>
#include <queue>

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

std::array<std::string, 3> Bot::plan(const Json& snap) {
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

}  // namespace botched::sim

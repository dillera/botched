// Test rig: a hand-built board + state that runs single ticks through the real engine.
#pragma once

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

#include "botched/engine.hpp"
#include "botched/map.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"

namespace rig {

using namespace botched;

struct Rig {
    Map map;
    GameState s;
    EventLog log;
    Programmes progs;

    // `text` is parsed with parse_map (no section 8 validation, any size). Robots start at
    // their spawn if the map has one, otherwise at (0,0); use place() to position them.
    explicit Rig(const std::string& text, int players = 2) {
        auto r = parse_map("test", text);
        REQUIRE_MESSAGE(r.map.has_value(), (r.errors.empty() ? "parse failed" : r.errors[0]));
        map = *r.map;
        for (int p = 1; p <= players; ++p) {
            Robot rb;
            rb.id = p;
            if (map.spawns[static_cast<std::size_t>(p)]) {
                rb.pos = *map.spawns[static_cast<std::size_t>(p)];
                rb.heading = map.tile(rb.pos).dir;
            }
            s.robots.push_back(rb);
        }
        for (char g : map.groups) s.groups.push_back({g, false});
        s.round = 1;
        progs.assign(static_cast<std::size_t>(players), Programme{Order::Wait, Order::Wait, Order::Wait});
    }

    Robot& place(int p, int x, int y, Dir d = Dir::E) {
        Robot& r = s.robot(p);
        r.pos = {x, y};
        r.heading = d;
        r.respawn_pending = false;
        return r;
    }
    Crate& loose(int id, int x, int y) {
        Crate c;
        c.id = id;
        c.pos = {x, y};
        s.crates.push_back(c);
        std::sort(s.crates.begin(), s.crates.end(), [](const Crate& a, const Crate& b) { return a.id < b.id; });
        if (id >= s.next_crate_id) s.next_crate_id = id + 1;
        return *s.crate(id);
    }
    void give(int p, int crate_id) {
        Crate* c = s.crate(crate_id);
        if (!c) { loose(crate_id, 0, 0); c = s.crate(crate_id); }
        c->carried = true;
        c->carrier = p;
        s.robot(p).carrying = crate_id;
    }
    void program(int p, std::initializer_list<Order> o) {
        Programme pr{Order::Wait, Order::Wait, Order::Wait};
        std::size_t i = 0;
        for (Order x : o) pr[i++] = x;
        progs[static_cast<std::size_t>(p - 1)] = pr;
    }
    // Runs tick `t` of the current round.
    void tick(int t = 1) {
        Engine e(map, log);
        e.run_tick(s, progs, t);
    }
    void ticks(int n) { for (int t = 1; t <= n; ++t) tick(t); }

    std::vector<std::string> types() const {
        std::vector<std::string> v;
        for (const auto& e : log.events()) v.push_back(e.type);
        return v;
    }
    int count(const std::string& type) const {
        int n = 0;
        for (const auto& e : log.events()) if (e.type == type) ++n;
        return n;
    }
    // Index of the first event of `type` or -1.
    int index_of(const std::string& type) const {
        int i = 0;
        for (const auto& e : log.events()) { if (e.type == type) return i; ++i; }
        return -1;
    }
    Pos at(int p) const { return s.robot(p).pos; }
    Engine engine() { return Engine(map, log); }
};

// 8x6 open room with a wall border; no machinery.
inline const char* kRoom =
    "# # # # # # # #\n"
    "# . . . . . . #\n"
    "# . . . . . . #\n"
    "# . . . . . . #\n"
    "# . . . . . . #\n"
    "# # # # # # # #\n";

inline Json ev_param(const Event& e, const char* key) {
    const Json* j = e.params.find(key);
    return j ? *j : Json(nullptr);
}

}  // namespace rig

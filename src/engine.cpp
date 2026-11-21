#include "botched/engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <set>
#include <tuple>
#include <utility>

namespace botched {

namespace {

Json obj(std::initializer_list<std::pair<const char*, Json>> kv) {
    Json o = Json::object();
    for (const auto& p : kv) o.set(p.first, p.second);
    return o;
}

Pos step(Pos p, Dir d) { return {p.x + dir_dx(d), p.y + dir_dy(d)}; }

Dir conveyor_dir(const Map& map, const GameState& s, Pos p) {
    const Tile& t = map.tile(p);
    const Group* g = s.group(t.group);
    return (g && g->reversed) ? opposite(t.dir) : t.dir;
}

Json scores_json(const GameState& s) {
    Json a = Json::array();
    for (const auto& r : s.robots)
        a.push(obj({{"player", r.id}, {"score", r.score}, {"deliveries", r.deliveries},
                    {"damage_taken", r.damage_taken}}));
    return a;
}

}  // namespace

std::vector<int> priority_order(int abs_tick, int player_count) {
    std::vector<int> v;
    int start = ((abs_tick - 1) % player_count) + 1;
    for (int i = 0; i < player_count; ++i) v.push_back(((start - 1 + i) % player_count) + 1);
    return v;
}

GameState make_initial_state(const Map& map, int player_count, int rounds, int target_score) {
    GameState s;
    s.rounds_total = rounds;
    s.target_score = target_score;
    for (int p = 1; p <= player_count; ++p) {
        Robot r;
        r.id = p;
        Pos sp = *map.spawns[static_cast<std::size_t>(p)];
        r.pos = sp;
        r.heading = map.tile(sp).dir;
        s.robots.push_back(r);
    }
    for (char g : map.groups) s.groups.push_back({g, false});
    for (const Trap& t : map.traps) s.traps.push_back({t.id, true, 0});  // armed and neutral
    return s;
}

std::vector<int> compute_winners(const GameState& s) {
    auto key = [](const Robot& r) { return std::make_tuple(r.score, r.deliveries, -r.damage_taken); };
    std::vector<int> w;
    for (const auto& r : s.robots) {
        if (w.empty() || key(r) > key(s.robot(w[0]))) {
            w.assign(1, r.id);
        } else if (key(r) == key(s.robot(w[0]))) {
            w.push_back(r.id);
        }
    }
    return w;
}

// ---------------------------------------------------------------- tile effects

void Engine::pick_up_and_deliver(GameState& s, int pid) {
    Robot& r = s.robot(pid);
    if (r.respawn_pending) return;
    if (r.carrying == 0) {
        if (Crate* c = s.loose_crate_at(r.pos)) {
            c->carried = true;
            c->carrier = pid;
            r.carrying = c->id;
            log_.emit("CRATE_PICKED_UP", obj({{"player", pid}, {"crate", c->id}, {"x", r.pos.x}, {"y", r.pos.y}}));
        }
    }
    deliver_if_on_dock(s, pid);
}

void Engine::deliver_if_on_dock(GameState& s, int pid) {
    Robot& r = s.robot(pid);
    if (r.respawn_pending) return;
    const Tile& t = map_.tile(r.pos);
    if (r.carrying != 0 && t.terrain == Terrain::Dock && t.owner == pid) {
        int cid = r.carrying;
        s.remove_crate(cid);
        r.carrying = 0;
        r.score += kDeliveryScore;
        r.deliveries += 1;
        log_.emit("CRATE_DELIVERED",
                  obj({{"player", pid}, {"crate", cid}, {"x", r.pos.x}, {"y", r.pos.y}, {"score", r.score}}));
    }
}

void Engine::disable_robot(GameState& s, int pid, Pos drop_cell, const char* cause, const TrapKill* kill) {
    Robot& r = s.robot(pid);
    if (kill)  // attributed disable: say which trap and whose it was (0 = neutral)
        log_.emit("ROBOT_DISABLED", obj({{"player", pid}, {"cause", cause}, {"x", r.pos.x}, {"y", r.pos.y},
                                         {"trap", kill->trap}, {"by", kill->owner}}));
    else
        log_.emit("ROBOT_DISABLED", obj({{"player", pid}, {"cause", cause}, {"x", r.pos.x}, {"y", r.pos.y}}));
    if (r.carrying != 0) {
        int cid = r.carrying;
        if (s.loose_crate_at(drop_cell) == nullptr) {
            Crate* c = s.crate(cid);
            c->carried = false;
            c->carrier = 0;
            c->pos = drop_cell;
            log_.emit("CRATE_DROPPED", obj({{"player", pid}, {"crate", cid}, {"x", drop_cell.x}, {"y", drop_cell.y}}));
        } else {
            s.remove_crate(cid);
            log_.emit("CRATE_DESTROYED",
                      obj({{"crate", cid}, {"x", drop_cell.x}, {"y", drop_cell.y}, {"reason", "DROP_BLOCKED"}}));
        }
        r.carrying = 0;
    }
    r.respawn_pending = true;
}

void Engine::resolve_tile(GameState& s, int pid, Pos last_safe) {
    Robot& r = s.robot(pid);
    if (r.respawn_pending) return;
    if (map_.tile(r.pos).terrain == Terrain::Pit) {
        disable_robot(s, pid, last_safe, "PIT");
        return;
    }
    pick_up_and_deliver(s, pid);
}

// ---------------------------------------------------------------- movement (5.2)

bool Engine::move_robot(GameState& s, int pid, Dir d, Cause cause) {
    Robot& a = s.robot(pid);
    Pos target = step(a.pos, d);
    std::vector<int> chain{pid};
    Pos cur = target;
    for (;;) {
        if (map_.blocked(cur)) {
            log_.emit("MOVE_BLOCKED", obj({{"player", pid}, {"dir", to_string(d)}, {"x", target.x},
                                          {"y", target.y}, {"cause", cause == Cause::Order ? "ORDER" : "CONVEYOR"}}));
            return false;
        }
        Robot* b = s.robot_at(cur);
        if (!b) break;
        chain.push_back(b->id);
        cur = step(cur, d);
    }

    // Legal: apply the whole chain atomically, farthest first.
    std::vector<Pos> prev(chain.size());
    for (std::size_t i = chain.size(); i-- > 0;) {
        Robot& r = s.robot(chain[i]);
        prev[i] = r.pos;
        r.pos = step(r.pos, d);
        moved_this_phase_.push_back(chain[i]);
    }
    for (std::size_t i = chain.size(); i-- > 1;) {
        const Robot& r = s.robot(chain[i]);
        log_.emit("ROBOT_PUSHED", obj({{"player", r.id}, {"by", pid}, {"from_x", prev[i].x}, {"from_y", prev[i].y},
                                       {"x", r.pos.x}, {"y", r.pos.y}}));
    }
    log_.emit("ROBOT_MOVED", obj({{"player", pid}, {"from_x", prev[0].x}, {"from_y", prev[0].y},
                                  {"x", a.pos.x}, {"y", a.pos.y},
                                  {"cause", cause == Cause::Order ? "ORDER" : "CONVEYOR"}}));
    // Tile effects: farthest pushed robot back to the active robot.
    for (std::size_t i = chain.size(); i-- > 0;) resolve_tile(s, chain[i], prev[i]);
    return true;
}

// ---------------------------------------------------------------- orders (4, 5.1 phase 1)

void Engine::drop(GameState& s, int pid) {
    Robot& r = s.robot(pid);
    if (r.carrying == 0) {
        log_.emit("ORDER_NO_OP", obj({{"player", pid}, {"order", "DROP"}, {"reason", "NOT_CARRYING"}}));
        return;
    }
    if (s.loose_crate_at(r.pos) != nullptr) {
        log_.emit("ORDER_NO_OP", obj({{"player", pid}, {"order", "DROP"}, {"reason", "TILE_OCCUPIED"}}));
        return;
    }
    log_.emit("ORDER_EXECUTED", obj({{"player", pid}, {"order", "DROP"}}));
    Crate* c = s.crate(r.carrying);
    c->carried = false;
    c->carrier = 0;
    c->pos = r.pos;
    log_.emit("CRATE_DROPPED", obj({{"player", pid}, {"crate", c->id}, {"x", r.pos.x}, {"y", r.pos.y}}));
    r.carrying = 0;
}

// G2-A: TOGGLE next to a trap console arms (or re-arms) the trap and makes the robot's player its owner.
// Re-arming an armed trap only changes ownership; whether it fires is decided by who stands next to it
// when the machinery phase runs.
void Engine::arm_trap(GameState& s, int pid, const TrapConsole& console) {
    TrapState* ts = s.trap(console.trap);
    const Trap* dev = nullptr;
    for (const auto& t : map_.traps) if (t.id == console.trap) dev = &t;
    if (!ts || !dev) return;
    const bool was_armed = ts->armed;
    const int previous = ts->owner;
    ts->armed = true;
    ts->owner = pid;
    log_.emit("TRAP_ARMED", obj({{"player", pid}, {"trap", ts->id}, {"x", dev->pos.x}, {"y", dev->pos.y},
                                 {"was_armed", was_armed}, {"previous_owner", previous}, {"owner", pid}}));
}

void Engine::toggle(GameState& s, int pid) {
    const Robot& r = s.robot(pid);
    const Console* best = nullptr;
    for (const auto& c : map_.consoles) {
        if (std::abs(c.pos.x - r.pos.x) + std::abs(c.pos.y - r.pos.y) != 1) continue;
        if (!best || c.id < best->id) best = &c;
    }
    if (!best) {
        // G2-A: no belt console next to the robot; a trap console (lowest trap id, then scan order) may be.
        const TrapConsole* tc = nullptr;
        for (const auto& c : map_.trap_consoles) {
            if (std::abs(c.pos.x - r.pos.x) + std::abs(c.pos.y - r.pos.y) != 1) continue;
            if (!tc || c.trap < tc->trap) tc = &c;
        }
        if (tc) {
            log_.emit("ORDER_EXECUTED", obj({{"player", pid}, {"order", "TOGGLE"}}));
            arm_trap(s, pid, *tc);
            return;
        }
        log_.emit("ORDER_NO_OP", obj({{"player", pid}, {"order", "TOGGLE"}, {"reason", "NO_CONSOLE"}}));
        return;
    }
    log_.emit("ORDER_EXECUTED", obj({{"player", pid}, {"order", "TOGGLE"}}));
    Group* g = s.group(best->group);
    g->reversed = !g->reversed;
    log_.emit("GROUP_TOGGLED", obj({{"player", pid}, {"group", std::string(1, g->id)}, {"console", best->id},
                                    {"reversed", g->reversed}}));
}

void Engine::run_order(GameState& s, int pid, Order o, int /*slot*/) {
    Robot& r = s.robot(pid);
    auto executed = [&] { log_.emit("ORDER_EXECUTED", obj({{"player", pid}, {"order", to_string(o)}})); };
    auto rotate = [&](Dir to) {
        executed();
        Dir from = r.heading;
        r.heading = to;
        log_.emit("ROBOT_ROTATED", obj({{"player", pid}, {"from", to_string(from)}, {"to", to_string(to)}}));
    };
    switch (o) {
        case Order::Forward1:
            executed();
            move_robot(s, pid, r.heading, Cause::Order);
            break;
        case Order::Forward2:
            executed();
            for (int i = 0; i < 2; ++i) {
                if (r.respawn_pending) break;
                if (!move_robot(s, pid, r.heading, Cause::Order)) break;
            }
            break;
        case Order::Back1:
            executed();
            move_robot(s, pid, opposite(r.heading), Cause::Order);
            break;
        case Order::TurnLeft: rotate(turn_left(r.heading)); break;
        case Order::TurnRight: rotate(turn_right(r.heading)); break;
        case Order::TurnAround: rotate(opposite(r.heading)); break;
        case Order::Drop: drop(s, pid); break;
        case Order::Toggle: toggle(s, pid); break;
        case Order::Wait: executed(); break;
    }
}

// ---------------------------------------------------------------- machinery

void Engine::phase_conveyors(GameState& s) {
    struct RobotMove { int pid; Dir dir; };
    struct CrateMove { int cid; Dir dir; };
    std::vector<RobotMove> rm;
    std::vector<CrateMove> cm;
    int abs_tick = absolute_tick(s.round, s.tick);
    for (int pid : priority_order(abs_tick, s.player_count())) {
        const Robot& r = s.robot(pid);
        if (r.respawn_pending) continue;
        if (map_.tile(r.pos).terrain == Terrain::Conveyor) rm.push_back({pid, conveyor_dir(map_, s, r.pos)});
    }
    for (const auto& c : s.crates) {  // ascending id
        if (c.carried) continue;
        if (map_.tile(c.pos).terrain == Terrain::Conveyor) cm.push_back({c.id, conveyor_dir(map_, s, c.pos)});
    }

    moved_this_phase_.clear();
    for (const auto& m : rm) {
        const Robot& r = s.robot(m.pid);
        if (r.respawn_pending) continue;
        // A robot already displaced this phase has used its one move.
        if (std::find(moved_this_phase_.begin(), moved_this_phase_.end(), m.pid) != moved_this_phase_.end()) continue;
        move_robot(s, m.pid, m.dir, Cause::Conveyor);
    }
    moved_this_phase_.clear();

    for (const auto& m : cm) {
        Crate* c = s.crate(m.cid);
        if (!c || c->carried) continue;  // picked up by a robot that arrived this phase
        Pos from = c->pos;
        Pos to = step(from, m.dir);
        if (map_.blocked(to) || s.loose_crate_at(to) != nullptr) continue;
        c->pos = to;
        log_.emit("CRATE_MOVED", obj({{"crate", c->id}, {"from_x", from.x}, {"from_y", from.y}, {"x", to.x},
                                      {"y", to.y}}));
        if (map_.tile(to).terrain == Terrain::Pit) {
            int cid = c->id;
            s.remove_crate(cid);
            log_.emit("CRATE_DESTROYED", obj({{"crate", cid}, {"x", to.x}, {"y", to.y}, {"reason", "PIT"}}));
            continue;
        }
        if (Robot* r = s.robot_at(to)) pick_up_and_deliver(s, r->id);
    }
}

void Engine::phase_lasers(GameState& s) {
    std::set<int> hit;
    for (Pos e : map_.lasers) {  // ascending (y,x)
        Dir d = map_.tile(e).dir;
        for (Pos p = step(e, d); !map_.blocked(p); p = step(p, d)) {
            Robot* r = s.robot_at(p);
            if (!r || hit.count(r->id)) continue;
            hit.insert(r->id);
            r->damage += 1;
            r->damage_taken += 1;
            log_.emit("LASER_HIT", obj({{"player", r->id}, {"emitter_x", e.x}, {"emitter_y", e.y},
                                        {"damage", r->damage}}));
        }
    }
}

void Engine::award_sabotage(GameState& s, int owner, int points, const char* reason, int victim, int trap) {
    Robot& o = s.robot(owner);
    o.score += points;
    o.sabotage += points;
    log_.emit("SCORE_AWARDED", obj({{"player", owner}, {"points", points}, {"reason", reason}, {"victim", victim},
                                    {"trap", trap}, {"score", o.score}}));
}

// G2-A. After orders, conveyors and lasers: every armed trap with at least one robot on its eight
// neighbouring tiles fires once, hits every such robot for 1 damage (its owner included) and disarms.
// Points are awarded for damage actually dealt to an opponent of the trap's owner; neutral traps and
// self-damage score nothing. A robot already at 3 damage (felled by a laser this tick) takes no further
// damage, so a disable is attributed to exactly one source.
void Engine::phase_traps(GameState& s) {
    for (const Trap& t : map_.traps) {  // ascending id
        TrapState* ts = s.trap(t.id);
        if (!ts || !ts->armed) continue;
        std::vector<int> caught;
        for (const Robot& r : s.robots) {  // ascending player id
            if (r.respawn_pending) continue;
            const int dx = std::abs(r.pos.x - t.pos.x), dy = std::abs(r.pos.y - t.pos.y);
            if (std::max(dx, dy) == 1) caught.push_back(r.id);
        }
        if (caught.empty()) continue;
        ts->armed = false;
        Json victims = Json::array();
        for (int pid : caught) victims.push(pid);
        log_.emit("TRAP_TRIGGERED", obj({{"trap", t.id}, {"x", t.pos.x}, {"y", t.pos.y}, {"owner", ts->owner},
                                         {"victims", victims}}));
        for (int pid : caught) {
            Robot& r = s.robot(pid);
            if (r.damage >= kMaxHealth) continue;
            r.damage += 1;
            r.damage_taken += 1;
            const int owner = ts->owner;
            const int points = (owner != 0 && owner != pid) ? 1 : 0;
            log_.emit("TRAP_HIT", obj({{"trap", t.id}, {"player", pid}, {"damage", r.damage}, {"owner", owner},
                                       {"points", points}}));
            if (points) award_sabotage(s, owner, points, "TRAP_DAMAGE", pid, t.id);
            if (r.damage >= kMaxHealth) trap_kills_.push_back({pid, t.id, owner});
        }
    }
}

void Engine::phase_disable(GameState& s) {
    for (auto& r : s.robots) {
        if (r.respawn_pending || r.damage < kMaxHealth) continue;
        const TrapKill* kill = nullptr;
        for (const auto& k : trap_kills_) if (k.victim == r.id) kill = &k;
        disable_robot(s, r.id, r.pos, kill ? "TRAP" : "LASER", kill);
        if (kill && kill->owner != 0 && kill->owner != r.id) award_sabotage(s, kill->owner, 3, "TRAP_DISABLE", r.id, kill->trap);
    }
}

void Engine::run_tick(GameState& s, const Programmes& programmes, int tick) {
    s.phase = Phase::Executing;
    s.tick = tick;
    trap_kills_.clear();
    log_.begin(s.round, tick);
    const int abs_tick = absolute_tick(s.round, tick);

    // Phase 1: robot orders in rotating priority.
    for (int pid : priority_order(abs_tick, s.player_count())) {
        Robot& r = s.robot(pid);
        Order o = programmes[static_cast<std::size_t>(pid - 1)][static_cast<std::size_t>(tick - 1)];
        if (r.respawn_pending) {
            log_.emit("ORDER_SKIPPED_DISABLED", obj({{"player", pid}, {"order", to_string(o)}}));
            continue;
        }
        deliver_if_on_dock(s, pid);  // start-of-order delivery (5.3); never picks up
        run_order(s, pid, o, tick);
    }
    phase_conveyors(s);   // Phase 2
    phase_lasers(s);      // Phase 3
    phase_traps(s);       // Phase 3b (G2-A): proximity traps, after lasers and before disables
    phase_disable(s);     // Phase 4
}

// ---------------------------------------------------------------- lifecycle

void Engine::process_respawns(GameState& s) {
    for (auto& r : s.robots) {
        if (!r.respawn_pending) continue;
        Pos spawn = *map_.spawns[static_cast<std::size_t>(r.id)];
        std::optional<Pos> place;
        if (s.robot_at(spawn) == nullptr) {
            place = spawn;
        } else {
            int best = 1 << 30;
            for (int y = 0; y < map_.height; ++y) {
                for (int x = 0; x < map_.width; ++x) {
                    Pos p{x, y};
                    if (!map_.walkable(p) || s.robot_at(p)) continue;
                    int dist = std::abs(x - spawn.x) + std::abs(y - spawn.y);
                    if (dist < best) { best = dist; place = p; }  // scan is (y,x) ascending: first wins ties
                }
            }
            if (place)
                log_.emit("RESPAWN_REDIRECTED", obj({{"player", r.id}, {"spawn_x", spawn.x}, {"spawn_y", spawn.y},
                                                     {"x", place->x}, {"y", place->y}}));
        }
        if (!place) continue;  // stays pending; retried next round
        r.pos = *place;
        r.heading = map_.tile(spawn).dir;
        r.damage = 0;
        r.carrying = 0;
        r.respawn_pending = false;
        log_.emit("ROBOT_RESPAWNED", obj({{"player", r.id}, {"x", r.pos.x}, {"y", r.pos.y},
                                          {"heading", to_string(r.heading)}}));
    }
}

void Engine::start_round(GameState& s) {
    s.round += 1;
    s.tick = 0;
    s.phase = Phase::Planning;
    log_.begin(s.round, 0);
    for (const auto& d : map_.dispensers) {
        if (s.loose_crate_at(d.pos) || s.robot_at(d.pos)) continue;
        Crate c;
        c.id = s.next_crate_id++;
        c.pos = d.pos;
        s.crates.push_back(c);
        log_.emit("CRATE_SPAWNED", obj({{"crate", c.id}, {"dispenser", d.id}, {"x", d.pos.x}, {"y", d.pos.y}}));
    }
    process_respawns(s);
    log_.emit("ROUND_STARTED", obj({{"round", s.round}}));
}

void Engine::end_round(GameState& s) {
    log_.begin(s.round, kRegisters);
    process_respawns(s);
    log_.emit("ROUND_ENDED", obj({{"round", s.round}, {"scores", scores_json(s)}}));
    // Victory is judged only here, after all three ticks and the round-end effects, so rotating tick
    // priority can never decide who "got there first".
    bool target_hit = false;
    if (s.target_score > 0)
        for (const auto& r : s.robots) target_hit = target_hit || r.score >= s.target_score;
    if (target_hit || s.round >= s.rounds_total) {
        s.phase = Phase::Finished;
        Json w = Json::array();
        for (int p : compute_winners(s)) w.push(p);
        Json ev = obj({{"winners", w}, {"scores", scores_json(s)}});
        if (s.target_score > 0) ev.set("reason", target_hit ? "target" : "cap");  // fixed-round events are unchanged
        log_.emit("MATCH_ENDED", std::move(ev));
    }
}

}  // namespace botched

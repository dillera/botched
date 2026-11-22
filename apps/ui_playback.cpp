#include "ui_playback.hpp"

#include <algorithm>
#include "botched/engine.hpp"
#include <cstdio>

namespace botched::ui {

namespace {
int gi(const Json& p, const char* k) { return static_cast<int>(p.get_int(k).value_or(0)); }
const Json& params(const Json& e) {
    static const Json none = Json::object();
    const Json* p = e.find("params");
    return p ? *p : none;
}
std::string type_of(const Json& e) { return e.get_string("type").value_or(""); }
}  // namespace

// ------------------------------------------------------------------ board view

RobotView* BoardView::robot(int id) {
    for (auto& r : robots) if (r.id == id) return &r;
    return nullptr;
}
const RobotView* BoardView::robot(int id) const {
    for (const auto& r : robots) if (r.id == id) return &r;
    return nullptr;
}
const RobotView* BoardView::robot_at(int x, int y) const {
    for (const auto& r : robots) if (r.active && r.x == x && r.y == y) return &r;
    return nullptr;
}
const TrapView* BoardView::trap(int id) const {
    for (const auto& t : traps) if (t.id == id) return &t;
    return nullptr;
}
const TurretView* BoardView::turret(int id) const {
    for (const auto& t : turrets) if (t.id == id) return &t;
    return nullptr;
}
const CrateView* BoardView::crate_at(int x, int y) const {
    for (const auto& c : crates) if (c.x == x && c.y == y) return &c;
    return nullptr;
}

std::string BoardView::diff(const BoardView& o) const {
    std::string d;
    auto add = [&](const std::string& s) { d += s + "; "; };
    if (robots.size() != o.robots.size()) add("robot count");
    for (std::size_t i = 0; i < robots.size() && i < o.robots.size(); ++i) {
        const auto &a = robots[i], &b = o.robots[i];
        const std::string n = "P" + std::to_string(a.id) + " ";
        if (a.id != b.id) add(n + "id");
        if (a.active != b.active) add(n + "active");
        if (a.active && b.active && (a.x != b.x || a.y != b.y)) add(n + "position");
        if (a.heading != b.heading) add(n + "heading");
        if (a.carry_id != b.carry_id) add(n + "carry");
        if (a.damage != b.damage) add(n + "damage");
        if (a.score != b.score) add(n + "score");
        if (a.deliveries != b.deliveries) add(n + "deliveries");
        if (a.disabled != b.disabled) add(n + "disabled");
        if (a.sabotage != b.sabotage) add(n + "sabotage");
    }
    auto sorted = [](std::vector<CrateView> v) {
        std::sort(v.begin(), v.end(), [](const CrateView& a, const CrateView& b) { return a.id < b.id; });
        return v;
    };
    auto ca = sorted(crates), cb = sorted(o.crates);
    if (ca.size() != cb.size()) add("crate count");
    else for (std::size_t i = 0; i < ca.size(); ++i)
        if (ca[i].id != cb[i].id || ca[i].x != cb[i].x || ca[i].y != cb[i].y) add("crate " + std::to_string(ca[i].id));
    if (reversed != o.reversed) add("belt groups");
    if (traps.size() != o.traps.size()) add("trap count");
    else for (std::size_t i = 0; i < traps.size(); ++i)
        if (traps[i].id != o.traps[i].id || traps[i].armed != o.traps[i].armed || traps[i].owner != o.traps[i].owner)
            add("trap " + std::to_string(traps[i].id));
    if (turrets.size() != o.turrets.size()) add("turret count");
    else for (std::size_t i = 0; i < turrets.size(); ++i)
        if (turrets[i].id != o.turrets[i].id || turrets[i].charge != o.turrets[i].charge || turrets[i].owner != o.turrets[i].owner)
            add("turret " + std::to_string(turrets[i].id));
    return d;
}
bool BoardView::equals(const BoardView& o) const { return diff(o).empty(); }

BoardView read_board(const Json& snap) {
    BoardView v;
    if (const Json* ps = snap.find("players"))
        for (const auto& p : ps->items()) {
            RobotView r;
            r.id = gi(p, "id");
            auto x = p.get_int("x"), y = p.get_int("y");
            r.active = x && y;
            r.x = static_cast<int>(x.value_or(0));
            r.y = static_cast<int>(y.value_or(0));
            r.heading = parse_dir(p.get_string("heading").value_or("N")).value_or(Dir::N);
            r.carry_id = static_cast<int>(p.get_int("carrying").value_or(0));
            r.damage = gi(p, "damage");
            r.score = gi(p, "score");
            r.deliveries = gi(p, "deliveries");
            r.disabled = p.get_bool("respawn_pending").value_or(false);
            r.sabotage = gi(p, "sabotage");
            v.robots.push_back(r);
        }
    if (const Json* cs = snap.find("crates"))
        for (const auto& c : cs->items()) {
            auto x = c.get_int("x"), y = c.get_int("y");
            if (x && y) v.crates.push_back({gi(c, "id"), static_cast<int>(*x), static_cast<int>(*y)});
        }
    if (const Json* ts = snap.find("traps"))
        for (const auto& t : ts->items()) v.traps.push_back({gi(t, "id"), t.get_bool("armed").value_or(true), gi(t, "owner")});
    if (const Json* ts = snap.find("turrets"))
        for (const auto& t : ts->items()) v.turrets.push_back({gi(t, "id"), gi(t, "charge"), gi(t, "owner")});
    if (const Json* gs = snap.find("groups"))
        for (const auto& g : gs->items()) {
            auto id = g.get_string("id");
            if (id && id->size() == 1 && (*id)[0] >= 'A' && (*id)[0] <= 'Z')
                v.reversed[static_cast<std::size_t>((*id)[0] - 'A')] = g.get_bool("reversed").value_or(false);
        }
    return v;
}

void apply_event(BoardView& b, const Json& e) {
    const std::string t = type_of(e);
    const Json& p = params(e);
    auto erase_crate = [&](int id) {
        b.crates.erase(std::remove_if(b.crates.begin(), b.crates.end(), [&](const CrateView& c) { return c.id == id; }), b.crates.end());
    };
    auto put_crate = [&](int id, int x, int y) {
        erase_crate(id);
        b.crates.push_back({id, x, y});
    };
    RobotView* r = b.robot(gi(p, "player"));
    if (t == "ROBOT_MOVED" || t == "ROBOT_PUSHED") {
        if (r) { r->x = gi(p, "x"); r->y = gi(p, "y"); }
    } else if (t == "ROBOT_ROTATED") {
        if (r) r->heading = parse_dir(p.get_string("to").value_or("N")).value_or(r->heading);
    } else if (t == "CRATE_PICKED_UP") {
        erase_crate(gi(p, "crate"));
        if (r) r->carry_id = gi(p, "crate");
    } else if (t == "CRATE_DELIVERED") {
        if (r) { r->carry_id = 0; r->score = gi(p, "score"); r->deliveries += 1; }
    } else if (t == "CRATE_DROPPED") {
        if (r) r->carry_id = 0;
        put_crate(gi(p, "crate"), gi(p, "x"), gi(p, "y"));
    } else if (t == "CRATE_MOVED") {
        put_crate(gi(p, "crate"), gi(p, "x"), gi(p, "y"));
    } else if (t == "CRATE_DESTROYED") {
        const int id = gi(p, "crate");
        erase_crate(id);
        for (auto& rb : b.robots) if (rb.carry_id == id) rb.carry_id = 0;
    } else if (t == "CRATE_SPAWNED") {
        put_crate(gi(p, "crate"), gi(p, "x"), gi(p, "y"));
    } else if (t == "LASER_HIT") {
        if (r) r->damage = gi(p, "damage");
    } else if (t == "TRAP_HIT") {
        if (r) r->damage = gi(p, "damage");
    } else if (t == "TRAP_ARMED") {
        for (auto& tv : b.traps) if (tv.id == gi(p, "trap")) { tv.armed = true; tv.owner = gi(p, "owner"); }
    } else if (t == "TRAP_TRIGGERED") {
        for (auto& tv : b.traps) if (tv.id == gi(p, "trap")) tv.armed = false;
    } else if (t == "TURRET_CAPTURED") {
        for (auto& tv : b.turrets) if (tv.id == gi(p, "turret")) tv.owner = gi(p, "owner");
    } else if (t == "TURRET_CHARGING") {
        for (auto& tv : b.turrets) if (tv.id == gi(p, "turret")) tv.charge = gi(p, "charge");
    } else if (t == "TURRET_FIRED") {
        for (auto& tv : b.turrets) if (tv.id == gi(p, "turret")) tv.charge = 0;
        if (r) r->damage = gi(p, "damage");
    } else if (t == "SCORE_AWARDED") {
        if (r) { r->score = gi(p, "score"); r->sabotage += gi(p, "points"); }
    } else if (t == "ROBOT_DISABLED") {
        if (r) { r->disabled = true; r->active = false; }
    } else if (t == "ROBOT_RESPAWNED") {
        if (r) {
            r->active = true;
            r->disabled = false;
            r->x = gi(p, "x");
            r->y = gi(p, "y");
            r->heading = parse_dir(p.get_string("heading").value_or("N")).value_or(Dir::N);
            r->damage = 0;
            r->carry_id = 0;
        }
    } else if (t == "GROUP_TOGGLED") {
        auto g = p.get_string("group");
        if (g && g->size() == 1 && (*g)[0] >= 'A' && (*g)[0] <= 'Z')
            b.reversed[static_cast<std::size_t>((*g)[0] - 'A')] = p.get_bool("reversed").value_or(false);
    }
}

// ------------------------------------------------------------------ execution queue

namespace {
bool is_order_event(const std::string& t) { return t == "ORDER_EXECUTED" || t == "ORDER_NO_OP" || t == "ORDER_SKIPPED_DISABLED"; }
}  // namespace

std::vector<QueueEntry> build_queue(const Json& events) {
    std::vector<QueueEntry> q;
    int round = -1, tick = -1;
    bool open = false;
    auto close = [&] {
        if (!open) return;
        QueueEntry m;
        m.machinery = true;
        m.round = round;
        m.tick = tick;
        q.push_back(m);
        open = false;
    };
    for (const auto& e : events.items()) {
        const std::string t = type_of(e);
        if (!is_order_event(t)) continue;
        const int r = gi(e, "round"), k = gi(e, "tick");
        if (open && (r != round || k != tick)) close();
        round = r;
        tick = k;
        open = true;
        QueueEntry en;
        en.round = r;
        en.tick = k;
        en.player = gi(params(e), "player");
        en.order = params(e).get_string("order").value_or("");
        en.no_op = t == "ORDER_NO_OP";
        en.skipped = t == "ORDER_SKIPPED_DISABLED";
        q.push_back(en);
    }
    close();
    return q;
}

std::vector<QueueEntry> build_preview_queue(int round, int player_count, int me, const std::vector<std::string>& my_program) {
    std::vector<QueueEntry> q;
    for (int tick = 1; tick <= 3; ++tick) {
        for (int pid : botched::priority_order(botched::absolute_tick(round, tick), player_count)) {
            QueueEntry e;
            e.preview = true;
            e.round = round;
            e.tick = tick;
            e.player = pid;
            if (pid == me) {
                if (static_cast<std::size_t>(tick - 1) < my_program.size()) e.order = my_program[static_cast<std::size_t>(tick - 1)];
            } else {
                e.hidden = true;
            }
            q.push_back(e);
        }
        QueueEntry m;
        m.preview = true;
        m.machinery = true;
        m.round = round;
        m.tick = tick;
        q.push_back(m);
    }
    return q;
}

std::string queue_text(const std::vector<QueueEntry>& q, int round) {
    std::string s;
    for (const auto& e : q) {
        if (e.round != round) continue;
        if (!s.empty()) s += ' ';
        s += e.machinery ? std::string("*") : "P" + std::to_string(e.player) + "(" + std::to_string(e.tick) + ")";
    }
    return s;
}

// ------------------------------------------------------------------ step building

namespace {

std::string code_of(const std::string& token) {
    static const struct { const char* t; const char* c; } k[] = {
        {"FORWARD_1", "F"}, {"FORWARD_2", "F2"}, {"BACK_1", "B"}, {"TURN_LEFT", "L"}, {"TURN_RIGHT", "R"},
        {"TURN_AROUND", "U"}, {"DROP", "D"}, {"TOGGLE", "T"}, {"WAIT", "W"}};
    for (const auto& e : k) if (token == e.t) return e.c;
    return token;
}
std::string xy(int x, int y) { return "(" + std::to_string(x) + "," + std::to_string(y) + ")"; }
std::string P(int id) { return "P" + std::to_string(id); }

}  // namespace

void Playback::build(const Json& batch, std::size_t first_event) {
    const std::size_t qbase = all_queue_.size();
    const std::vector<QueueEntry> q = build_queue(batch);
    for (const auto& e : q) all_queue_.push_back(e);
    std::size_t qcur = qbase;

    int round = -1, tick = -1;
    bool tick_has_orders = false, machinery_started = false;
    int order_q = -1, mach_q = -1;
    std::string cur_code;
    int cur_player = 0;

    std::pair<int, int> last_tick{-1, -1};
    if (!steps_.empty()) {
        for (std::size_t k = steps_.size(); k-- > 0;)
            if (steps_[k].phase != Step::RoundEdge && steps_[k].tick >= 1) { last_tick = {steps_[k].round, steps_[k].tick}; break; }
    }
    auto push = [&](Step s) {
        if (s.phase != Step::RoundEdge && s.tick >= 1 && std::make_pair(s.round, s.tick) != last_tick) {
            s.tick_start = true;
            last_tick = {s.round, s.tick};
            ++ticks_scheduled_;
        }
        steps_.push_back(std::move(s));
    };
    auto new_step = [&](Step::Kind k, int ms) {
        Step s;
        s.kind = k;
        s.ms = ms;
        s.round = round;
        s.tick = tick;
        s.phase = machinery_started ? Step::Machinery : Step::Orders;
        s.queue_index = machinery_started ? mach_q : order_q;
        return s;
    };
    auto start_machinery = [&](bool triggered) {
        if (machinery_started || !tick_has_orders) return;
        machinery_started = true;
        mach_q = static_cast<int>(qcur++);
        Step s = new_step(Step::Quiet, 450);
        s.phase = Step::Machinery;
        s.queue_index = mach_q;
        s.caption = triggered ? "MACHINERY: conveyors and hazards run" : "MACHINERY: nothing moved";
        push(std::move(s));
    };
    auto close_tick = [&] {
        if (tick_has_orders) start_machinery(false);
        tick_has_orders = false;
        machinery_started = false;
        order_q = mach_q = -1;
    };
    auto is_trigger = [&](const std::string& t, const Json& p, std::size_t i) {
        if (t == "CRATE_MOVED" || t == "LASER_HIT") return true;
        if (t == "ROBOT_MOVED" || t == "MOVE_BLOCKED") return p.get_string("cause").value_or("") == "CONVEYOR";
        if (t == "TRAP_TRIGGERED" || t == "TRAP_HIT" || t == "SCORE_AWARDED") return true;
        if (t == "TURRET_CAPTURED" || t == "TURRET_CHARGING" || t == "TURRET_FIRED") return true;
        if (t == "ROBOT_DISABLED") return p.get_string("cause").value_or("") == "LASER" || p.get_string("cause").value_or("") == "TRAP" ||
                                          p.get_string("cause").value_or("") == "TURRET";
        if (t == "ROBOT_PUSHED") {  // caused by the pusher's own move: look ahead for it
            for (std::size_t j = i + 1; j < events_.size(); ++j) {
                if (type_of(events_[j]) != "ROBOT_MOVED") continue;
                const Json& jp = params(events_[j]);
                if (gi(jp, "player") == gi(p, "by")) return jp.get_string("cause").value_or("") == "CONVEYOR";
            }
        }
        return false;
    };

    for (std::size_t i = first_event; i < events_.size(); ++i) {
        const Json& e = events_[i];
        const std::string t = type_of(e);
        const Json& p = params(e);
        const bool edge = t == "ROBOT_RESPAWNED" || t == "RESPAWN_REDIRECTED" || t == "CRATE_SPAWNED" || t == "ROUND_STARTED" ||
                          t == "ROUND_ENDED" || t == "MATCH_ENDED";
        if (edge) {
            close_tick();
            Step s;
            s.round = gi(e, "round");
            s.tick = gi(e, "tick");
            s.phase = Step::RoundEdge;
            s.events = {i};
            s.commit_first = true;
            s.player = gi(p, "player");
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            if (t == "ROBOT_RESPAWNED") {
                s.kind = Step::Respawn;
                s.ms = 800;
                s.caption = "NEW ROUND: " + P(s.player) + " respawns at " + xy(s.cx, s.cy);
            } else if (t == "CRATE_SPAWNED") {
                s.kind = Step::Spawn;
                s.ms = 350;
                s.caption = "NEW ROUND: crate " + std::to_string(gi(p, "crate")) + " appears at dispenser G" + std::to_string(gi(p, "dispenser"));
            } else {
                s.kind = Step::Instant;
                s.ms = 0;
            }
            push(std::move(s));
            continue;
        }
        const int er = gi(e, "round"), et = gi(e, "tick");
        if (is_order_event(t)) {
            if (er != round || et != tick) { close_tick(); round = er; tick = et; }
            tick_has_orders = true;
            order_q = static_cast<int>(qcur++);
            cur_player = gi(p, "player");
            const std::string tok = p.get_string("order").value_or("");
            cur_code = code_of(tok);
            Step s = new_step(Step::Mark, t == "ORDER_SKIPPED_DISABLED" ? 300 : t == "ORDER_NO_OP" ? 450 : tok == "WAIT" ? 380 : 330);
            s.phase = Step::Orders;
            s.queue_index = order_q;
            s.player = cur_player;
            s.order_code = cur_code;
            if (t == "ORDER_SKIPPED_DISABLED") s.caption = P(cur_player) + " is disabled: order skipped";
            else if (t == "ORDER_NO_OP") s.caption = P(cur_player) + " " + cur_code + ": nothing to do (" + p.get_string("reason").value_or("?") + ")";
            else if (tok == "WAIT") s.caption = P(cur_player) + " waits";
            else s.caption = P(cur_player) + " executes " + cur_code;
            const bool tar_stuck = t == "ORDER_NO_OP" && p.get_string("reason").value_or("") == "TAR";
            if (tar_stuck) s.caption = P(cur_player) + " " + cur_code + ": stuck in tar, cannot move";
            push(std::move(s));
            if (tar_stuck) {
                Step ts = new_step(Step::TarSlow, 600);
                ts.phase = Step::Orders;
                ts.queue_index = order_q;
                ts.player = cur_player;
                ts.caption = P(cur_player) + " is stuck in tar";
                push(std::move(ts));
            }
            continue;
        }
        if (is_trigger(t, p, i)) start_machinery(true);

        if (t == "ROBOT_ROTATED") {
            Step s = new_step(Step::Turn, 480);
            s.player = gi(p, "player");
            s.from = parse_dir(p.get_string("from").value_or("N")).value_or(Dir::N);
            s.to = parse_dir(p.get_string("to").value_or("N")).value_or(Dir::N);
            s.events = {i};
            s.caption = P(s.player) + " turns " + to_string(s.from) + " -> " + to_string(s.to);
            push(std::move(s));
        } else if (t == "ROBOT_PUSHED") {
            Step s = new_step(Step::Push, 700);
            const int by = gi(p, "by");
            std::size_t j = i;
            while (j < events_.size() && type_of(events_[j]) == "ROBOT_PUSHED" && gi(params(events_[j]), "by") == by) {
                const Json& jp = params(events_[j]);
                s.movers.push_back({gi(jp, "player"), gi(jp, "from_x"), gi(jp, "from_y"), gi(jp, "x"), gi(jp, "y"), false});
                s.events.push_back(j);
                ++j;
            }
            std::string cause = "ORDER";
            if (j < events_.size() && type_of(events_[j]) == "ROBOT_MOVED" && gi(params(events_[j]), "player") == by) {
                const Json& jp = params(events_[j]);
                cause = jp.get_string("cause").value_or("ORDER");
                s.movers.push_back({by, gi(jp, "from_x"), gi(jp, "from_y"), gi(jp, "x"), gi(jp, "y"), false});
                s.events.push_back(j);
                s.cx = gi(jp, "x");
                s.cy = gi(jp, "y");
                ++j;
            }
            s.belt = cause == "CONVEYOR";
            s.player = by;
            s.commit_first = false;
            std::string victims;
            for (const auto& m : s.movers) if (m.id != by) victims += (victims.empty() ? "" : ", ") + P(m.id) + " " + xy(m.fx, m.fy) + " -> " + xy(m.tx, m.ty);
            s.caption = std::string(s.belt ? "BELT: " : "") + P(by) + " pushes " + victims;
            push(std::move(s));
            i = j - 1;
        } else if (t == "ROBOT_MOVED") {
            const bool belt = p.get_string("cause").value_or("") == "CONVEYOR";
            Step s = new_step(Step::Move, belt ? 650 : 520);
            s.player = gi(p, "player");
            s.belt = belt;
            s.movers.push_back({s.player, gi(p, "from_x"), gi(p, "from_y"), gi(p, "x"), gi(p, "y"), false});
            s.events = {i};
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.caption = belt ? "BELT carries " + P(s.player) + (s.player == cur_player && cur_code == "W" ? " (ordered WAIT)" : "") + " " +
                                   xy(s.movers[0].fx, s.movers[0].fy) + " -> " + xy(s.cx, s.cy)
                             : P(s.player) + " drives " + xy(s.movers[0].fx, s.movers[0].fy) + " -> " + xy(s.cx, s.cy);
            push(std::move(s));
        } else if (t == "MOVE_BLOCKED") {
            Step s = new_step(Step::Blocked, 520);
            s.player = gi(p, "player");
            s.belt = p.get_string("cause").value_or("") == "CONVEYOR";
            s.to = parse_dir(p.get_string("dir").value_or("N")).value_or(Dir::N);
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.caption = std::string(s.belt ? "BELT cannot move " : "") + P(s.player) + (s.belt ? "" : " is blocked") + " going " + to_string(s.to) + " into " + xy(s.cx, s.cy);
            push(std::move(s));
        } else if (t == "CRATE_PICKED_UP") {
            Step s = new_step(Step::Pickup, 380);
            s.player = gi(p, "player");
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.events = {i};
            s.commit_first = true;
            s.caption = P(s.player) + " picks up crate " + std::to_string(gi(p, "crate"));
            push(std::move(s));
        } else if (t == "CRATE_DELIVERED") {
            Step s = new_step(Step::Deliver, 750);
            s.player = gi(p, "player");
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.events = {i};
            s.commit_first = true;
            s.caption = P(s.player) + " DELIVERS crate " + std::to_string(gi(p, "crate")) + " - score " + std::to_string(gi(p, "score"));
            push(std::move(s));
        } else if (t == "CRATE_DROPPED") {
            Step s = new_step(Step::Drop, 320);
            s.player = gi(p, "player");
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.events = {i};
            s.commit_first = true;
            s.caption = P(s.player) + " drops crate " + std::to_string(gi(p, "crate"));
            push(std::move(s));
        } else if (t == "CRATE_MOVED") {
            Step s = new_step(Step::CrateMove, 600);
            s.movers.push_back({gi(p, "crate"), gi(p, "from_x"), gi(p, "from_y"), gi(p, "x"), gi(p, "y"), true});
            s.events = {i};
            s.belt = true;
            s.caption = "BELT carries crate " + std::to_string(gi(p, "crate")) + " " + xy(gi(p, "from_x"), gi(p, "from_y")) + " -> " + xy(gi(p, "x"), gi(p, "y"));
            push(std::move(s));
        } else if (t == "CRATE_DESTROYED") {
            Step s = new_step(Step::CrateGone, 480);
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.events = {i};
            s.caption = "crate " + std::to_string(gi(p, "crate")) + " destroyed (" + p.get_string("reason").value_or("?") + ")";
            push(std::move(s));
        } else if (t == "LASER_HIT") {
            Step s = new_step(Step::Laser, 480);
            s.player = gi(p, "player");
            s.events = {i};
            s.commit_first = true;
            s.caption = P(s.player) + " hit by a laser (damage " + std::to_string(gi(p, "damage")) + ")";
            push(std::move(s));
        } else if (t == "ROBOT_DISABLED") {
            Step s = new_step(Step::Disable, 1000);
            s.player = gi(p, "player");
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.events = {i};
            s.caption = P(s.player) + " DISABLED (" + p.get_string("cause").value_or("?") + ")";
            push(std::move(s));
        } else if (t == "TRAP_ARMED") {
            Step s = new_step(Step::TrapArm, 700);
            s.player = gi(p, "player");
            s.trap = gi(p, "trap");
            s.owner = gi(p, "owner");
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.events = {i};
            s.commit_first = true;
            s.caption = P(s.player) + (p.get_bool("was_armed").value_or(false) ? " takes over trap " : " arms trap ") + std::to_string(s.trap) +
                        (gi(p, "previous_owner") != 0 && gi(p, "previous_owner") != s.player ? " (was P" + std::to_string(gi(p, "previous_owner")) + "'s)" : "");
            push(std::move(s));
        } else if (t == "TRAP_TRIGGERED") {
            Step s = new_step(Step::TrapBurst, 850);
            s.trap = gi(p, "trap");
            s.owner = gi(p, "owner");
            s.player = s.owner;
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            std::string names;
            if (const Json* v = p.find("victims"))
                for (const auto& x : v->items()) {
                    s.victims.push_back(static_cast<int>(x.as_int()));
                    names += (names.empty() ? "" : ", ") + P(static_cast<int>(x.as_int()));
                }
            s.events = {i};
            s.commit_first = true;
            s.caption = "TRAP " + std::to_string(s.trap) + " (" + (s.owner ? P(s.owner) : std::string("neutral")) + ") fires: catches " + names;
            push(std::move(s));
        } else if (t == "TRAP_HIT") {
            Step s = new_step(Step::TrapHit, 480);
            s.player = gi(p, "player");
            s.trap = gi(p, "trap");
            s.owner = gi(p, "owner");
            s.events = {i};
            s.commit_first = true;
            s.caption = P(s.player) + " damaged by trap " + std::to_string(s.trap) + " (damage " + std::to_string(gi(p, "damage")) + ")" +
                        (s.owner == 0 ? " - neutral, no points" : s.owner == s.player ? " - own trap, no points" : "");
            push(std::move(s));
        } else if (t == "SCORE_AWARDED") {
            Step s = new_step(Step::Score, 800);
            s.player = gi(p, "player");   // who earned it
            s.points = gi(p, "points");
            const std::string reason = p.get_string("reason").value_or("");
            s.turret = reason.rfind("TURRET", 0) == 0;
            s.trap = gi(p, s.turret ? "turret" : "trap");
            s.events = {i};
            s.commit_first = true;
            const std::string why = (reason == "TRAP_DISABLE" || reason == "TURRET_DISABLE") ? "disabling " : "damaging ";
            s.caption = "+" + std::to_string(s.points) + " for " + P(s.player) + ": " + why + P(gi(p, "victim")) +
                        (s.turret ? " with turret " : " with trap ") + std::to_string(s.trap) + " (score " + std::to_string(gi(p, "score")) + ")";
            push(std::move(s));
        } else if (t == "TAR_SLOWED") {
            Step s = new_step(Step::TarSlow, 600);
            s.phase = Step::Orders;
            s.queue_index = order_q;
            s.player = gi(p, "player");
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.events = {i};
            s.caption = P(s.player) + " is slowed by tar: FORWARD_2 stops after one tile";
            push(std::move(s));
        } else if (t == "TURRET_CAPTURED") {
            Step s = new_step(Step::TurretCapture, 700);
            s.player = gi(p, "player");
            s.trap = gi(p, "turret");
            s.owner = gi(p, "owner");
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.events = {i};
            s.commit_first = true;
            s.caption = P(s.player) + " captures turret " + std::to_string(s.trap) +
                        (gi(p, "previous_owner") != 0 ? " (was P" + std::to_string(gi(p, "previous_owner")) + "'s)" : "");
            push(std::move(s));
        } else if (t == "TURRET_CHARGING") {
            Step s = new_step(Step::TurretCharge, 380);
            s.trap = gi(p, "turret");
            s.owner = gi(p, "owner");
            s.charge = gi(p, "charge");
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.events = {i};
            s.commit_first = true;
            s.caption = "turret " + std::to_string(s.trap) + " charging " + std::to_string(s.charge) + "/2";
            push(std::move(s));
        } else if (t == "TURRET_FIRED") {
            Step s = new_step(Step::TurretFire, 900);
            s.player = gi(p, "player");  // the victim
            s.trap = gi(p, "turret");
            s.owner = gi(p, "owner");
            s.cx = gi(p, "x");
            s.cy = gi(p, "y");
            s.to = parse_dir(p.get_string("dir").value_or("N")).value_or(Dir::N);
            s.distance = gi(p, "distance");
            s.events = {i};
            s.commit_first = true;
            s.caption = "TURRET " + std::to_string(s.trap) + " (" + (s.owner ? P(s.owner) : std::string("neutral")) + ") fires " +
                        to_string(s.to) + " and hits " + P(s.player) + " (damage " + std::to_string(gi(p, "damage")) + ")" +
                        (s.owner == 0 ? " - neutral, no points" : s.owner == s.player ? " - own turret, no points" : "");
            push(std::move(s));
        } else if (t == "GROUP_TOGGLED") {
            Step s = new_step(Step::Toggle, 600);
            s.player = gi(p, "player");
            s.group = p.get_string("group").value_or("A").empty() ? 'A' : p.get_string("group").value_or("A")[0];
            s.events = {i};
            s.commit_first = true;
            s.caption = P(s.player) + " toggles belt group " + std::string(1, s.group) + (p.get_bool("reversed").value_or(false) ? ": REVERSED" : ": normal");
            push(std::move(s));
        } else {
            // Unknown or bookkeeping event: commit without delay.
            Step s = new_step(Step::Instant, 0);
            s.round = er;
            s.tick = et;
            s.events = {i};
            s.commit_first = true;
            push(std::move(s));
        }
    }
    close_tick();
}

// ------------------------------------------------------------------ playback machine

void Playback::start(const BoardView& base, const Json& events) {
    if (!active()) {
        board_ = base;
        steps_.clear();
        events_.clear();
        committed_.clear();
        all_queue_.clear();
        pos_ = 0;
        elapsed_ = 0;
        began_ = false;
        ticks_started_ = 0;
        ticks_scheduled_ = 0;
    }
    const std::size_t first = events_.size();
    for (const auto& e : events.items()) events_.push_back(e);
    build(events, first);
    committed_.resize(steps_.size(), 0);
}

void Playback::commit(std::size_t step) {
    if (committed_[step]) return;
    for (std::size_t i : steps_[step].events) apply_event(board_, events_[i]);
    committed_[step] = 1;
}

void Playback::begin_step() {
    began_ = true;
    Step& s = steps_[pos_];
    if (s.tick_start) ++ticks_started_;
    // queue display follows the round of the step
    if (s.queue_index >= 0 && static_cast<std::size_t>(s.queue_index) < all_queue_.size()) {
        const int r = all_queue_[static_cast<std::size_t>(s.queue_index)].round;
        if (r != queue_round_ || queue_.empty()) {
            queue_round_ = r;
            queue_.clear();
            queue_base_ = s.queue_index;
            for (std::size_t i = 0; i < all_queue_.size(); ++i)
                if (all_queue_[i].round == r) {
                    if (queue_.empty()) queue_base_ = static_cast<int>(i);
                    queue_.push_back(all_queue_[i]);
                }
        }
    }
    if (s.commit_first) commit(pos_);
}

void Playback::finish_step() { commit(pos_); }

void Playback::advance(int dt_ms) {
    if (!active()) return;
    if (!began_) begin_step();
    elapsed_ += static_cast<int>(dt_ms * speed_);
    while (active() && elapsed_ >= steps_[pos_].ms) {
        elapsed_ -= steps_[pos_].ms;
        finish_step();
        ++pos_;
        if (!active()) break;
        begin_step();
    }
    if (!active()) { elapsed_ = 0; finished_queue(); }
}

void Playback::finished_queue() { done_all_ = true; }

void Playback::skip() {
    if (!active()) return;
    if (!began_) begin_step();
    for (; pos_ < steps_.size(); ++pos_) {
        commit(pos_);
    }
    ticks_started_ = ticks_scheduled_;
    elapsed_ = 0;
    done_all_ = true;
}

double Playback::progress() const {
    if (!active() || steps_[pos_].ms <= 0) return 1.0;
    return std::min(1.0, static_cast<double>(elapsed_) / steps_[pos_].ms);
}

int Playback::queue_live() const {
    if (!active()) return -1;
    const int qi = steps_[pos_].queue_index;
    if (qi < 0) return -1;
    const int local = qi - queue_base_;
    return (local >= 0 && local < static_cast<int>(queue_.size())) ? local : -1;
}

int Playback::queue_done() const {
    if (!active()) return static_cast<int>(queue_.size());
    const int live = queue_live();
    if (live >= 0) return live;
    return steps_[pos_].phase == Step::RoundEdge ? static_cast<int>(queue_.size()) : 0;
}

}  // namespace botched::ui

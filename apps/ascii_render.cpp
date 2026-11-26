#include "ascii_render.hpp"

#include <vector>

namespace botched::sim {

namespace {

char terrain_char(const Tile& t) {
    switch (t.terrain) {
        case Terrain::Wall: return '#';
        case Terrain::Floor: return '.';
        case Terrain::Pit: return 'O';
        case Terrain::Spawn: return 'S';
        case Terrain::Dock: return 'D';
        case Terrain::Dispenser: return 'G';
        case Terrain::Console: return 'T';
        case Terrain::LaserEmitter: return 'L';
        case Terrain::Trap: return 'X';
        case Terrain::TrapConsole: return 'K';
        case Terrain::TarGenerator: return 'A';
        case Terrain::Turret: return 'Y';
        case Terrain::TurretPad: return 'P';
        case Terrain::Conveyor:
            switch (t.dir) {
                case Dir::E: return '>';
                case Dir::W: return '<';
                case Dir::N: return '^';
                case Dir::S: return 'v';
            }
    }
    return '?';
}

char arrow(Dir d) {
    switch (d) {
        case Dir::E: return '>';
        case Dir::W: return '<';
        case Dir::N: return '^';
        case Dir::S: return 'v';
    }
    return '?';
}

}  // namespace

std::string render_board(const Map& map, const Json& snap) {
    struct Cell { bool crate = false; char robot = ' '; char head = ' '; };
    std::vector<Cell> cells(map.tiles.size());
    auto at = [&](int x, int y) -> Cell& { return cells[static_cast<std::size_t>(y * map.width + x)]; };
    if (const Json* cs = snap.find("crates")) {
        for (const auto& c : cs->items()) {
            auto x = c.get_int("x");
            auto y = c.get_int("y");
            if (x && y) at(static_cast<int>(*x), static_cast<int>(*y)).crate = true;
        }
    }
    if (const Json* ps = snap.find("players")) {
        for (const auto& p : ps->items()) {
            auto x = p.get_int("x");
            auto y = p.get_int("y");
            if (!x || !y) continue;
            Cell& c = at(static_cast<int>(*x), static_cast<int>(*y));
            c.robot = static_cast<char>('0' + p.get_int("id").value_or(0));
            c.head = arrow(parse_dir(p.get_string("heading").value_or("N")).value_or(Dir::N));
        }
    }
    std::string out = "    ";
    for (int x = 0; x < map.width; ++x) {
        std::string n = std::to_string(x);
        out += n + std::string(5 - n.size(), ' ');
    }
    out += '\n';
    for (int y = 0; y < map.height; ++y) {
        std::string n = std::to_string(y);
        out += n + std::string(4 - n.size(), ' ');
        for (int x = 0; x < map.width; ++x) {
            const Tile& t = map.tile({x, y});
            const Cell& c = cells[static_cast<std::size_t>(y * map.width + x)];
            out += terrain_char(t);
            bool owned = t.terrain == Terrain::Spawn || t.terrain == Terrain::Dock || t.terrain == Terrain::Dispenser ||
                         t.terrain == Terrain::Trap || t.terrain == Terrain::TrapConsole ||
                         t.terrain == Terrain::Turret || t.terrain == Terrain::TurretPad;
            out += owned ? static_cast<char>('0' + t.owner % 10) : ' ';
            out += c.crate ? '*' : ' ';
            out += c.robot;
            out += c.head;
        }
        out += '\n';
    }
    return out;
}

std::string render_scores(const Json& snap) {
    std::string out;
    const Json* ps = snap.find("players");
    if (!ps) return out;
    for (const auto& p : ps->items()) {
        out += "  P" + std::to_string(p.get_int("id").value_or(0)) + "  score " +
               std::to_string(p.get_int("score").value_or(0)) + "  deliveries " +
               std::to_string(p.get_int("deliveries").value_or(0)) + "  damage " +
               std::to_string(p.get_int("damage").value_or(0));
        const Json* c = p.find("carrying");
        if (c && !c->is_null()) out += "  carrying crate " + std::to_string(c->as_int());
        if (p.get_bool("respawn_pending").value_or(false)) out += "  [DISABLED]";
        out += '\n';
    }
    return out;
}

}  // namespace botched::sim

// ====================================================================================== playtest

namespace botched::sim {

namespace {

const char* kReset = "\x1b[0m";

// Player colours (ANSI SGR background for robots, foreground for owned tiles).
int player_bg(int id) {
    static const int bg[] = {47, 41, 44, 42, 45};  // [0] unused, P1 red, P2 blue, P3 green, P4 magenta
    return bg[id >= 1 && id <= 4 ? id : 0];
}
int player_fg(int id) {
    static const int fg[] = {37, 91, 94, 92, 95};
    return fg[id >= 1 && id <= 4 ? id : 0];
}
int group_fg(char g) {
    static const int fg[] = {96, 95, 93, 92, 94};  // A cyan, B magenta, C yellow, D green, E blue, then cycle
    return fg[(g - 'A') % 5 < 0 ? 0 : (g - 'A') % 5];
}

std::string paint(const Style& st, const std::string& text, const std::string& sgr) {
    if (!st.color || sgr.empty()) return text;
    return "\x1b[" + sgr + "m" + text + kReset;
}

const char* arrow_glyph(Dir d, bool uni) {
    if (uni) {
        switch (d) { case Dir::N: return "▲"; case Dir::E: return "▶"; case Dir::S: return "▼"; case Dir::W: return "◀"; }
    } else {
        switch (d) { case Dir::N: return "^"; case Dir::E: return ">"; case Dir::S: return "v"; case Dir::W: return "<"; }
    }
    return "?";
}

const char* belt_glyph(Dir d, bool uni) {
    if (uni) {
        switch (d) { case Dir::N: return "↑"; case Dir::E: return "→"; case Dir::S: return "↓"; case Dir::W: return "←"; }
    }
    return arrow_glyph(d, false);
}

struct Overlay {
    int robot = 0;   // player id, 0 none
    Dir heading = Dir::N;
    bool carrying = false;
    bool crate = false;
};

struct View {
    std::vector<Overlay> cells;
    std::vector<bool> reversed;  // by group letter A..Z
    struct TrapInfo { bool armed = true; int owner = 0; };
    std::vector<TrapInfo> traps = std::vector<TrapInfo>(10);  // by trap id 1..9
    struct TurretInfo { int charge = 0; int owner = 0; };
    std::vector<TurretInfo> turrets = std::vector<TurretInfo>(10);  // by turret id 1..9
    View(const Map& map, const Json& snap) : cells(map.tiles.size()), reversed(26, false) {
        auto at = [&](int x, int y) -> Overlay* {
            if (x < 0 || y < 0 || x >= map.width || y >= map.height) return nullptr;
            return &cells[static_cast<std::size_t>(y * map.width + x)];
        };
        if (const Json* cs = snap.find("crates"))
            for (const auto& c : cs->items()) {
                auto x = c.get_int("x"), y = c.get_int("y");
                if (x && y) if (Overlay* o = at(static_cast<int>(*x), static_cast<int>(*y))) o->crate = true;
            }
        if (const Json* ps = snap.find("players"))
            for (const auto& p : ps->items()) {
                auto x = p.get_int("x"), y = p.get_int("y");
                if (!x || !y) continue;
                Overlay* o = at(static_cast<int>(*x), static_cast<int>(*y));
                if (!o) continue;
                o->robot = static_cast<int>(p.get_int("id").value_or(0));
                o->heading = parse_dir(p.get_string("heading").value_or("N")).value_or(Dir::N);
                const Json* c = p.find("carrying");
                o->carrying = c && !c->is_null();
            }
        if (const Json* ts = snap.find("traps"))
            for (const auto& t : ts->items()) {
                const auto id = t.get_int("id").value_or(0);
                if (id >= 1 && id <= 9) traps[static_cast<std::size_t>(id)] = {t.get_bool("armed").value_or(true), static_cast<int>(t.get_int("owner").value_or(0))};
            }
        if (const Json* ts = snap.find("turrets"))
            for (const auto& t : ts->items()) {
                const auto id = t.get_int("id").value_or(0);
                if (id >= 1 && id <= 9)
                    turrets[static_cast<std::size_t>(id)] = {static_cast<int>(t.get_int("charge").value_or(0)),
                                                             static_cast<int>(t.get_int("owner").value_or(0))};
            }
        if (const Json* gs = snap.find("groups"))
            for (const auto& g : gs->items()) {
                auto id = g.get_string("id");
                if (id && id->size() == 1 && (*id)[0] >= 'A' && (*id)[0] <= 'Z')
                    reversed[static_cast<std::size_t>((*id)[0] - 'A')] = g.get_bool("reversed").value_or(false);
            }
    }
    const Overlay& at(const Map& map, int x, int y) const { return cells[static_cast<std::size_t>(y * map.width + x)]; }
    bool group_reversed(char g) const { return g >= 'A' && g <= 'Z' && reversed[static_cast<std::size_t>(g - 'A')]; }
};

Dir effective_dir(const Tile& t, const View& v) { return v.group_reversed(t.group) ? opposite(t.dir) : t.dir; }

std::string pad3(const std::string& s) { return s.size() >= 3 ? s : s + std::string(3 - s.size(), ' '); }

Pos t_pos(int x, int y) { return Pos{x, y}; }

std::string cell_text(const Map& map, const View& v, const Style& st, int x, int y) {
    const Tile& t = map.tile({x, y});
    const Overlay& o = v.at(map, x, y);
    const bool u = st.unicode;
    if (o.robot) {
        std::string g = std::to_string(o.robot % 10) + arrow_glyph(o.heading, u) + (o.carrying ? (u ? "■" : "*") : " ");
        return paint(st, g, "1;97;" + std::to_string(player_bg(o.robot)));
    }
    if (o.crate && t.terrain != Terrain::Dispenser)
        return paint(st, u ? " ■ " : " * ", "1;93");  // a loose crate hides the terrain beneath it
    switch (t.terrain) {
        case Terrain::Wall: return paint(st, u ? "███" : "###", "90");
        case Terrain::Floor:
            if (map.in_tar(t_pos(x, y))) return paint(st, " ~ ", "1;35");  // tar: slows voluntary movement
            return paint(st, u ? " · " : " . ", "90");
        case Terrain::Pit: return paint(st, u ? "░░░" : " O ", "31");
        case Terrain::Spawn: return paint(st, "S" + std::to_string(t.owner % 10) + " ", "1;" + std::to_string(player_fg(t.owner)));
        case Terrain::Dock: return paint(st, u ? "▣" + std::to_string(t.owner % 10) + " " : "D" + std::to_string(t.owner % 10) + " ", "1;" + std::to_string(player_fg(t.owner)));
        case Terrain::Dispenser:  // loaded: bright block "G1"; empty: dim lower-case "g1" (details in the legend)
            return o.crate ? paint(st, "G" + std::to_string(t.owner % 10) + " ", "1;30;103")
                           : paint(st, "g" + std::to_string(t.owner % 10) + " ", "33");
        case Terrain::Console: return paint(st, u ? " ⌨ " : " T ", "1;" + std::to_string(group_fg(t.group)));
        case Terrain::LaserEmitter: return paint(st, std::string(" L") + arrow_glyph(t.dir, false), "1;91");
        case Terrain::Trap: {  // armed: "X1!" in the owner's colour (white = neutral); disarmed: dim "x1 "
            const auto& ti = v.traps[static_cast<std::size_t>(t.owner % 10)];
            const std::string id = std::to_string(t.owner % 10);
            return ti.armed ? paint(st, "X" + id + "!", "1;" + std::to_string(player_fg(ti.owner))) : paint(st, "x" + id + " ", "90");
        }
        case Terrain::TrapConsole: return paint(st, "K" + std::to_string(t.owner % 10) + " ", "1;96");
        case Terrain::TarGenerator: return paint(st, " A ", "1;35");
        case Terrain::Turret: {  // "Y1" plus charge dots, in the owner's colour (white = neutral)
            const auto& ti = v.turrets[static_cast<std::size_t>(t.owner % 10)];
            return paint(st, "Y" + std::to_string(t.owner % 10) + std::string(ti.charge >= 2 ? "!" : ti.charge == 1 ? "'" : " "),
                         "1;" + std::to_string(player_fg(ti.owner)));
        }
        case Terrain::TurretPad: return paint(st, "P" + std::to_string(t.owner % 10) + " ", "1;95");
        case Terrain::Conveyor:
            return paint(st, std::string(" ") + belt_glyph(effective_dir(t, v), u) + " ", std::to_string(group_fg(t.group)));
    }
    return " ? ";
}

}  // namespace

std::string render_board_styled(const Map& map, const Json& snap, const Style& st) {
    View v(map, snap);
    std::string out = "    ";
    for (int x = 0; x < map.width; ++x) out += pad3(std::to_string(x));
    out += '\n';
    for (int y = 0; y < map.height; ++y) {
        out += pad3(std::to_string(y)) + " ";
        for (int x = 0; x < map.width; ++x) out += cell_text(map, v, st, x, y);
        out += '\n';
    }
    return out;
}

std::string render_scores_styled(const Json& snap, const Style& st) {
    std::string out;
    const Json* ps = snap.find("players");
    if (!ps) return out;
    for (const auto& p : ps->items()) {
        const int id = static_cast<int>(p.get_int("id").value_or(0));
        std::string line = paint(st, " P" + std::to_string(id) + " ", "1;97;" + std::to_string(player_bg(id)));
        line += "  score " + std::to_string(p.get_int("score").value_or(0)) +
                "  deliveries " + std::to_string(p.get_int("deliveries").value_or(0)) +
                "  damage " + std::to_string(p.get_int("damage").value_or(0));
        if (auto sab = p.get_int("sabotage"); sab && *sab > 0) line += "  sabotage +" + std::to_string(*sab);
        const Json* c = p.find("carrying");
        if (c && !c->is_null()) line += "  carrying crate " + std::to_string(c->as_int());
        if (p.get_bool("respawn_pending").value_or(false)) line += "  " + paint(st, "[DISABLED]", "1;91");
        out += line + '\n';
    }
    return out;
}

std::string render_legend(const Map& map, const Json& snap, const Style& st) {
    View v(map, snap);
    const bool u = st.unicode;
    std::string out;
    out += std::string("Key: ") + paint(st, u ? "██" : "##", "90") + " wall  " + paint(st, u ? "░░" : "O", "31") +
           " pit(destroys)  " + paint(st, u ? "→" : ">", "96") + " belt  " + paint(st, u ? "⌨" : "T", "1;96") + " console  " +
           paint(st, "S", "1;97") + " spawn  " + paint(st, u ? "▣" : "D", "1;97") + " dock  " + paint(st, "G", "1;30;103") + "/" + paint(st, "g", "33") + " dispenser (loaded/empty)  " +
           paint(st, u ? "■" : "*", "1;93") + " crate\n";
    out += std::string("      robot = ") + paint(st, std::string("1") + arrow_glyph(Dir::E, u) + (u ? "■" : "*"), "1;97;41") +
           "  (player, facing, carrying a crate); a robot hides the tile under it\n";
    // belts
    std::string belts;
    for (char g : map.groups) {
        bool has_belt = false, has_console = false;
        for (const auto& t : map.tiles) {
            if (t.group != g) continue;
            has_belt |= t.terrain == Terrain::Conveyor;
            has_console |= t.terrain == Terrain::Console;
        }
        if (!has_belt && !has_console) continue;
        belts += "  " + paint(st, std::string("group ") + g, "1;" + std::to_string(group_fg(g))) + ": " +
                 (v.group_reversed(g) ? paint(st, "REVERSED", "1;91") : std::string("normal"));
    }
    if (!belts.empty()) out += "Belts:" + belts + "\n";
    // proximity traps
    std::string traps;
    for (const auto& tp : map.traps) {
        const auto& ti = v.traps[static_cast<std::size_t>(tp.id % 10)];
        traps += "  X" + std::to_string(tp.id) + "(" + std::to_string(tp.pos.x) + "," + std::to_string(tp.pos.y) + ") " +
                 (ti.armed ? paint(st, "ARMED", "1;91") : std::string("disarmed")) + ", " +
                 (ti.owner == 0 ? std::string("neutral") : "P" + std::to_string(ti.owner));
    }
    if (!traps.empty()) out += "Traps:" + traps + "   (hit the 8 tiles around; TOGGLE next to K<n> arms and claims)\n";
    // charging turrets
    std::string turrets;
    for (const auto& tp : map.turrets) {
        const auto& ti = v.turrets[static_cast<std::size_t>(tp.id % 10)];
        turrets += "  Y" + std::to_string(tp.id) + "(" + std::to_string(tp.pos.x) + "," + std::to_string(tp.pos.y) + ") " +
                   (ti.charge >= 2 ? paint(st, "READY", "1;91") : "charge " + std::to_string(ti.charge) + "/2") + ", " + (ti.owner == 0 ? std::string("neutral") : "P" + std::to_string(ti.owner));
    }
    if (!turrets.empty())
        out += "Turrets:" + turrets + "   (2 ticks to charge, then READY: fires at the nearest robot within 2 tiles N/E/S/W; step on P<n> to claim)\n";
    // dispensers
    std::string disp;
    for (const auto& d : map.dispensers) {
        bool has = v.at(map, d.pos.x, d.pos.y).crate;
        disp += "  G" + std::to_string(d.id) + "(" + std::to_string(d.pos.x) + "," + std::to_string(d.pos.y) + ") " +
                (has ? paint(st, "crate ready", "1;93") : std::string("empty"));
    }
    if (!disp.empty()) out += "Dispensers:" + disp + "\n";
    return out;
}

std::string describe_tile(const Map& map, const Json& snap, Pos pos, int viewer) {
    View v(map, snap);
    if (!map.in_bounds(pos)) return "off the map";
    const Tile& t = map.tile(pos);
    auto owner = [&](const char* what) {
        return t.owner == viewer ? std::string("your ") + what : std::string(what) + " of P" + std::to_string(t.owner);
    };
    auto dname = [](Dir d) { return std::string(to_string(d)); };
    switch (t.terrain) {
        case Terrain::Wall: return "wall";
        case Terrain::Floor: return map.in_tar(pos) ? "tar: F2 moves 1 tile, F and B cannot move" : "floor";
        case Terrain::Pit: return "pit";
        case Terrain::Spawn: return owner("spawn");
        case Terrain::Dock: return owner("dock") + " (carrying a crate here scores)";
        case Terrain::Dispenser: {
            bool has = v.at(map, pos.x, pos.y).crate;
            return "dispenser G" + std::to_string(t.owner) + (has ? " (crate here)" : " (empty)");
        }
        case Terrain::Console: return std::string("console, belt group ") + t.group;
        case Terrain::LaserEmitter: return "laser emitter";
        case Terrain::Trap: {
            const auto& ti = v.traps[static_cast<std::size_t>(t.owner % 10)];
            std::string who = ti.owner == 0 ? "neutral" : ti.owner == viewer ? "yours" : "owned by P" + std::to_string(ti.owner);
            return "proximity trap " + std::to_string(t.owner) + " (" + (ti.armed ? "ARMED, " : "disarmed, ") + who +
                   ") - hits every robot on the 8 tiles around it, once, then disarms";
        }
        case Terrain::TarGenerator:
            return "tar generator: the 8 tiles around it slow robots (no damage)";
        case Terrain::Turret: {
            const auto& ti = v.turrets[static_cast<std::size_t>(t.owner % 10)];
            std::string who = ti.owner == 0 ? "neutral" : ti.owner == viewer ? "yours" : "owned by P" + std::to_string(ti.owner);
            return "charging laser turret " + std::to_string(t.owner) + " (" + (ti.charge >= 2 ? std::string("READY") : "charge " + std::to_string(ti.charge) + "/2") + ", " + who +
                   ") - charges for 2 ticks, then fires at the nearest robot within 2 tiles N/E/S/W as soon as one is there";
        }
        case Terrain::TurretPad:
            return "control pad of turret " + std::to_string(t.owner) + " - step onto it to claim the turret";
        case Terrain::TrapConsole:
            return "control console of trap " + std::to_string(t.owner) + " - stand on or next to it and TOGGLE (T) to arm the trap and claim it";
        case Terrain::Conveyor:
            return "conveyor heading " + dname(effective_dir(t, v)) + " (group " + t.group + ", " +
                   (v.group_reversed(t.group) ? "reversed" : "normal") + ") - it will move you at the end of every tick";
    }
    return "?";
}

}  // namespace botched::sim

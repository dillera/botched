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
            bool owned = t.terrain == Terrain::Spawn || t.terrain == Terrain::Dock || t.terrain == Terrain::Dispenser;
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

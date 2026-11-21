#include "botched/map.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <queue>
#include <set>
#include <sstream>

namespace botched {

namespace {

Dir default_heading(int owner) { return (owner % 2 == 1) ? Dir::E : Dir::W; }

std::optional<Dir> arrow_dir(char c) {
    switch (c) {
        case '>': return Dir::E;
        case '<': return Dir::W;
        case '^': return Dir::N;
        case 'v': return Dir::S;
        default: return std::nullopt;
    }
}

char dir_arrow(Dir d) {
    switch (d) {
        case Dir::E: return '>';
        case Dir::W: return '<';
        case Dir::N: return '^';
        case Dir::S: return 'v';
    }
    return '?';
}

bool is_group(char c) { return c >= 'A' && c <= 'Z'; }

// Parses digits-only strings of 1..4 characters; returns -1 on failure.
int parse_small_int(std::string_view s) {
    if (s.empty() || s.size() > 3) return -1;
    int v = 0;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
        v = v * 10 + (c - '0');
    }
    return v;
}

std::optional<Tile> parse_token(const std::string& t, std::string& err) {
    Tile tile;
    auto bad = [&](const char* why) -> std::optional<Tile> {
        err = "token '" + t + "': " + why;
        return std::nullopt;
    };
    if (t == "#") { tile.terrain = Terrain::Wall; return tile; }
    if (t == ".") { tile.terrain = Terrain::Floor; return tile; }
    if (t == "O") { tile.terrain = Terrain::Pit; return tile; }
    char k = t[0];
    if (k == 'S' || k == 'D' || k == 'G') {
        std::size_t n = 1;
        while (n < t.size() && std::isdigit(static_cast<unsigned char>(t[n]))) ++n;
        int v = parse_small_int(std::string_view(t).substr(1, n - 1));
        if (v < 1) return bad("expected owner/id number");
        tile.owner = v;
        if (k == 'S') {
            if (v > kMaxPlayers) return bad("owner must be 1-4");
            tile.terrain = Terrain::Spawn;
            tile.dir = default_heading(v);
            if (n < t.size()) {
                if (t.size() != n + 1) return bad("trailing characters");
                auto d = parse_dir(std::string_view(t).substr(n, 1));
                if (!d) return bad("bad spawn heading");
                tile.dir = *d;
            }
            return tile;
        }
        if (n != t.size()) return bad("trailing characters");
        if (k == 'D') {
            if (v > kMaxPlayers) return bad("owner must be 1-4");
            tile.terrain = Terrain::Dock;
        } else {
            tile.terrain = Terrain::Dispenser;
        }
        return tile;
    }
    if (k == 'C') {
        if (t.size() < 2 || t.size() > 3) return bad("expected C<dir>[group]");
        auto d = arrow_dir(t[1]);
        if (!d) return bad("bad conveyor direction");
        tile.terrain = Terrain::Conveyor;
        tile.dir = *d;
        if (t.size() == 3) {
            if (!is_group(t[2])) return bad("bad group letter");
            tile.group = t[2];
        }
        return tile;
    }
    if (k == 'T') {
        if (t.size() > 2) return bad("expected T[group]");
        tile.terrain = Terrain::Console;
        if (t.size() == 2) {
            if (!is_group(t[1])) return bad("bad group letter");
            tile.group = t[1];
        }
        return tile;
    }
    if (k == 'L') {
        if (t.size() != 2) return bad("expected L<dir>");
        auto d = arrow_dir(t[1]);
        if (!d) return bad("bad laser direction");
        tile.terrain = Terrain::LaserEmitter;
        tile.dir = *d;
        return tile;
    }
    return bad("unknown token");
}

std::string token_text(const Tile& t) {
    switch (t.terrain) {
        case Terrain::Wall: return "#";
        case Terrain::Floor: return ".";
        case Terrain::Pit: return "O";
        case Terrain::Spawn: {
            std::string s = "S" + std::to_string(t.owner);
            if (t.dir != default_heading(t.owner)) s += to_string(t.dir);
            return s;
        }
        case Terrain::Dock: return "D" + std::to_string(t.owner);
        case Terrain::Dispenser: return "G" + std::to_string(t.owner);
        case Terrain::Conveyor: {
            std::string s = "C";
            s += dir_arrow(t.dir);
            if (t.group != 'A') s += t.group;
            return s;
        }
        case Terrain::Console: {
            std::string s = "T";
            if (t.group != 'A') s += t.group;
            return s;
        }
        case Terrain::LaserEmitter: return std::string("L") + dir_arrow(t.dir);
    }
    return "?";
}

}  // namespace

bool Map::blocked(Pos p) const {
    if (!in_bounds(p)) return true;
    Terrain t = tile(p).terrain;
    return t == Terrain::Wall || t == Terrain::LaserEmitter;
}

bool Map::walkable(Pos p) const {
    return !blocked(p) && tile(p).terrain != Terrain::Pit;
}

void Map::finalize() {
    spawns = {};
    docks = {};
    dispensers.clear();
    consoles.clear();
    lasers.clear();
    groups.clear();
    std::set<char> g;
    int console_index = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Pos p{x, y};
            const Tile& t = tile(p);
            switch (t.terrain) {
                case Terrain::Spawn:
                    if (!spawns[static_cast<std::size_t>(t.owner)]) spawns[static_cast<std::size_t>(t.owner)] = p;
                    break;
                case Terrain::Dock:
                    if (!docks[static_cast<std::size_t>(t.owner)]) docks[static_cast<std::size_t>(t.owner)] = p;
                    break;
                case Terrain::Dispenser: dispensers.push_back({t.owner, p}); break;
                case Terrain::Console: {
                    char buf[24];
                    std::snprintf(buf, sizeof buf, "T%03d", ++console_index);
                    consoles.push_back({buf, p, t.group});
                    g.insert(t.group);
                    break;
                }
                case Terrain::Conveyor: g.insert(t.group); break;
                case Terrain::LaserEmitter: lasers.push_back(p); break;
                default: break;
            }
        }
    }
    std::stable_sort(dispensers.begin(), dispensers.end(),
                     [](const Dispenser& a, const Dispenser& b) { return a.id < b.id; });
    groups.assign(g.begin(), g.end());
}

MapLoad parse_map(std::string_view id, std::string_view text) {
    MapLoad r;
    Map m;
    m.id = std::string(id);
    std::vector<std::vector<Tile>> rows;
    std::istringstream in{std::string(text)};
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        std::istringstream ls(line);
        std::vector<std::string> toks;
        std::string t;
        while (ls >> t) toks.push_back(t);
        if (toks.empty() || toks[0][0] == ';') continue;
        std::vector<Tile> row;
        for (const auto& tok : toks) {
            std::string err;
            auto tile = parse_token(tok, err);
            if (!tile) {
                r.errors.push_back("line " + std::to_string(lineno) + ": " + err);
                continue;
            }
            row.push_back(*tile);
        }
        rows.push_back(std::move(row));
    }
    if (rows.empty()) r.errors.push_back("map has no rows");
    if (!r.errors.empty()) return r;
    m.height = static_cast<int>(rows.size());
    m.width = static_cast<int>(rows[0].size());
    for (std::size_t y = 0; y < rows.size(); ++y) {
        if (static_cast<int>(rows[y].size()) != m.width) {
            r.errors.push_back("row " + std::to_string(y) + " has " + std::to_string(rows[y].size()) +
                               " tokens, expected " + std::to_string(m.width));
        }
    }
    if (!r.errors.empty()) return r;
    for (auto& row : rows) for (auto& t : row) m.tiles.push_back(t);
    m.finalize();
    r.map = std::move(m);
    return r;
}

std::vector<std::string> validate_map(const Map& m, const ValidationOptions& opt) {
    std::vector<std::string> e;
    if (opt.player_count < 2 || opt.player_count > kMaxPlayers) e.push_back("player_count must be 2-4");
    if (m.width < 8 || m.width > 20 || m.height < 8 || m.height > 16)
        e.push_back("dimensions " + std::to_string(m.width) + "x" + std::to_string(m.height) +
                    " outside 8-20 columns x 8-16 rows");
    if (m.tiles.size() != static_cast<std::size_t>(m.width) * static_cast<std::size_t>(m.height)) {
        e.push_back("tile count does not match dimensions");
        return e;
    }

    // Per-owner uniqueness, and presence for each available slot.
    std::array<int, kMaxPlayers + 1> spawn_n{}, dock_n{};
    std::set<int> disp_ids;
    int disp_n = 0;
    std::set<char> conv_groups, cons_groups;
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            const Tile& t = m.tile({x, y});
            switch (t.terrain) {
                case Terrain::Spawn: ++spawn_n[static_cast<std::size_t>(t.owner)]; break;
                case Terrain::Dock: ++dock_n[static_cast<std::size_t>(t.owner)]; break;
                case Terrain::Dispenser:
                    ++disp_n;
                    if (!disp_ids.insert(t.owner).second)
                        e.push_back("duplicate dispenser id " + std::to_string(t.owner));
                    break;
                case Terrain::Conveyor: {
                    conv_groups.insert(t.group);
                    Pos n{x + dir_dx(t.dir), y + dir_dy(t.dir)};
                    if (opt.strict_first_party && !m.in_bounds(n))
                        e.push_back("conveyor at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") faces out of bounds");
                    break;
                }
                case Terrain::Console: cons_groups.insert(t.group); break;
                default: break;
            }
        }
    }
    for (int p = 1; p <= kMaxPlayers; ++p) {
        auto i = static_cast<std::size_t>(p);
        bool required = p <= opt.player_count;
        if (required && spawn_n[i] != 1) e.push_back("player " + std::to_string(p) + " needs exactly one spawn");
        if (required && dock_n[i] != 1) e.push_back("player " + std::to_string(p) + " needs exactly one dock");
        if (!required && (spawn_n[i] > 1 || dock_n[i] > 1))
            e.push_back("player " + std::to_string(p) + " has duplicate spawn/dock");
    }
    if (disp_n < 2) e.push_back("at least two dispensers required");
    for (char g : conv_groups)
        if (!cons_groups.count(g)) e.push_back(std::string("conveyor group ") + g + " has no console");
    for (char g : cons_groups)
        if (!conv_groups.count(g)) e.push_back(std::string("console group ") + g + " has no conveyor");

    // Connectivity over walkable cells, ignoring robots.
    auto flood = [&](Pos from) {
        std::vector<char> seen(m.tiles.size(), 0);
        std::queue<Pos> q;
        if (m.in_bounds(from)) { seen[static_cast<std::size_t>(from.y * m.width + from.x)] = 1; q.push(from); }
        while (!q.empty()) {
            Pos c = q.front(); q.pop();
            for (int d = 0; d < 4; ++d) {
                Dir dd = static_cast<Dir>(d);
                Pos n{c.x + dir_dx(dd), c.y + dir_dy(dd)};
                if (!m.walkable(n)) continue;
                auto idx = static_cast<std::size_t>(n.y * m.width + n.x);
                if (seen[idx]) continue;
                seen[idx] = 1;
                q.push(n);
            }
        }
        return seen;
    };
    for (int p = 1; p <= opt.player_count && p <= kMaxPlayers; ++p) {
        auto s = m.spawns[static_cast<std::size_t>(p)];
        auto d = m.docks[static_cast<std::size_t>(p)];
        if (!s || !d) continue;  // already reported
        auto reach = flood(*s);
        if (!reach[static_cast<std::size_t>(d->y * m.width + d->x)])
            e.push_back("player " + std::to_string(p) + " spawn cannot reach own dock");
        auto from_dock = flood(*d);
        bool any = false;
        for (const auto& disp : m.dispensers)
            if (from_dock[static_cast<std::size_t>(disp.pos.y * m.width + disp.pos.x)]) any = true;
        if (!any) e.push_back("player " + std::to_string(p) + " dock reachable from no dispenser");
    }
    return e;
}

MapLoad load_map(std::string_view id, std::string_view text, const ValidationOptions& opt) {
    MapLoad r = parse_map(id, text);
    if (!r.map) return r;
    r.errors = validate_map(*r.map, opt);
    return r;
}

std::string map_to_text(const Map& m) {
    std::string out;
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            if (x) out += ' ';
            out += token_text(m.tile({x, y}));
        }
        out += '\n';
    }
    return out;
}

Json map_to_json(const Map& m) {
    Json j = Json::object();
    j.set("protocol", kProtocol);
    j.set("map_id", m.id);
    j.set("width", m.width);
    j.set("height", m.height);
    Json rows = Json::array();
    for (int y = 0; y < m.height; ++y) {
        Json row = Json::array();
        for (int x = 0; x < m.width; ++x) row.push(token_text(m.tile({x, y})));
        rows.push(std::move(row));
    }
    j.set("rows", std::move(rows));
    return j;
}

MapLoad map_from_json(const Json& j) {
    MapLoad r;
    auto id = j.get_string("map_id");
    const Json* rows = j.find("rows");
    if (!id || !rows || !rows->is_array()) {
        r.errors.push_back("map definition needs map_id and rows");
        return r;
    }
    std::string text;
    for (const auto& row : rows->items()) {
        if (!row.is_array()) { r.errors.push_back("row is not an array"); return r; }
        bool first = true;
        for (const auto& tok : row.items()) {
            if (!tok.is_string()) { r.errors.push_back("token is not a string"); return r; }
            if (!first) text += ' ';
            first = false;
            text += tok.as_string();
        }
        text += '\n';
    }
    return parse_map(*id, text);
}

}  // namespace botched

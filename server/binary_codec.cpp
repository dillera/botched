#include "binary_codec.hpp"
#include "botched/serialize.hpp"
#include "server.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <limits>

namespace botched::binary {
namespace {
const char *orders[] = {"FORWARD_1",   "FORWARD_2", "BACK_1", "TURN_LEFT", "TURN_RIGHT",
                        "TURN_AROUND", "DROP",      "TOGGLE", "WAIT"};
const char *dirs[] = {"N", "E", "S", "W"};
const char *phases[] = {"PLANNING", "EXECUTING", "FINISHED", "LOBBY"};
const char *moves[] = {"ORDER", "CONVEYOR"};
const char *disables[] = {"PIT", "LASER", "TRAP", "TURRET"};
const char *noops[] = {"NOT_CARRYING", "TILE_OCCUPIED", "NO_CONSOLE", "TAR"};
const char *destroys[] = {"DROP_BLOCKED", "PIT"};
const char *reasons[] = {"TRAP_DAMAGE", "TRAP_DISABLE", "TURRET_DAMAGE", "TURRET_DISABLE", "SHUNT"};
const char *personalities[] = {"hauler", "opportunist", "saboteur"};
const char *codes[] = {"OK",
                       "REQUEST_TOO_LARGE",
                       "BAD_REQUEST",
                       "UNKNOWN_REQUEST",
                       "UNKNOWN_MATCH",
                       "UNKNOWN_MAP",
                       "TOO_MANY_MATCHES",
                       "INVALID_MAP",
                       "ALREADY_STARTED",
                       "MATCH_FULL",
                       "START_FAILED",
                       "NOT_STARTED",
                       "UNKNOWN_PLAYER",
                       "UNAUTHORIZED",
                       "INVALID_ORDERS",
                       "SNAPSHOT_REQUIRED",
                       "PLANNING_CLOSED",
                       "WRONG_ROUND",
                       "PROGRAM_LOCKED",
                       "INVALID_REVISION",
                       "STALE_REVISION",
                       "REVISION_CONFLICT",
                       "REQUEST_ID_REUSED",
                       "REQUEST_LIMIT"};
const char *events[] = {"",
                        "PLANNING_CLOSED",
                        "ROUND_STARTED",
                        "ROUND_ENDED",
                        "MATCH_ENDED",
                        "ORDER_EXECUTED",
                        "ORDER_NO_OP",
                        "ORDER_SKIPPED_DISABLED",
                        "ROBOT_MOVED",
                        "ROBOT_PUSHED",
                        "MOVE_BLOCKED",
                        "ROBOT_ROTATED",
                        "CRATE_SPAWNED",
                        "CRATE_PICKED_UP",
                        "CRATE_DROPPED",
                        "CRATE_DELIVERED",
                        "CRATE_MOVED",
                        "CRATE_DESTROYED",
                        "GROUP_TOGGLED",
                        "LASER_HIT",
                        "ROBOT_DISABLED",
                        "RESPAWN_REDIRECTED",
                        "ROBOT_RESPAWNED",
                        "TRAP_ARMED",
                        "TRAP_TRIGGERED",
                        "TRAP_HIT",
                        "TURRET_CAPTURED",
                        "TURRET_CHARGING",
                        "TURRET_FIRED",
                        "TAR_SLOWED",
                        "SCORE_AWARDED"};
std::int64_t num(const Json &j, const char *k) { return j.get_int(k).value_or(0); }
std::string str(const Json &j, const char *k) { return j.get_string(k).value_or(""); }
bool yes(const Json &j, const char *k) { return j.get_bool(k).value_or(false); }
template <size_t N> unsigned index(const char *const (&names)[N], const std::string &name) {
    for (unsigned i = 0; i < N; ++i)
        if (name == names[i])
            return i;
    throw CodecError("unknown semantic enumeration: " + name);
}
const Json &at(const Json &j, const char *k) {
    auto p = j.find(k);
    if (!p)
        throw CodecError(std::string("missing ") + k);
    return *p;
}
unsigned bits(const Json &j) {
    unsigned m = 0;
    for (const auto &v : j.items()) {
        auto n = v.as_int();
        if (n < 1 || n > 4)
            throw CodecError("player mask");
        m |= 1u << (n - 1);
    }
    return m;
}
Json array(unsigned m) {
    Json a = Json::array();
    for (int i = 1; i <= 4; ++i)
        if (m & (1u << (i - 1)))
            a.push(i);
    return a;
}
unsigned arrbits(const Json &j, const char *k) {
    auto p = j.find(k);
    return p ? bits(*p) : 0;
}
std::string read_key(Reader &r) {
    auto m = r.b();
    if (m == 0)
        return {};
    if (m == 1)
        return "b:" + std::to_string(r.i());
    return r.str();
}
void score_rows(Fields &f, const Json &j) {
    f.b(j.items().size());
    for (const auto &row : j.items())
        f.b(num(row, "player")).i(num(row, "score")).i(num(row, "deliveries")).i(num(row, "damage_taken"));
}
Json read_rows(Reader &r) {
    Json a = Json::array();
    unsigned n = r.b();
    while (n--) {
        Json j = Json::object();
        j.set("player", (int)r.b());
        j.set("score", (std::int64_t)r.i());
        j.set("deliveries", (std::int64_t)r.i());
        j.set("damage_taken", (std::int64_t)r.i());
        a.push(j);
    }
    return a;
}
struct Field {
    const char *key;
    char type;
};
// Field kinds: B byte, I u32, H u16, X signed coordinate, O order, D direction,
// M movement, N no-op, R destruction, G group, C console, Z boolean.
const std::vector<Field> layouts[] = {
    {},
    {},
    {},
    {},
    {},
    {{"player", 'B'}, {"order", 'O'}},
    {{"player", 'B'}, {"order", 'O'}, {"reason", 'N'}},
    {{"player", 'B'}, {"order", 'O'}},
    {{"player", 'B'}, {"from_x", 'X'}, {"from_y", 'X'}, {"x", 'X'}, {"y", 'X'}, {"cause", 'M'}},
    {{"player", 'B'}, {"by", 'B'}, {"from_x", 'X'}, {"from_y", 'X'}, {"x", 'X'}, {"y", 'X'}},
    {{"player", 'B'}, {"dir", 'D'}, {"x", 'X'}, {"y", 'X'}, {"cause", 'M'}},
    {{"player", 'B'}, {"from", 'D'}, {"to", 'D'}},
    {{"crate", 'I'}, {"dispenser", 'H'}, {"x", 'X'}, {"y", 'X'}},
    {{"player", 'B'}, {"crate", 'I'}, {"x", 'X'}, {"y", 'X'}},
    {{"player", 'B'}, {"crate", 'I'}, {"x", 'X'}, {"y", 'X'}},
    {{"player", 'B'}, {"crate", 'I'}, {"x", 'X'}, {"y", 'X'}, {"score", 'I'}},
    {{"crate", 'I'}, {"from_x", 'X'}, {"from_y", 'X'}, {"x", 'X'}, {"y", 'X'}},
    {{"crate", 'I'}, {"x", 'X'}, {"y", 'X'}, {"reason", 'R'}},
    {{"player", 'B'}, {"group", 'G'}, {"console", 'C'}, {"reversed", 'Z'}},
    {{"player", 'B'}, {"emitter_x", 'X'}, {"emitter_y", 'X'}, {"damage", 'B'}},
    {},
    {{"player", 'B'}, {"spawn_x", 'X'}, {"spawn_y", 'X'}, {"x", 'X'}, {"y", 'X'}},
    {{"player", 'B'}, {"x", 'X'}, {"y", 'X'}, {"heading", 'D'}},
    {{"player", 'B'},
     {"trap", 'B'},
     {"x", 'X'},
     {"y", 'X'},
     {"was_armed", 'Z'},
     {"previous_owner", 'B'},
     {"owner", 'B'}},
    {},
    {{"trap", 'B'}, {"player", 'B'}, {"damage", 'B'}, {"owner", 'B'}, {"points", 'B'}},
    {{"player", 'B'}, {"turret", 'B'}, {"x", 'X'}, {"y", 'X'}, {"previous_owner", 'B'}, {"owner", 'B'}},
    {{"turret", 'B'}, {"x", 'X'}, {"y", 'X'}, {"charge", 'B'}, {"owner", 'B'}},
    {{"turret", 'B'},
     {"x", 'X'},
     {"y", 'X'},
     {"owner", 'B'},
     {"player", 'B'},
     {"dir", 'D'},
     {"distance", 'B'},
     {"damage", 'B'},
     {"points", 'B'}},
    {{"player", 'B'}, {"order", 'O'}, {"x", 'X'}, {"y", 'X'}},
    {}};
void fields(Fields &f, const Json &j, unsigned e) {
    for (auto [k, t] : layouts[e]) {
        unsigned v = (unsigned)num(j, k);
        switch (t) {
        case 'I':
            f.i(v);
            continue;
        case 'H':
            f.h(v);
            continue;
        case 'Z':
            v = yes(j, k);
            break;
        case 'O':
            v = index(orders, str(j, k));
            break;
        case 'D':
            v = index(dirs, str(j, k));
            break;
        case 'M':
            v = index(moves, str(j, k));
            break;
        case 'N':
            v = index(noops, str(j, k));
            break;
        case 'R':
            v = index(destroys, str(j, k));
            break;
        case 'G':
            v = str(j, k).at(0) - 'A';
            break;
        case 'C': {
            auto s = str(j, k);
            unsigned ordinal = 0;
            auto result = std::from_chars(s.data() + 1, s.data() + s.size(), ordinal);
            if (result.ec != std::errc{})
                throw CodecError("console id");
            f.h(ordinal);
            continue;
        }
        default:
            break;
        }
        f.b(v & 255);
    }
}
void fields(Reader &r, Json &j, unsigned e) {
    for (auto [k, t] : layouts[e]) {
        if (t == 'I') {
            j.set(k, (std::int64_t)r.i());
            continue;
        }
        if (t == 'H') {
            j.set(k, (int)r.h());
            continue;
        }
        if (t == 'X') {
            j.set(k, r.coord());
            continue;
        }
        unsigned v = t == 'C' ? r.h() : r.b();
        switch (t) {
        case 'Z':
            j.set(k, v != 0);
            break;
        case 'O':
            j.set(k, orders[v]);
            break;
        case 'D':
            j.set(k, dirs[v]);
            break;
        case 'M':
            j.set(k, moves[v]);
            break;
        case 'N':
            j.set(k, noops[v]);
            break;
        case 'R':
            j.set(k, destroys[v]);
            break;
        case 'G':
            j.set(k, std::string(1, (char)('A' + v)));
            break;
        case 'C': {
            char b[16];
            std::snprintf(b, sizeof b, "T%03u", v);
            j.set(k, b);
            break;
        }
        default:
            j.set(k, (int)v);
        }
    }
}
unsigned dev(const Json &j) { return j.find("trap") ? 1 : j.find("turret") ? 2 : 0; }
void write_dev(Fields &f, const Json &j) {
    unsigned d = dev(j);
    f.b(d).b(d ? num(j, d == 1 ? "trap" : "turret") : 0);
}
std::int64_t utc_seconds(const std::string &s) {
    if (s.empty())
        return 0;
    int y, m, d, h, mi, se;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%dZ", &y, &m, &d, &h, &mi, &se) != 6)
        throw CodecError("UTC deadline");
    using namespace std::chrono;
    return duration_cast<seconds>(
               sys_days{year{y} / month{(unsigned)m} / day{(unsigned)d}}.time_since_epoch())
               .count() +
           h * 3600 + mi * 60 + se;
}
void context(Records &out, const std::string &m) {
    Fields f;
    f.str(m);
    out.add(1, f.span());
}
void bound(Records &out, const Binding &b) {
    Fields f;
    f.b(1).b(b.player).i(b.next_key).b(b.exhausted);
    out.add(8, f.span());
}
} // namespace
std::int64_t Reader::q() {
    std::uint64_t lo = i(), hi = i(), v = lo | (hi << 32);
    return v <= INT64_MAX ? (std::int64_t)v : -(std::int64_t)(UINT64_MAX - v) - 1;
}
std::string Reader::str() {
    auto n = b();
    const auto *p = bw_read_bytes(&r, n);
    if (!p)
        throw CodecError("truncated string");
    return {(const char *)p, n};
}
Fields &Fields::b(unsigned v) {
    if (v > 255)
        throw CodecError("u8 overflow");
    bw_write_u8(&w, v);
    return *this;
}
Fields &Fields::h(unsigned v) {
    if (v > 65535)
        throw CodecError("u16 overflow");
    bw_write_u16(&w, v);
    return *this;
}
Fields &Fields::i(std::uint32_t v) {
    bw_write_u32(&w, v);
    return *this;
}
Fields &Fields::q(std::int64_t v) {
    auto u = (std::uint64_t)v;
    i((std::uint32_t)u);
    i((std::uint32_t)(u >> 32));
    return *this;
}
Fields &Fields::str(const std::string &s) {
    b(s.size());
    bw_write_bytes(&w, (const uint8_t *)s.data(), s.size());
    return *this;
}
Fields &Fields::key(const std::string &s) {
    if (s.empty())
        return b(0);
    std::uint32_t v = 0;
    if (s.starts_with("b:")) {
        auto z = std::from_chars(s.data() + 2, s.data() + s.size(), v);
        if (z.ec == std::errc{} && z.ptr == s.data() + s.size() && s == "b:" + std::to_string(v))
            return b(1).i(v);
    }
    return b(2).str(s);
}
std::span<const std::uint8_t> Fields::span() const {
    if (w.error)
        throw CodecError("record overflow");
    return {data, w.pos};
}
void Records::add(std::uint8_t tag, std::span<const std::uint8_t> body) {
    std::uint8_t temp[101];
    size_t n = 0;
    if (!bw_encode_record(tag, body.data(), body.size(), temp, sizeof temp, &n))
        throw CodecError("invalid record " + std::to_string(tag));
    if (n > limit_ - bytes_.size())
        throw CodecError("transfer limit");
    if (bytes_.capacity() < bytes_.size() + n)
        bytes_.reserve(std::min(limit_, std::max(bytes_.size() + n, bytes_.capacity() * 2)));
    bytes_.insert(bytes_.end(), temp, temp + n);
}
void Records::text(unsigned field, const std::string &v) {
    if (v.size() > 65535)
        throw CodecError("text limit");
    size_t offset = 0;
    do {
        Fields f;
        auto n = std::min<size_t>(64, v.size() - offset);
        f.b(field).i(v.size()).i(offset).b(n);
        bw_write_bytes(&f.w, (const uint8_t *)v.data() + offset, n);
        add(15, f.span());
        offset += n;
    } while (offset < v.size());
}
Bytes packet(unsigned op, unsigned ex, std::span<const std::uint8_t> body) {
    Bytes out(body.size() + 3);
    size_t n;
    if (!bw_encode_packet(op, ex, body.data(), body.size(), out.data(), out.size(), &n))
        throw CodecError("invalid packet");
    return out;
}
const char *request_name(unsigned op) {
    static const char *names[] = {"LIST_MAPS",   "CREATE_MATCH",   "JOIN_MATCH",
                                  "START_MATCH", "SUBMIT_PROGRAM", "GET_SNAPSHOT",
                                  "GET_EVENTS",  "GET_MAP",        "GET_MATCH_STATUS"};
    return op >= 16 && op <= 24 ? names[op - 16] : "";
}
Command decode_request(std::span<const std::uint8_t> p, const Binding &binding) {
    if (!bw_validate_packet(p.data(), p.size(), 240))
        throw CodecError("invalid request");
    Command c;
    c.json = Json::object();
    c.json.set("type", request_name(p[0]));
    Reader r(p.subspan(3));
    auto scope_read = [&] {
        c.match = r.b() ? r.str() : binding.match;
        c.json.set("match_id", c.match);
    };
    switch (p[0]) {
    case 16:
        break;
    case 17: {
        unsigned present = r.b();
        auto map = r.str();
        unsigned players = r.b(), bots = r.b(), rounds = r.b(), target = r.h();
        auto secs = r.i();
        if (present & 1)
            c.json.set("map_id", map);
        if (present & 2)
            c.json.set("player_count", (int)players);
        if (present & 4)
            c.json.set("bots", (int)bots);
        if (present & 8)
            c.json.set("rounds", (int)rounds);
        if (present & 16)
            c.json.set("target_score", (int)target);
        if (present & 32)
            c.json.set("planning_seconds", (std::int64_t)secs);
        break;
    }
    case 18:
        c.match = r.str();
        c.json.set("match_id", c.match);
        break;
    case 20: {
        auto explicit_id = r.b();
        int player = explicit_id ? r.j() : binding.player;
        c.match = binding.match;
        c.json.set("match_id", c.match);
        c.json.set("player_id", player);
        c.json.set("session", binding.credential);
        c.json.set("round", r.j());
        c.json.set("revision", r.j());
        Json a = Json::array();
        for (int i = 0; i < 3; ++i) {
            auto o = r.b();
            a.push(o == 255 ? "INVALID" : orders[o]);
        }
        c.json.set("orders", a);
        c.json.set("ready", r.b() != 0);
        auto k = read_key(r);
        if (!k.empty())
            c.json.set("request_id", k);
        break;
    }
    case 22:
        scope_read();
        c.json.set("since_version", r.j());
        c.max_versions = r.h();
        break;
    default:
        scope_read();
        break;
    }
    return c;
}
Bytes encode_request(const Json &j, std::uint16_t ex, bool bound_scope) {
    for (const char *k : {"player_id", "round", "revision", "since_version", "planning_seconds",
                          "player_count", "bots", "rounds", "target_score", "max_versions"})
        if (j.find(k)) {
            auto v = j.get_int(k);
            if (!v || *v < INT32_MIN || *v > INT32_MAX)
                throw CodecError("request integer domain");
        }
    for (const char *k : {"type", "match_id", "map_id", "request_id"})
        if (j.find(k) && !j.get_string(k))
            throw CodecError("request string type");
    if (j.find("ready") && !j.get_bool("ready"))
        throw CodecError("ready type");
    if (str(j, "type") == "SUBMIT_PROGRAM") {
        for (const char *k : {"player_id", "round", "revision"})
            if (!j.get_int(k))
                throw CodecError("missing submit integer");
        const auto &a = at(j, "orders");
        if (!a.is_array() || a.items().size() != 3)
            throw CodecError("three orders required");
    }
    unsigned op = 0;
    for (unsigned i = 16; i <= 24; ++i)
        if (str(j, "type") == request_name(i))
            op = i;
    if (!op)
        throw CodecError("request name");
    Fields f;
    auto scope_write = [&] {
        f.b(bound_scope ? 0 : 1);
        if (!bound_scope)
            f.str(str(j, "match_id"));
    };
    switch (op) {
    case 16:
        break;
    case 17: {
        unsigned present = 0;
        const char *keys[] = {"map_id", "player_count", "bots", "rounds", "target_score", "planning_seconds"};
        for (unsigned i = 0; i < 6; ++i)
            if (j.find(keys[i]))
                present |= 1 << i;
        f.b(present)
            .str(str(j, "map_id"))
            .b(num(j, "player_count"))
            .b(num(j, "bots"))
            .b(num(j, "rounds"))
            .h(num(j, "target_score"))
            .i(num(j, "planning_seconds"));
        break;
    }
    case 18:
        f.str(str(j, "match_id"));
        break;
    case 20:
        f.b(1).i(num(j, "player_id")).i(num(j, "round")).i(num(j, "revision"));
        for (const auto &o : at(j, "orders").items()) {
            unsigned code = 255;
            for (unsigned i = 0; i < 9; ++i)
                if (o.as_string() == orders[i])
                    code = i;
            f.b(code);
        }
        f.b(yes(j, "ready")).key(str(j, "request_id"));
        break;
    case 22:
        scope_write();
        f.i(num(j, "since_version")).h(j.get_int("max_versions").value_or(16));
        break;
    default:
        scope_write();
    }
    return packet(op, ex, f.span());
}
void encode_event(Records &out, const Json &e) {
    unsigned code = index(events, str(e, "type"));
    Fields f;
    f.b(code).i(num(e, "state_version")).b(num(e, "round")).b(num(e, "tick")).i(num(e, "sequence"));
    const auto &p = at(e, "params");
    switch (code) {
    case 1:
        f.b(arrbits(p, "missing"));
        break;
    case 2:
        f.b(num(p, "round"));
        break;
    case 3:
        f.b(num(p, "round"));
        score_rows(f, at(p, "scores"));
        break;
    case 4:
        f.b(arrbits(p, "winners")).b(p.find("reason") ? (str(p, "reason") == "target" ? 1 : 2) : 0);
        score_rows(f, at(p, "scores"));
        break;
    case 20:
        f.b(num(p, "player"))
            .b(index(disables, str(p, "cause")))
            .b(num(p, "x"))
            .b(num(p, "y"))
            .b(p.find("by") ? 1 : 0)
            .b(num(p, "by"));
        write_dev(f, p);
        break;
    case 24:
        f.b(num(p, "trap")).b(num(p, "x")).b(num(p, "y")).b(num(p, "owner")).b(arrbits(p, "victims"));
        break;
    case 30:
        f.b(num(p, "player"))
            .b(num(p, "points"))
            .b(index(reasons, str(p, "reason")))
            .b(num(p, "victim"))
            .i(num(p, "score"));
        write_dev(f, p);
        break;
    default:
        fields(f, p, code);
    }
    out.add(21, f.span());
}
Json decode_event(std::span<const std::uint8_t> rec, const std::string &match) {
    if (!bw_validate_record(rec.data(), rec.size()) || rec[0] != 21)
        throw CodecError("event record");
    Reader r(rec.subspan(2));
    unsigned code = r.b();
    Json e = Json::object();
    e.set("match_id", match);
    e.set("state_version", (std::int64_t)r.i());
    e.set("round", (int)r.b());
    e.set("tick", (int)r.b());
    e.set("sequence", (std::int64_t)r.i());
    e.set("type", events[code]);
    Json p = Json::object();
    switch (code) {
    case 1:
        p.set("missing", array(r.b()));
        break;
    case 2:
        p.set("round", (int)r.b());
        break;
    case 3:
        p.set("round", (int)r.b());
        p.set("scores", read_rows(r));
        break;
    case 4: {
        p.set("winners", array(r.b()));
        unsigned reason = r.b();
        p.set("scores", read_rows(r));
        if (reason)
            p.set("reason", reason == 1 ? "target" : "cap");
        break;
    }
    case 20: {
        p.set("player", (int)r.b());
        p.set("cause", disables[r.b()]);
        p.set("x", r.coord());
        p.set("y", r.coord());
        unsigned flags = r.b(), by = r.b(), d = r.b(), id = r.b();
        if (d)
            p.set(d == 1 ? "trap" : "turret", (int)id);
        if (flags)
            p.set("by", (int)by);
        break;
    }
    case 24:
        p.set("trap", (int)r.b());
        p.set("x", r.coord());
        p.set("y", r.coord());
        p.set("owner", (int)r.b());
        p.set("victims", array(r.b()));
        break;
    case 30: {
        p.set("player", (int)r.b());
        p.set("points", (int)r.b());
        p.set("reason", reasons[r.b()]);
        p.set("victim", (int)r.b());
        p.set("score", (std::int64_t)r.i());
        unsigned d = r.b(), id = r.b();
        if (d)
            p.set(d == 1 ? "trap" : "turret", (int)id);
        break;
    }
    default:
        fields(r, p, code);
    }
    e.set("params", p);
    return e;
}
std::uint32_t profile_bytes(const Map &map) {
    unsigned a = 0, w = 0;
    for (int y = 0; y < map.height; ++y)
        for (int x = 0; x < map.width; ++x) {
            auto t = map.tile({x, y}).terrain;
            if (t != Terrain::Floor && t != Terrain::Wall && t != Terrain::Pit && t != Terrain::TarGenerator)
                ++a;
            if (map.walkable({x, y}))
                ++w;
        }
    return 841 + (map.tiles.size() + 1) / 2 + 5 * a + 7 * (w + 4) + 2 * map.groups.size() +
           4 * (map.traps.size() + map.turrets.size());
}
void encode_map_meta(Records &out, const Map &map, std::uint32_t handle) {
    unsigned attrs = 0;
    for (const auto &t : map.tiles)
        if (t.terrain != Terrain::Floor && t.terrain != Terrain::Wall && t.terrain != Terrain::Pit &&
            t.terrain != Terrain::TarGenerator)
            ++attrs;
    Fields f;
    f.i(handle).str(map.id).b(map.width).b(map.height).h(map.tiles.size()).h(attrs);
    out.add(16, f.span());
}
void encode_response(Records &out, const Json &j, const Map *map, std::uint32_t handle,
                     const Binding *binding,
                     const std::function<std::uint32_t(const std::string &)> &map_id) {
    std::string type = str(j, "type");
    Fields f;
    if (type == "ERROR") {
        f.b(index(codes, str(j, "code")));
        out.add(6, f.span());
        Fields echo;
        echo.str(str(j, "request_type")).key(str(j, "request_id"));
        out.add(7, echo.span());
        out.text(3, str(j, "message"));
        return;
    }
    if (type == "MAPS") {
        const auto &maps = at(j, "maps");
        f.h(maps.items().size());
        out.add(19, f.span());
        for (const auto &m : maps.items()) {
            Fields entry;
            entry.i(map_id(str(m, "map_id")))
                .str(str(m, "map_id"))
                .b(num(m, "min_players"))
                .b(num(m, "max_players"));
            out.add(20, entry.span());
            out.text(1, str(m, "name"));
            out.text(2, str(m, "description"));
        }
        return;
    }
    const Json *nested = nullptr;
    if (type == "SNAPSHOT")
        nested = &at(j, "snapshot");
    if (type == "MATCH_STATUS")
        nested = &at(j, "status");
    context(out, str(nested ? *nested : j, "match_id"));
    if (type == "MATCH_CREATED") {
        f.b(num(j, "player_count")).b(num(j, "bots")).b(num(j, "rounds")).h(num(j, "target_score"));
        out.add(2, f.span());
    } else if (type == "JOINED") {
        f.b(num(j, "player_id")).b(yes(j, "started")).str(str(j, "session"));
        out.add(3, f.span());
        if (!binding)
            throw CodecError("missing joined binding");
        bound(out, *binding);
    } else if (type == "BOUND") {
        if (!binding)
            throw CodecError("missing binding");
        bound(out, *binding);
    } else if (type == "MATCH_STARTED") {
        f.b(num(j, "player_count"));
        out.add(4, f.span());
    } else if (type == "SUBMIT_PROGRAM") {
        unsigned detail = 4;
        auto message = str(j, "message");
        if (message == "accepted")
            detail = 0;
        else if (message == "unchanged")
            detail = 1;
        else if (message == "exactly 3 orders required")
            detail = 2;
        else if (message == "unknown order token")
            detail = 3;
        f.i(num(j, "player_id"))
            .i(num(j, "round"))
            .i(num(j, "revision"))
            .i(num(j, "state_version"))
            .b(yes(j, "ok"))
            .b(index(codes, str(j, "code")))
            .b(detail)
            .key(str(j, "request_id"));
        out.add(5, f.span());
        out.text(3, message);
    } else if (type == "MATCH_STATUS") {
        const auto &s = *nested;
        bool lobby = str(s, "phase") == "LOBBY";
        f.b(index(phases, str(s, "phase")))
            .b(num(s, "round"))
            .b(num(s, "tick"))
            .b(num(s, "rounds_total"))
            .h(num(s, "target_score"))
            .i(num(s, "state_version"))
            .b(num(s, "player_count"))
            .b(num(s, "joined"))
            .b(num(s, "capacity"))
            .b(num(s, "bots"))
            .b(arrbits(s, "submitted_players"))
            .b(arrbits(s, "ready_players"))
            .b(arrbits(s, "winners"));
        auto ids = s.find("bot_players");
        f.b(lobby ? 0 : (ids ? ids->items().size() : 0));
        if (ids) {
            const auto &kinds = at(s, "bot_personalities");
            for (size_t i = 0; i < ids->items().size(); ++i)
                f.b(ids->items()[i].as_int()).b(index(personalities, kinds.items()[i].as_string()));
        }
        out.add(14, f.span());
    } else if (type == "SNAPSHOT") {
        if (!map)
            throw CodecError("missing map");
        const auto &s = *nested;
        encode_map_meta(out, *map, handle);
        auto traps = s.find("traps"), turrets = s.find("turrets");
        bool deadline = !at(s, "planning_deadline_utc").is_null();
        f.b(index(phases, str(s, "phase")))
            .b(num(s, "round"))
            .b(num(s, "tick"))
            .b(num(s, "rounds_total"))
            .h(num(s, "target_score"))
            .i(num(s, "next_crate_id"))
            .i(handle)
            .b((s.find("target_score") ? 1 : 0) | (deadline ? 2 : 0))
            .b(at(s, "players").items().size())
            .h(at(s, "crates").items().size())
            .b(at(s, "groups").items().size())
            .b(traps ? traps->items().size() : 0)
            .b(turrets ? turrets->items().size() : 0)
            .b(arrbits(s, "submitted_players"))
            .b(arrbits(s, "ready_players"))
            .q(deadline ? utc_seconds(str(s, "planning_deadline_utc")) : 0);
        out.add(9, f.span());
        for (const auto &p : at(s, "players").items()) {
            Fields v;
            unsigned flags = (yes(p, "respawn_pending") ? 1 : 0) | (!at(p, "carrying").is_null() ? 2 : 0) |
                             (p.find("sabotage") ? 4 : 0) | (p.find("pushed_by") ? 8 : 0);
            v.b(num(p, "id"))
                .b(flags)
                .b(num(p, "x"))
                .b(num(p, "y"))
                .b(index(dirs, str(p, "heading")))
                .b(num(p, "damage"))
                .i(num(p, "carrying"))
                .i(num(p, "score"))
                .i(num(p, "deliveries"))
                .i(num(p, "damage_taken"))
                .i(num(p, "sabotage"))
                .b(num(p, "pushed_by"));
            out.add(10, v.span());
        }
        for (const auto &c : at(s, "crates").items()) {
            Fields v;
            v.i(num(c, "id")).b(num(c, "x")).b(num(c, "y")).b(num(c, "carrier_player_id"));
            out.add(11, v.span());
        }
        for (const auto &g : at(s, "groups").items()) {
            Fields v;
            v.b(str(g, "id").at(0) - 'A').b(yes(g, "reversed"));
            out.add(12, v.span());
        }
        for (unsigned kind = 1; kind <= 2; ++kind) {
            auto a = kind == 1 ? traps : turrets;
            if (a)
                for (const auto &h : a->items()) {
                    Fields v;
                    v.b(kind)
                        .b(num(h, "id"))
                        .b(kind == 1 ? yes(h, "armed") : num(h, "charge"))
                        .b(num(h, "owner"));
                    out.add(13, v.span());
                }
        }
    } else if (type == "MAP") {
        if (!map)
            throw CodecError("missing map");
        encode_map_meta(out, *map, handle);
        for (size_t first = 0; first < map->tiles.size(); first += 128) {
            Fields v;
            auto n = std::min<size_t>(128, map->tiles.size() - first);
            v.h(first).b(n);
            for (size_t i = 0; i < n; i += 2) {
                unsigned byte = (unsigned)map->tiles[first + i].terrain;
                if (i + 1 < n)
                    byte |= (unsigned)map->tiles[first + i + 1].terrain << 4;
                v.b(byte);
            }
            out.add(17, v.span());
        }
        for (int y = 0; y < map->height; ++y)
            for (int x = 0; x < map->width; ++x) {
                const auto &t = map->tile({x, y});
                if (t.terrain == Terrain::Floor || t.terrain == Terrain::Wall || t.terrain == Terrain::Pit ||
                    t.terrain == Terrain::TarGenerator)
                    continue;
                unsigned value = 0, aux = 0;
                switch (t.terrain) {
                case Terrain::Spawn:
                    value = t.owner;
                    aux = (unsigned)t.dir;
                    break;
                case Terrain::Dock:
                case Terrain::Dispenser:
                case Terrain::Trap:
                case Terrain::TrapConsole:
                case Terrain::Turret:
                case Terrain::TurretPad:
                    value = t.owner;
                    break;
                case Terrain::Conveyor:
                    value = t.group - 'A';
                    aux = (unsigned)t.dir;
                    break;
                case Terrain::Console:
                    value = t.group - 'A';
                    break;
                case Terrain::LaserEmitter:
                    aux = (unsigned)t.dir;
                    break;
                default:
                    break;
                }
                Fields v;
                v.b(x).b(y).h(value).b(aux);
                out.add(18, v.span());
            }
    } else if (type == "EVENTS") {
        f.i(num(j, "since_version")).i(at(j, "events").items().size());
        out.add(22, f.span());
        for (const auto &e : at(j, "events").items())
            encode_event(out, e);
    } else
        throw CodecError("unknown response type");
}

Json decode_response(std::span<const std::uint8_t> bytes, unsigned origin, std::uint32_t version,
                     bool error) {
    Json out = Json::object(), world = Json::object(), status = Json::object(), board = Json::object();
    Json players = Json::array(), crates = Json::array(), groups = Json::array(), traps = Json::array(),
         turrets = Json::array(), ev = Json::array(), maps = Json::array();
    std::string match, message;
    Map map;
    unsigned snap_flags = 0, submitted = 0, ready = 0;
    std::int64_t deadline = 0;
    Json map_entry;
    bool have_entry = false;
    // Validate the complete logical grammar using canonical synthetic pages, before constructing output.
    bw_stream stream;
    bw_stream_init(&stream, 0);
    size_t offset = 0;
    while (offset < bytes.size()) {
        size_t end = offset;
        while (end < bytes.size()) {
            if (bytes.size() - end < 2 || bytes[end + 1] + 2u > bytes.size() - end ||
                !bw_validate_record(bytes.data() + end, bytes[end + 1] + 2u))
                throw CodecError("invalid record stream");
            size_t next = end + bytes[end + 1] + 2;
            if (next - offset > 218)
                break;
            end = next;
        }
        if (end == offset)
            throw CodecError("oversize record");
        Bytes p(22 + end - offset);
        bw_writer w;
        bw_writer_init(&w, p.data(), p.size());
        bw_write_u8(&w, 0x82);
        bw_write_u16(&w, 1);
        bw_write_u32(&w, 1);
        bw_write_u32(&w, offset);
        bw_write_u32(&w, bytes.size());
        bw_write_u32(&w, version);
        bw_write_u8(&w, origin);
        bw_write_u8(&w, (error ? 2 : 0) | (end == bytes.size() ? 1 : 0));
        bw_write_u8(&w, end - offset);
        bw_write_bytes(&w, bytes.data() + offset, end - offset);
        int apply;
        if (!bw_stream_accept(&stream, p.data(), p.size(), &apply))
            throw CodecError("invalid logical response");
        offset = end;
    }
    if (bytes.empty())
        throw CodecError("empty response");
    for (size_t pos = 0; pos < bytes.size(); pos += bytes[pos + 1] + 2) {
        unsigned tag = bytes[pos];
        Reader r(bytes.subspan(pos + 2, bytes[pos + 1]));
        switch (tag) {
        case 1:
            match = r.str();
            break;
        case 2:
            out.set("type", "MATCH_CREATED");
            out.set("ok", true);
            out.set("match_id", match);
            out.set("player_count", (int)r.b());
            out.set("bots", (int)r.b());
            out.set("rounds", (int)r.b());
            out.set("target_score", (int)r.h());
            break;
        case 3:
            out.set("type", "JOINED");
            out.set("ok", true);
            out.set("match_id", match);
            out.set("player_id", (int)r.b());
            {
                bool started = r.b();
                out.set("session", r.str());
                out.set("started", started);
            }
            break;
        case 4:
            out.set("type", "MATCH_STARTED");
            out.set("ok", true);
            out.set("match_id", match);
            out.set("player_count", (int)r.b());
            break;
        case 5: {
            auto player = r.j(), round = r.j(), revision = r.j();
            auto v = r.i();
            bool ok = r.b();
            auto code = r.b();
            r.b();
            auto key = read_key(r);
            out.set("type", "SUBMIT_PROGRAM");
            out.set("ok", ok);
            out.set("code", codes[code]);
            out.set("message", "");
            out.set("match_id", match);
            out.set("player_id", player);
            out.set("round", round);
            out.set("revision", revision);
            out.set("state_version", (std::int64_t)v);
            if (!key.empty())
                out.set("request_id", key);
            break;
        }
        case 6:
            out.set("type", "ERROR");
            out.set("ok", false);
            out.set("code", codes[r.b()]);
            out.set("message", "");
            break;
        case 7: {
            auto type = r.str(), key = read_key(r);
            if (!type.empty())
                out.set("request_type", type);
            if (!key.empty())
                out.set("request_id", key);
            break;
        }
        case 8:
            if (origin == 2) {
                out.set("type", "BOUND");
                out.set("ok", true);
                out.set("match_id", match);
            }
            break; // adapter-only fields are not part of JSON JOINED
        case 9: {
            world.set("phase", phases[r.b()]);
            world.set("round", (int)r.b());
            world.set("tick", (int)r.b());
            world.set("rounds_total", (int)r.b());
            auto target = r.h(), next = r.i();
            r.i();
            snap_flags = r.b();
            if (snap_flags & 1)
                world.set("target_score", (int)target);
            world.set("next_crate_id", (std::int64_t)next);
            r.b();
            r.h();
            r.b();
            r.b();
            r.b();
            submitted = r.b();
            ready = r.b();
            deadline = r.q();
            break;
        }
        case 10: {
            Json p = Json::object();
            auto id = r.b(), flags = r.b();
            int x = r.coord(), y = r.coord();
            auto heading = r.b(), damage = r.b(), carry = r.i(), score = r.i(), deliveries = r.i(),
                 taken = r.i(), sabotage = r.i(), by = r.b();
            p.set("id", (int)id);
            p.set("x", flags & 1 ? Json(nullptr) : Json(x));
            p.set("y", flags & 1 ? Json(nullptr) : Json(y));
            p.set("heading", dirs[heading]);
            p.set("damage", (int)damage);
            p.set("carrying", flags & 2 ? Json((std::int64_t)carry) : Json(nullptr));
            p.set("score", (std::int64_t)score);
            p.set("deliveries", (std::int64_t)deliveries);
            p.set("damage_taken", (std::int64_t)taken);
            p.set("respawn_pending", (flags & 1) != 0);
            if (flags & 4)
                p.set("sabotage", (std::int64_t)sabotage);
            if (flags & 8)
                p.set("pushed_by", (int)by);
            players.push(p);
            break;
        }
        case 11: {
            Json c = Json::object();
            auto id = r.i();
            int x = r.coord(), y = r.coord();
            auto carrier = r.b();
            c.set("id", (std::int64_t)id);
            c.set("x", carrier ? Json(nullptr) : Json(x));
            c.set("y", carrier ? Json(nullptr) : Json(y));
            c.set("carrier_player_id", carrier ? Json((int)carrier) : Json(nullptr));
            crates.push(c);
            break;
        }
        case 12: {
            Json g = Json::object();
            g.set("id", std::string(1, (char)('A' + r.b())));
            g.set("reversed", r.b() != 0);
            groups.push(g);
            break;
        }
        case 13: {
            Json h = Json::object();
            auto kind = r.b(), id = r.b(), value = r.b(), owner = r.b();
            h.set("id", (int)id);
            if (kind == 1)
                h.set("armed", value != 0);
            else
                h.set("charge", (int)value);
            h.set("owner", owner ? Json((int)owner) : Json(nullptr));
            (kind == 1 ? traps : turrets).push(h);
            break;
        }
        case 14: {
            auto phase = r.b(), round = r.b(), tick = r.b(), rounds = r.b(), target = r.h(), v = r.i(),
                 count = r.b(), joined = r.b(), capacity = r.b(), bots = r.b(), sub = r.b(), rd = r.b(),
                 winners = r.b(), nbots = r.b();
            status.set("match_id", match);
            status.set("phase", phases[phase]);
            if (phase == 3) {
                status.set("joined", (int)joined);
                status.set("capacity", (int)capacity);
                status.set("bots", (int)bots);
                status.set("rounds_total", (int)rounds);
                status.set("target_score", (int)target);
            } else {
                status.set("round", (int)round);
                status.set("rounds_total", (int)rounds);
                status.set("target_score", (int)target);
                status.set("tick", (int)tick);
                status.set("state_version", (std::int64_t)v);
                status.set("player_count", (int)count);
                status.set("submitted_players", array(sub));
                status.set("ready_players", array(rd));
                if (phase == 2)
                    status.set("winners", array(winners));
                status.set("bots", (int)bots);
                Json ids = Json::array(), kinds = Json::array();
                while (nbots--) {
                    ids.push((int)r.b());
                    kinds.push(personalities[r.b()]);
                }
                status.set("bot_players", ids);
                status.set("bot_personalities", kinds);
            }
            break;
        }
        case 15: {
            unsigned field = r.b();
            r.i();
            r.i();
            auto n = r.b();
            auto data = bw_read_bytes(&r.r, n);
            std::string part((const char *)data, n);
            if (field == 3)
                message += part;
            else {
                const char *k = field == 1 ? "name" : "description";
                map_entry.set(k, str(map_entry, k) + part);
            }
            break;
        }
        case 16: {
            r.i();
            map.id = r.str();
            map.width = r.b();
            map.height = r.b();
            map.tiles.resize(r.h());
            r.h();
            board.set("width", map.width);
            board.set("height", map.height);
            board.set("map_id", map.id);
            break;
        }
        case 17: {
            auto first = r.h(), n = r.b();
            for (unsigned i = 0; i < n; i += 2) {
                auto v = r.b();
                map.tiles.at(first + i).terrain = (Terrain)(v & 15);
                if (i + 1 < n)
                    map.tiles.at(first + i + 1).terrain = (Terrain)(v >> 4);
            }
            break;
        }
        case 18: {
            int x = r.b(), y = r.b();
            auto value = r.h(), aux = r.b();
            auto &t = map.tile({x, y});
            switch (t.terrain) {
            case Terrain::Spawn:
                t.owner = value;
                t.dir = (Dir)aux;
                break;
            case Terrain::Dock:
            case Terrain::Dispenser:
            case Terrain::Trap:
            case Terrain::TrapConsole:
            case Terrain::Turret:
            case Terrain::TurretPad:
                t.owner = value;
                break;
            case Terrain::Conveyor:
                t.group = (char)('A' + value);
                t.dir = (Dir)aux;
                break;
            case Terrain::Console:
                t.group = (char)('A' + value);
                break;
            case Terrain::LaserEmitter:
                t.dir = (Dir)aux;
                break;
            default:
                throw CodecError("unexpected attribute");
            }
            break;
        }
        case 19:
            break;
        case 20: {
            if (have_entry)
                maps.push(map_entry);
            have_entry = true;
            map_entry = Json::object();
            r.i();
            map_entry.set("map_id", r.str());
            map_entry.set("name", "");
            map_entry.set("description", "");
            map_entry.set("min_players", (int)r.b());
            map_entry.set("max_players", (int)r.b());
            break;
        }
        case 21:
            ev.push(decode_event(bytes.subspan(pos, bytes[pos + 1] + 2), match));
            break;
        case 22:
            break;
        default:
            throw CodecError("record tag");
        }
    }
    if (error || origin == 20) {
        out.set("message", message);
        return out;
    }
    if (origin == 16) {
        if (have_entry)
            maps.push(map_entry);
        out.set("type", "MAPS");
        out.set("ok", true);
        out.set("maps", maps);
    }
    if (origin == 21) {
        if ((snap_flags & 2) && (deadline > INT64_MAX / 1000 || deadline < INT64_MIN / 1000))
            throw CodecError("deadline outside server clock domain");
        Json snap = Json::object();
        snap.set("protocol", "botched/1");
        snap.set("match_id", match);
        snap.set("rules_version", "botched/1");
        snap.set("state_version", (std::int64_t)version);
        for (const auto &[k, v] : world.members())
            snap.set(k, v);
        snap.set("players", players);
        snap.set("crates", crates);
        snap.set("groups", groups);
        if (!traps.items().empty())
            snap.set("traps", traps);
        if (!turrets.items().empty())
            snap.set("turrets", turrets);
        snap.set("board", board);
        snap.set("submitted_players", array(submitted));
        snap.set("ready_players", array(ready));
        snap.set("planning_deadline_utc",
                 snap_flags & 2 ? Json(server::format_utc(deadline * 1000)) : Json(nullptr));
        out.set("type", "SNAPSHOT");
        out.set("ok", true);
        out.set("snapshot", snap);
    }
    if (origin == 22) {
        out.set("type", "EVENTS");
        out.set("ok", true);
        out.set("match_id", match);
        out.set("state_version", (std::int64_t)version);
        out.set("events", ev);
    }
    if (origin == 23) {
        map.finalize();
        out.set("type", "MAP");
        out.set("ok", true);
        out.set("match_id", match);
        out.set("map", map_to_json(map));
    }
    if (origin == 24) {
        out.set("type", "MATCH_STATUS");
        out.set("ok", true);
        out.set("status", status);
    }
    return out;
}
} // namespace botched::binary

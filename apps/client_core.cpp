#include "client_core.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
#include <ostream>
#include <sstream>
#include <thread>

#include "ascii_render.hpp"
#include "bot.hpp"
#include "botched/map.hpp"
#include "line_buffer.hpp"

namespace botched::client {

// ------------------------------------------------------------------ orders

namespace {
struct Code { const char* code; const char* token; };
constexpr Code kCodes[] = {
    {"F", "FORWARD_1"}, {"F1", "FORWARD_1"}, {"F2", "FORWARD_2"}, {"FF", "FORWARD_2"}, {"B", "BACK_1"},
    {"L", "TURN_LEFT"}, {"R", "TURN_RIGHT"}, {"U", "TURN_AROUND"}, {"A", "TURN_AROUND"}, {"D", "DROP"},
    {"T", "TOGGLE"}, {"W", "WAIT"},
};
}  // namespace

const char* order_help() {
    return "F=forward 1  F2=forward 2  B=back 1  L/R=turn left/right  U=turn around  D=drop crate  "
           "T=toggle console  W=wait";
}

std::optional<std::array<std::string, 3>> parse_program(const std::string& line, std::string& error) {
    std::string norm;
    for (char c : line) norm += (c == ',' ) ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::istringstream in(norm);
    std::vector<std::string> toks;
    std::string t;
    while (in >> t) toks.push_back(t);
    if (toks.size() != 3) {
        error = "enter exactly three orders, e.g.  F2 R T   (" + std::string(order_help()) + ")";
        return std::nullopt;
    }
    std::array<std::string, 3> out;
    for (std::size_t i = 0; i < 3; ++i) {
        std::string token;
        for (const auto& c : kCodes) if (toks[i] == c.code) token = c.token;
        if (token.empty() && parse_order(toks[i])) token = toks[i];  // full name
        if (token.empty()) {
            error = "unknown order '" + toks[i] + "'  (" + std::string(order_help()) + ")";
            return std::nullopt;
        }
        out[i] = token;
    }
    return out;
}

std::string order_code(const std::string& token) {
    for (const auto& c : kCodes) if (token == c.token) return c.code;  // first match is the short form
    return token;
}

std::optional<std::int64_t> parse_utc_ms(const std::string& s) {
    if (s.size() != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z')
        return std::nullopt;
    auto num = [&](std::size_t pos, std::size_t len) -> std::int64_t {
        std::int64_t v = 0;
        for (std::size_t i = pos; i < pos + len; ++i) {
            if (s[i] < '0' || s[i] > '9') return -1;
            v = v * 10 + (s[i] - '0');
        }
        return v;
    };
    std::int64_t y = num(0, 4), m = num(5, 2), d = num(8, 2), hh = num(11, 2), mm = num(14, 2), ss = num(17, 2);
    if (y < 0 || m < 1 || m > 12 || d < 1 || d > 31 || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 60)
        return std::nullopt;
    // days-from-civil (Howard Hinnant)
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;
    const std::int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = era * 146097 + doe - 719468;
    return ((days * 24 + hh) * 60 + mm) * 60 * 1000 + ss * 1000;
}

// ------------------------------------------------------------------ events

namespace {
int pint(const Json& p, const char* k) { return static_cast<int>(p.get_int(k).value_or(0)); }
}  // namespace

namespace {
std::string xy(const Json& p, const char* kx, const char* ky) {
    return "(" + std::to_string(pint(p, kx)) + "," + std::to_string(pint(p, ky)) + ")";
}
std::string P(int id) { return "P" + std::to_string(id); }
}  // namespace

// One line per thing that happened, in exactly the order the server logged it. Nothing is inferred
// from positions: every movement line comes from a ROBOT_MOVED / ROBOT_PUSHED / MOVE_BLOCKED /
// CRATE_MOVED event. Phases are visible from the wording: "P1 F2" lines are robot orders, "belt ..."
// lines are the conveyor phase after all orders.
std::vector<std::string> summarize_events(const Json& events) {
    std::vector<std::string> lines;
    const auto& items = events.items();
    int cur_round = -1, cur_tick = -1;
    std::string pending;  // an ORDER_EXECUTED line waiting to see whether the order did something
    int pending_player = 0;
    std::vector<std::pair<int, std::string>> ordered;  // (player, order code) this tick, for belt notes
    auto order_of = [&](int pl) -> std::string {
        for (const auto& o : ordered) if (o.first == pl) return o.second;
        return "";
    };
    auto flush_pending = [&] {
        if (!pending.empty()) lines.push_back(pending);
        pending.clear();
        pending_player = 0;
    };
    auto act = [&](int player, const std::string& text) {  // text belongs to an order actor
        if (pending_player == player && !pending.empty()) {
            lines.push_back(pending + ": " + text);
            pending.clear();
            pending_player = 0;
        } else {
            flush_pending();
            lines.push_back("    " + P(player) + " " + text);
        }
    };
    auto plain = [&](const std::string& text) { flush_pending(); lines.push_back("    " + text); };

    for (std::size_t i = 0; i < items.size(); ++i) {
        const Json& e = items[i];
        const int round = pint(e, "round"), tick = pint(e, "tick");
        if (round != cur_round || tick != cur_tick) {
            flush_pending();
            cur_round = round;
            cur_tick = tick;
            ordered.clear();
            if (tick > 0) lines.push_back("-- round " + std::to_string(round) + ", tick " + std::to_string(tick) + " --");
        }
        const std::string type = e.get_string("type").value_or("");
        const Json* pp = e.find("params");
        if (!pp) continue;
        const Json& p = *pp;
        const int pl = pint(p, "player");
        const std::string who = P(pl);
        const std::string dirn = p.get_string("dir").value_or("?");

        if (type == "ORDER_EXECUTED") {
            flush_pending();
            const std::string code = order_code(p.get_string("order").value_or("?"));
            ordered.emplace_back(pl, code);
            pending = "  " + who + " " + code;
            pending_player = pl;
            // Orders with no visible effect (WAIT) read as a line of their own.
            if (code == "W") { lines.push_back(pending + ": waits"); pending.clear(); pending_player = 0; }
        } else if (type == "ORDER_NO_OP") {
            const std::string code = order_code(p.get_string("order").value_or("?"));
            const std::string why = p.get_string("reason").value_or("");
            ordered.emplace_back(pl, code);
            flush_pending();
            lines.push_back("  " + who + " " + code + ": nothing happened" + (why.empty() ? "" : " (" + why + ")"));
        } else if (type == "ORDER_SKIPPED_DISABLED") {
            plain(who + " is disabled: order skipped");
        } else if (type == "ROBOT_ROTATED") {
            act(pl, "turns " + p.get_string("from").value_or("?") + " -> " + p.get_string("to").value_or("?"));
        } else if (type == "ROBOT_MOVED") {
            const bool belt = p.get_string("cause").value_or("") == "CONVEYOR";
            const std::string route = xy(p, "from_x", "from_y") + " -> " + xy(p, "x", "y");
            if (belt) {
                std::string o = order_of(pl);
                flush_pending();
                lines.push_back("  belt moves " + who + " " + route + (o == "W" ? "   (they ordered WAIT)" : ""));
            } else {
                act(pl, "drives " + route);
            }
        } else if (type == "ROBOT_PUSHED") {
            // The pusher's own ROBOT_MOVED follows; look ahead (same tick) only to name the cause.
            const int by = pint(p, "by");
            std::string cause = "ORDER";
            for (std::size_t j = i + 1; j < items.size(); ++j) {
                if (pint(items[j], "tick") != tick || pint(items[j], "round") != round) break;
                if (items[j].get_string("type").value_or("") != "ROBOT_MOVED") continue;
                const Json* jp = items[j].find("params");
                if (jp && pint(*jp, "player") == by) { cause = jp->get_string("cause").value_or("ORDER"); break; }
            }
            const std::string text = "pushes " + who + " " + xy(p, "from_x", "from_y") + " -> " + xy(p, "x", "y");
            if (cause == "CONVEYOR") { flush_pending(); lines.push_back("  belt: " + P(by) + " " + text); }
            else act(by, text);
        } else if (type == "MOVE_BLOCKED") {
            const bool belt = p.get_string("cause").value_or("") == "CONVEYOR";
            const std::string text = std::string(belt ? "" : "is blocked ") + "going " + dirn + " into " + xy(p, "x", "y");
            if (belt) { flush_pending(); lines.push_back("  belt cannot move " + who + " " + dirn + " into " + xy(p, "x", "y") + ": blocked"); }
            else act(pl, text);
        } else if (type == "CRATE_MOVED") {
            flush_pending();
            lines.push_back("  belt moves crate " + std::to_string(pint(p, "crate")) + " " + xy(p, "from_x", "from_y") + " -> " + xy(p, "x", "y"));
        } else if (type == "CRATE_PICKED_UP") {
            act(pl, "picks up crate " + std::to_string(pint(p, "crate")) + " at " + xy(p, "x", "y"));
        } else if (type == "CRATE_DELIVERED") {
            act(pl, "DELIVERED crate " + std::to_string(pint(p, "crate")) + " (score " + std::to_string(pint(p, "score")) + ")");
        } else if (type == "CRATE_DROPPED") {
            act(pl, "drops crate " + std::to_string(pint(p, "crate")) + " at " + xy(p, "x", "y"));
        } else if (type == "CRATE_DESTROYED") {
            plain("crate " + std::to_string(pint(p, "crate")) + " destroyed (" + p.get_string("reason").value_or("?") + ") at " + xy(p, "x", "y"));
        } else if (type == "LASER_HIT") {
            plain(who + " hit by a laser (damage " + std::to_string(pint(p, "damage")) + ")");
        } else if (type == "TRAP_ARMED") {
            act(pl, std::string(p.get_bool("was_armed").value_or(false) ? "takes over trap " : "arms trap ") + std::to_string(pint(p, "trap")));
        } else if (type == "TRAP_TRIGGERED") {
            std::string who_caught;
            if (const Json* v = p.find("victims")) for (const auto& x : v->items()) who_caught += (who_caught.empty() ? "" : ", ") + P(static_cast<int>(x.as_int()));
            const int owner = pint(p, "owner");
            plain("TRAP " + std::to_string(pint(p, "trap")) + " (" + (owner ? P(owner) : std::string("neutral")) + ") fires and catches " + who_caught);
        } else if (type == "TRAP_HIT") {
            plain("  " + who + " hit by the trap (damage " + std::to_string(pint(p, "damage")) + ")");
        } else if (type == "SCORE_AWARDED") {
            plain("  +" + std::to_string(pint(p, "points")) + " for " + who + ": " +
                  (p.get_string("reason").value_or("") == "TRAP_DISABLE" ? "disabling " : "damaging ") + P(pint(p, "victim")) +
                  " with trap " + std::to_string(pint(p, "trap")) + " (score " + std::to_string(pint(p, "score")) + ")");
        } else if (type == "ROBOT_DISABLED") {
            plain(who + " DISABLED (" + p.get_string("cause").value_or("?") + ")");
        } else if (type == "ROBOT_RESPAWNED") {
            plain(who + " respawned at " + xy(p, "x", "y"));
        } else if (type == "RESPAWN_REDIRECTED") {
            plain(who + " respawn redirected to " + xy(p, "x", "y"));
        } else if (type == "GROUP_TOGGLED") {
            act(pl, "toggles belt group " + p.get_string("group").value_or("?") +
                       (p.get_bool("reversed").value_or(false) ? " -> REVERSED" : " -> normal"));
        } else if (type == "CRATE_SPAWNED") {
            plain("crate " + std::to_string(pint(p, "crate")) + " appears at dispenser G" + std::to_string(pint(p, "dispenser")) + " " + xy(p, "x", "y"));
        }
    }
    flush_pending();
    return lines;
}

// ------------------------------------------------------------------ connection

Connection::~Connection() { close(); }

void Connection::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    buf_.clear();
}

bool Connection::open(const std::string& host, int port, std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        error = "cannot resolve " + host;
        return false;
    }
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bool ok = fd >= 0 && ::connect(fd, res->ai_addr, res->ai_addrlen) == 0;
    ::freeaddrinfo(res);
    if (!ok) {
        if (fd >= 0) ::close(fd);
        error = "cannot connect to " + host + ":" + std::to_string(port);
        return false;
    }
    fd_ = fd;
    return true;
}

std::optional<Json> Connection::request(const Json& req, std::string& error, int timeout_ms) {
    if (fd_ < 0) { error = "not connected"; return std::nullopt; }
    std::string line = req.dump() + "\n";
    for (std::size_t off = 0; off < line.size();) {
        ssize_t n = ::send(fd_, line.data() + off, line.size() - off, MSG_NOSIGNAL);
        if (n <= 0) { error = "connection lost (send)"; return std::nullopt; }
        off += static_cast<std::size_t>(n);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        auto nl = buf_.find('\n');
        if (nl != std::string::npos) {
            std::string resp = buf_.substr(0, nl);
            buf_.erase(0, nl + 1);
            auto j = Json::parse(resp);
            if (!j) { error = "server sent malformed JSON"; return std::nullopt; }
            return j;
        }
        if (buf_.size() > (8u << 20)) { error = "response too large"; return std::nullopt; }
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) { error = "timed out waiting for the server"; return std::nullopt; }
        pollfd p{fd_, POLLIN, 0};
        if (::poll(&p, 1, static_cast<int>(left)) <= 0) continue;
        char tmp[8192];
        ssize_t n = ::recv(fd_, tmp, sizeof tmp, 0);
        if (n <= 0) { error = "connection lost (server closed)"; return std::nullopt; }
        buf_.append(tmp, static_cast<std::size_t>(n));
    }
}

// ------------------------------------------------------------------ session

namespace {

Json make(const char* type) {
    Json j = Json::object();
    j.set("type", type);
    return j;
}

// Keyboard lines from fd 0 without blocking the refresh loop.
class Keyboard {
public:
    // Waits up to timeout_ms; returns false when stdin is closed and drained.
    bool wait(int timeout_ms, std::deque<std::string>& lines) {
        if (!lines.empty()) return true;
        if (eof_) { std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms)); return false; }
        pollfd p{0, POLLIN, 0};
        if (::poll(&p, 1, timeout_ms) > 0 && (p.revents & (POLLIN | POLLHUP))) {
            char tmp[512];
            ssize_t n = ::read(0, tmp, sizeof tmp);
            if (n <= 0) { eof_ = true; return false; }
            std::vector<server::LineBuffer::Line> got;
            lb_.feed(tmp, static_cast<std::size_t>(n), got);
            for (auto& l : got) lines.push_back(std::move(l.text));
        }
        return true;
    }
    bool eof() const { return eof_; }
private:
    server::LineBuffer lb_{512};
    bool eof_ = false;
};

std::string join_ids(const Json* arr) {
    std::string s;
    if (!arr) return s;
    for (const auto& v : arr->items()) s += (s.empty() ? "P" : " P") + std::to_string(v.as_int());
    return s;
}

}  // namespace

ClientResult run_client(const ClientOptions& opt, std::ostream& out) {
    ClientResult res;
    auto fail = [&](std::string msg) { res.ok = false; res.error = std::move(msg); return res; };
    Connection conn;
    std::string err;
    bool connected = false;
    for (int attempt = 0; attempt < 20 && !connected; ++attempt) {
        connected = conn.open(opt.host, opt.port, err);
        if (!connected) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!connected) return fail(err);
    auto call = [&](const Json& req, Json& resp) -> bool {
        auto r = conn.request(req, err);
        if (!r) return false;
        resp = std::move(*r);
        return true;
    };

    if (opt.list_maps) {
        Json lm = make("LIST_MAPS"), r;
        if (!call(lm, r)) return fail(err);
        const Json* maps = r.find("maps");
        if (!maps) return fail("server sent no map list");
        for (const auto& m : maps->items())
            out << m.get_string("map_id").value_or("?") << "  -  " << m.get_string("name").value_or("") << "  ("
                << m.get_int("min_players").value_or(0) << "-" << m.get_int("max_players").value_or(0) << " players)  "
                << m.get_string("description").value_or("") << "\n";
        res.ok = true;
        return res;
    }

    // ---- create / join ----
    Json resp;
    std::string match_id = opt.match_id;
    if (opt.create) {
        Json c = make("CREATE_MATCH");
        c.set("player_count", opt.players);
        if (opt.rounds > 0) c.set("rounds", opt.rounds);
        if (opt.target_score > 0) c.set("target_score", opt.target_score);
        c.set("planning_seconds", opt.planning_seconds);
        if (opt.bots > 0) c.set("bots", opt.bots);
        if (!opt.map_id.empty()) c.set("map_id", opt.map_id);
        if (!call(c, resp)) return fail(err);
        if (!resp.get_bool("ok").value_or(false)) return fail("cannot create match: " + resp.get_string("message").value_or("?"));
        match_id = *resp.get_string("match_id");
        out << "Match " << match_id << " created for " << opt.players << " players. Others join with:  botched-client --join "
            << match_id << " --host <this host> --port " << opt.port << "\n" << std::flush;
    }
    if (match_id.empty()) return fail("no match to join (use --create or --join ID)");
    Json j = make("JOIN_MATCH");
    j.set("match_id", match_id);
    if (!call(j, resp)) return fail(err);
    if (!resp.get_bool("ok").value_or(false)) return fail("cannot join " + match_id + ": " + resp.get_string("message").value_or(resp.get_string("code").value_or("?")));
    const int me = static_cast<int>(*resp.get_int("player_id"));
    const std::string session = *resp.get_string("session");
    res.player_id = me;
    res.match_id = match_id;
    out << "Joined " << match_id << " as player P" << me << ".\n" << std::flush;

    Keyboard kb;
    std::deque<std::string> typed;
    auto status = [&](Json& st) -> bool {
        Json q = make("GET_MATCH_STATUS");
        q.set("match_id", match_id);
        Json r;
        if (!call(q, r)) return false;
        const Json* s = r.find("status");
        if (!s) { err = "bad status response"; return false; }
        st = *s;
        return true;
    };

    // ---- lobby ----
    for (;;) {
        Json st;
        if (!status(st)) return fail(err);
        if (st.get_string("phase").value_or("") != "LOBBY") break;
        out << "Waiting for players: " << st.get_int("joined").value_or(0) << "/" << st.get_int("capacity").value_or(0)
            << (opt.bot ? "" : "   (type 'start' to begin with the players who have joined, 'quit' to leave)") << "\n" << std::flush;
        if (opt.bot) { std::this_thread::sleep_for(std::chrono::milliseconds(opt.refresh_ms)); continue; }
        kb.wait(opt.refresh_ms, typed);
        while (!typed.empty()) {
            std::string l = typed.front();
            typed.pop_front();
            std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return std::tolower(c); });
            if (l == "quit" || l == "q") return fail("left the lobby");
            if (l == "start") {
                Json s = make("START_MATCH");
                s.set("match_id", match_id);
                Json r;
                if (!call(s, r)) return fail(err);
                if (!r.get_bool("ok").value_or(false)) out << "Cannot start yet: " << r.get_string("message").value_or("?") << "\n";
            } else if (!l.empty()) {
                out << "The match has not started yet. Type 'start' to begin with the players who have joined, or 'quit'.\n" << std::flush;
            }
        }
    }

    // ---- map ----
    Json gm = make("GET_MAP");
    gm.set("match_id", match_id);
    if (!call(gm, resp)) return fail(err);
    const Json* mj = resp.find("map");
    MapLoad ml = mj ? map_from_json(*mj) : MapLoad{};
    if (!ml.map) return fail("could not read the map from the server");
    const Map& map = *ml.map;
    sim::Bot bot(me, map, opt.seed * 1000003ULL + static_cast<std::uint64_t>(me));

    // ---- game loop ----
    int last_round = 0, last_drawn_version = -1, events_version = -1, revision = 0;
    bool have_program = false, locked = false;
    std::array<std::string, 3> program{};
    std::deque<std::string> log;     // what happened recently
    std::string notice;              // last message to the player
    Json snap;
    auto fetch_snapshot = [&]() -> bool {
        Json q = make("GET_SNAPSHOT");
        q.set("match_id", match_id);
        Json r;
        if (!call(q, r)) return false;
        const Json* s = r.find("snapshot");
        if (!s) { err = "bad snapshot response"; return false; }
        snap = *s;
        return true;
    };
    auto submit = [&](bool ready) -> bool {
        ++revision;
        Json s = make("SUBMIT_PROGRAM");
        s.set("match_id", match_id);
        s.set("player_id", me);
        s.set("session", session);
        s.set("round", last_round);
        s.set("revision", revision);
        Json a = Json::array();
        for (const auto& o : program) a.push(o);
        s.set("orders", std::move(a));
        s.set("ready", ready);
        s.set("request_id", "p" + std::to_string(me) + "r" + std::to_string(last_round) + "v" + std::to_string(revision));
        Json r;
        if (!call(s, r)) return false;
        if (r.get_bool("ok").value_or(false)) {
            have_program = true;
            if (ready) locked = true;
            notice = ready ? "Locked in. Waiting for the other players..." : "Program saved. Type 'go' to lock it in, or enter new orders.";
        } else {
            const std::string code = r.get_string("code").value_or("?");
            if (code == "PLANNING_CLOSED" || code == "WRONG_ROUND") notice = "Too late: planning has closed for this round.";
            else if (code == "PROGRAM_LOCKED") notice = "Already locked in this round.";
            else notice = "Rejected: " + r.get_string("message").value_or(code);
            if (code == "PROGRAM_LOCKED") locked = true;
        }
        return true;
    };

    const sim::Style style{opt.unicode, opt.color};
    auto draw = [&](bool final_screen) {
        const std::string phase = snap.get_string("phase").value_or("?");
        const int round = static_cast<int>(snap.get_int("round").value_or(0));
        const int total = static_cast<int>(snap.get_int("rounds_total").value_or(0));
        std::ostringstream o;
        if (opt.tty) o << "\x1b[H\x1b[J";
        else o << "\n----------------------------------------------------------------\n";
        o << "=== BOTCHED!   match " << match_id << "   you are P" << me << " ===\n";
        if (final_screen) {
            o << "MATCH OVER after round " << round << " of " << total << "\n";
        } else {
            const int tgt = static_cast<int>(snap.get_int("target_score").value_or(0));
            if (tgt > 0) o << "Round " << round << " (cap " << total << ")   first to " << tgt << " pts   " << phase;
            else o << "Round " << round << "/" << total << "   " << phase;
            if (phase == "PLANNING") {
                const Json* dl = snap.find("planning_deadline_utc");
                if (dl && dl->is_string()) {
                    auto ms = parse_utc_ms(dl->as_string());
                    if (ms) {
                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        o << "   closes in about " << std::max<std::int64_t>(0, (*ms - now + 999) / 1000) << "s";
                    }
                }
                o << "\n   Ready: " << (join_ids(snap.find("ready_players")).empty() ? "nobody yet" : join_ids(snap.find("ready_players")));
            }
            o << "\n";
        }
        o << sim::render_board_styled(map, snap, style) << "\n" << sim::render_scores_styled(snap, style) << sim::render_legend(map, snap, style);
        if (const Json* ps = snap.find("players")) {
            for (const auto& p : ps->items()) {
                if (p.get_int("id").value_or(0) != me) continue;
                if (p.get_bool("respawn_pending").value_or(false)) { o << "You: disabled, respawning at the end of the round\n"; break; }
                o << "You: P" << me << " at (" << p.get_int("x").value_or(0) << "," << p.get_int("y").value_or(0) << ") facing "
                  << p.get_string("heading").value_or("?") << " on "
                  << sim::describe_tile(map, snap, {static_cast<int>(p.get_int("x").value_or(0)), static_cast<int>(p.get_int("y").value_or(0))}, me)
                  << ", damage " << p.get_int("damage").value_or(0) << "/2";
                const Json* c = p.find("carrying");
                if (c && !c->is_null()) o << ", carrying crate " << c->as_int();
                o << "\n";
            }
        }
        if (!log.empty()) {
            o << "-- what happened --\n";
            const std::size_t from = log.size() > 30 ? log.size() - 30 : 0;
            for (std::size_t i = from; i < log.size(); ++i) o << log[i] << "\n";
        }
        if (!final_screen) {
            if (have_program) {
                o << "Your orders: ";
                for (const auto& p : program) o << order_code(p) << " ";
                o << (locked ? "(locked)\n" : "(not locked yet)\n");
            }
            o << "Orders: " << order_help() << "\n";
            if (!notice.empty()) o << ">> " << notice << "\n";
        }
        out << o.str() << std::flush;
    };

    int guard = 0;
    for (;;) {
        if (!fetch_snapshot()) return fail(err);
        const std::string phase = snap.get_string("phase").value_or("?");
        const int round = static_cast<int>(snap.get_int("round").value_or(0));
        const int version = static_cast<int>(snap.get_int("state_version").value_or(0));

        if (events_version < 0) events_version = version;  // skip history before we arrived
        if (version != events_version) {
            Json q = make("GET_EVENTS");
            q.set("match_id", match_id);
            q.set("since_version", events_version);
            Json r;
            if (!call(q, r)) return fail(err);
            if (r.get_bool("ok").value_or(false))
                for (auto& l : summarize_events(*r.find("events"))) log.push_back(std::move(l));
            while (log.size() > 60) log.pop_front();
            events_version = version;
        }
        if (round != last_round) {
            last_round = round;
            revision = 0;
            have_program = locked = false;
            if (round > 1) notice = "New round: enter your three orders.";
            else notice = "Enter your three orders, e.g.  F2 R T   then 'go' to lock in.";
        }
        res.final_round = round;
        if (version != last_drawn_version || opt.tty) {
            if (phase == "FINISHED") {
                Json st;
                if (!status(st)) return fail(err);
                draw(true);
                const Json* w = st.find("winners");
                std::string names = join_ids(w);
                if (w) for (const auto& v : w->items()) res.winners.push_back(static_cast<int>(v.as_int()));
                out << "\n=== RESULT ===\n";
                if (res.winners.size() == 1) out << "Winner: " << names << (res.winners[0] == me ? "   -- that's you!" : "") << "\n";
                else out << "Shared victory: " << names << "\n";
                out << std::flush;
                res.ok = true;
                return res;
            }
            if (version != last_drawn_version) draw(false);
            last_drawn_version = version;
            if (opt.tty && !opt.bot) { /* tty redraw happens only on change to keep typing intact */ }
        }
        if (phase == "FINISHED") continue;  // handled above on the next pass if the version did not change
        if (++guard > 1000000) return fail("giving up");

        if (opt.bot) {
            if (phase == "PLANNING" && !locked) {
                if (opt.bot_think_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(opt.bot_think_ms));
                program = bot.plan(snap);
                if (!submit(true)) return fail(err);
                last_drawn_version = -1;  // show the "locked" state
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(opt.refresh_ms));
            continue;
        }

        // ---- human input ----
        bool redraw = false;
        kb.wait(opt.refresh_ms, typed);
        while (!typed.empty()) {
            std::string line = typed.front();
            typed.pop_front();
            std::string low = line;
            std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return std::tolower(c); });
            low.erase(0, low.find_first_not_of(" \t"));
            low.erase(low.find_last_not_of(" \t") == std::string::npos ? 0 : low.find_last_not_of(" \t") + 1);
            if (low.empty()) continue;
            if (low == "quit" || low == "q" || low == "exit") { out << "Leaving the match. Your robot will WAIT.\n"; return fail("left the match"); }
            if (low == "help" || low == "?") {
                notice = std::string("Type three orders then Enter, e.g.  F2 R T.  ") + order_help() + ".  'go' locks in, 'show' redraws, 'quit' leaves.";
                redraw = true;
            } else if (low == "show" || low == "redraw") {
                redraw = true;
            } else if (low == "go" || low == "ready" || low == "ok") {
                if (phase != "PLANNING") notice = "Planning is not open right now.";
                else if (!have_program) notice = "Enter your three orders first, e.g.  F2 R T";
                else if (locked) notice = "Already locked in.";
                else if (!submit(true)) return fail(err);
                redraw = true;
            } else {
                std::string perr;
                auto prog = parse_program(line, perr);
                if (!prog) notice = perr;
                else if (phase != "PLANNING") notice = "Planning is closed: wait for the next round.";
                else if (locked) notice = "Already locked in this round.";
                else {
                    program = *prog;
                    if (!submit(false)) return fail(err);
                }
                redraw = true;
            }
        }
        if (redraw) draw(false);
        if (kb.eof() && !locked && phase == "PLANNING" && !typed.empty()) typed.clear();
    }
}

}  // namespace botched::client

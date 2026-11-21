// M1-A: TCP host, terminal-client helpers, and a full match over real sockets.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <optional>
#include <sstream>
#include <thread>

#include <array>

#include "ascii_render.hpp"
#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include "client_core.hpp"
#include "doctest.h"
#include "map_fixture.hpp"
#include "line_buffer.hpp"
#include "server.hpp"
#include "tcp_host.hpp"

using namespace botched;
using namespace botched::server;
using namespace botched::client;

namespace {

Json req(const char* type, const std::string& match = "") {
    Json j = Json::object();
    j.set("type", type);
    if (!match.empty()) j.set("match_id", match);
    return j;
}
std::string code(const Json& j) { return j.get_string("code").value_or(""); }
bool ok(const Json& j) { return j.get_bool("ok").value_or(false); }

// Non-blocking raw client for driving a TcpHost from the same thread (deterministic, no races).
struct Raw {
    int fd = -1;
    std::string buf;
    explicit Raw(int port) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<std::uint16_t>(port));
        ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        REQUIRE(::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    }
    ~Raw() { if (fd >= 0) ::close(fd); }
    void send(const std::string& s) { REQUIRE(::send(fd, s.data(), s.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(s.size())); }
    // Pumps the host until one full line arrives (or gives up).
    std::optional<std::string> line(TcpHost& host, int iterations = 300) {
        for (int i = 0; i < iterations; ++i) {
            auto nl = buf.find('\n');
            if (nl != std::string::npos) {
                std::string l = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                return l;
            }
            host.run_once(2);
            char tmp[8192];
            ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
            if (n > 0) buf.append(tmp, static_cast<std::size_t>(n));
        }
        return std::nullopt;
    }
    Json ask(TcpHost& host, const Json& r) {
        send(r.dump() + "\n");
        auto l = line(host);
        REQUIRE(l.has_value());
        auto j = Json::parse(*l);
        REQUIRE(j.has_value());
        return *j;
    }
    // True once the host has closed this connection.
    bool closed_by_host(TcpHost& host) {
        for (int i = 0; i < 200; ++i) {
            host.run_once(2);
            char c;
            ssize_t n = ::recv(fd, &c, 1, 0);
            if (n == 0) return true;
        }
        return false;
    }
};

struct Harness {
    ManualClock clock;
    GameServer server;
    TcpHost host;
    static TcpHost::Options opts(int max_clients) {
        TcpHost::Options o;
        o.port = 0;
        o.max_clients = max_clients;
        return o;
    }
    explicit Harness(int max_clients = 32, ServerConfig cfg = {}) : server(cfg, clock), host(server, opts(max_clients)) {
        std::string err;
        REQUIRE_MESSAGE(host.listen(err), err);
    }
};

Json submit(const std::string& match, int p, const std::string& session, int round, int rev, bool ready) {
    Json s = req("SUBMIT_PROGRAM", match);
    s.set("player_id", p);
    s.set("session", session);
    s.set("round", round);
    s.set("revision", rev);
    Json a = Json::array();
    for (const char* o : {"FORWARD_1", "WAIT", "WAIT"}) a.push(o);
    s.set("orders", a);
    s.set("ready", ready);
    s.set("request_id", "t" + std::to_string(p) + "-" + std::to_string(round));
    return s;
}

}  // namespace

// ------------------------------------------------------------------ buffer and client helpers

TEST_CASE("M1-A line buffer: splits, reassembles, strips CR, bounds and resynchronises") {
    LineBuffer lb(10);
    std::vector<LineBuffer::Line> out;
    lb.feed("ab", 2, out);
    CHECK(out.empty());
    lb.feed("c\r\nxyz\n\nlast", 11, out);
    REQUIRE(out.size() == 3);
    CHECK(out[0].text == "abc");
    CHECK(out[1].text == "xyz");
    CHECK(out[2].text.empty());
    out.clear();
    std::string big(50, 'q');
    lb.feed(big.data(), big.size(), out);
    CHECK(out.empty());
    CHECK(lb.pending() == 0);  // nothing is retained for an over-long line
    lb.feed("\nok\n", 4, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0].too_long);
    CHECK(out[1].text == "ok");
}

TEST_CASE("M1-A client: order codes parse to the protocol tokens") {
    std::string err;
    auto p = parse_program("f2 r T", err);
    REQUIRE(p.has_value());
    CHECK((*p)[0] == "FORWARD_2");
    CHECK((*p)[1] == "TURN_RIGHT");
    CHECK((*p)[2] == "TOGGLE");
    CHECK(parse_program("F, F1, ff", err).has_value());
    CHECK((*parse_program("B L U", err))[2] == "TURN_AROUND");
    CHECK((*parse_program("drop WAIT forward_2", err))[2] == "FORWARD_2");
    for (const char* bad : {"", "F", "F F", "F F F F", "F X W", "F2R T", "FORWARD W W 1"}) {
        CHECK_FALSE(parse_program(bad, err).has_value());
        CHECK_FALSE(err.empty());
    }
    CHECK(order_code("FORWARD_2") == "F2");
    CHECK(order_code("TURN_AROUND") == "U");
}

TEST_CASE("M1-A client: server UTC timestamps parse back to the same instant") {
    for (std::int64_t ms : {INT64_C(0), INT64_C(951782400000), INT64_C(1795000000000), INT64_C(1795000999000)}) {
        auto parsed = parse_utc_ms(format_utc(ms));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == ms / 1000 * 1000);
    }
    CHECK_FALSE(parse_utc_ms("tomorrow").has_value());
    CHECK_FALSE(parse_utc_ms("2026-13-01T00:00:00Z").has_value());
}

TEST_CASE("M1-A client: events become readable lines, orders grouped per tick") {
    auto ev = Json::parse(
        "[{\"round\":2,\"tick\":1,\"type\":\"ORDER_EXECUTED\",\"params\":{\"player\":1,\"order\":\"FORWARD_2\"}},"
        "{\"round\":2,\"tick\":1,\"type\":\"CRATE_PICKED_UP\",\"params\":{\"player\":1,\"crate\":3,\"x\":4,\"y\":5}},"
        "{\"round\":2,\"tick\":1,\"type\":\"ORDER_NO_OP\",\"params\":{\"player\":2,\"order\":\"TOGGLE\",\"reason\":\"NO_CONSOLE\"}},"
        "{\"round\":2,\"tick\":2,\"type\":\"CRATE_DELIVERED\",\"params\":{\"player\":1,\"crate\":3,\"score\":5}},"
        "{\"round\":2,\"tick\":2,\"type\":\"ROBOT_DISABLED\",\"params\":{\"player\":2,\"cause\":\"PIT\"}}]");
    REQUIRE(ev.has_value());
    auto lines = summarize_events(*ev);
    auto has = [&](const std::string& needle) {
        for (const auto& l : lines) if (l.find(needle) != std::string::npos) return true;
        return false;
    };
    CHECK(has("-- round 2, tick 1 --"));
    CHECK(has("P1 F2: picks up crate 3 at (4,5)"));
    CHECK(has("P2 T: nothing happened (NO_CONSOLE)"));
    CHECK(has("P1 DELIVERED crate 3 (score 5)"));
    CHECK(has("P2 DISABLED (PIT)"));
}

namespace {
std::vector<std::string> summarize_round(Match& m, int before) {
    auto ev = m.events_since(before);
    Json arr = Json::array();
    for (const auto& e : *ev) arr.push(event_to_json(e, "m"));
    return summarize_events(arr);
}
int find_line(const std::vector<std::string>& lines, const std::string& needle) {
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (lines[i].find(needle) != std::string::npos) return static_cast<int>(i);
    return -1;
}
}  // namespace

TEST_CASE("M1-A UI: training-01 two-player belts and pushes are explained in server order") {
    MatchConfig c;
    c.match_id = "m";
    c.map = std::make_shared<const Map>(training01());
    c.player_count = 2;
    c.rounds = 6;
    Match m(c);
    int rid = 0;
    auto play = [&](std::array<const char*, 3> p1, std::array<const char*, 3> p2) {
        ++rid;
        m.submit_program(1, rid, 1, {p1[0], p1[1], p1[2]}, true, "a" + std::to_string(rid));
        m.submit_program(2, rid, 1, {p2[0], p2[1], p2[2]}, true, "b" + std::to_string(rid));
        int before = m.state_version();
        m.run_round();
        return summarize_round(m, before);
    };
    play({"TURN_RIGHT", "FORWARD_2", "TURN_LEFT"}, {"TURN_LEFT", "FORWARD_2", "TURN_RIGHT"});
    auto r2 = play({"FORWARD_2", "FORWARD_1", "WAIT"}, {"FORWARD_2", "FORWARD_1", "FORWARD_1"});
    // Round 2: P1 steps onto the central belt by order, then WAITs and is carried by the belt.
    CHECK(find_line(r2, "P1 F: drives (3,3) -> (4,3)") >= 0);
    CHECK(find_line(r2, "belt moves P1 (4,3) -> (5,3)") >= 0);
    CHECK(find_line(r2, "P1 W: waits") >= 0);
    CHECK(find_line(r2, "belt moves P1 (5,3) -> (6,3)   (they ordered WAIT)") >= 0);
    CHECK(find_line(r2, "P1 W: waits") < find_line(r2, "belt moves P1 (5,3) -> (6,3)"));  // orders precede belts

    auto r3 = play({"WAIT", "WAIT", "WAIT"}, {"FORWARD_1", "WAIT", "WAIT"});
    // Tick 1: P2 pushes P1 off the belt tile by order; then the belt phase pushes back.
    int order_push = find_line(r3, "P2 F: pushes P1 (6,3) -> (5,3)");
    int drives = find_line(r3, "P2 drives (7,3) -> (6,3)");
    int belt_push = find_line(r3, "belt: P1 pushes P2 (6,3) -> (7,3)");
    int belt_move = find_line(r3, "belt moves P1 (5,3) -> (6,3)   (they ordered WAIT)");
    REQUIRE(order_push >= 0);
    REQUIRE(drives >= 0);
    REQUIRE(belt_push >= 0);
    REQUIRE(belt_move >= 0);
    CHECK(order_push < drives);
    CHECK(drives < belt_push);
    CHECK(belt_push < belt_move);

    auto r4 = play({"FORWARD_2", "FORWARD_2", "FORWARD_2"}, {"WAIT", "WAIT", "WAIT"});
    CHECK(find_line(r4, "P1 F2: pushes P2 (8,3) -> (9,3)") >= 0);
    CHECK(find_line(r4, "P1 F2: is blocked going E into (10,3)") >= 0);
}

TEST_CASE("M1-A UI: rotations, toggles, blocked belts and belt crates have their own lines") {
    auto ev = Json::parse(
        "[{\"round\":1,\"tick\":1,\"type\":\"ORDER_EXECUTED\",\"params\":{\"player\":1,\"order\":\"TURN_RIGHT\"}},"
        "{\"round\":1,\"tick\":1,\"type\":\"ROBOT_ROTATED\",\"params\":{\"player\":1,\"from\":\"E\",\"to\":\"S\"}},"
        "{\"round\":1,\"tick\":1,\"type\":\"ORDER_EXECUTED\",\"params\":{\"player\":2,\"order\":\"TOGGLE\"}},"
        "{\"round\":1,\"tick\":1,\"type\":\"GROUP_TOGGLED\",\"params\":{\"player\":2,\"group\":\"A\",\"console\":\"T001\",\"reversed\":true}},"
        "{\"round\":1,\"tick\":1,\"type\":\"MOVE_BLOCKED\",\"params\":{\"player\":2,\"dir\":\"E\",\"x\":7,\"y\":3,\"cause\":\"CONVEYOR\"}},"
        "{\"round\":1,\"tick\":1,\"type\":\"CRATE_MOVED\",\"params\":{\"crate\":4,\"from_x\":4,\"from_y\":3,\"x\":5,\"y\":3}}]");
    REQUIRE(ev.has_value());
    auto lines = summarize_events(*ev);
    CHECK(find_line(lines, "P1 R: turns E -> S") >= 0);
    CHECK(find_line(lines, "P2 T: toggles belt group A -> REVERSED") >= 0);
    CHECK(find_line(lines, "belt cannot move P2 E into (7,3): blocked") >= 0);
    CHECK(find_line(lines, "belt moves crate 4 (4,3) -> (5,3)") >= 0);
}

// ------------------------------------------------------------------ host over real sockets (single-threaded)

TEST_CASE("M1-A host: a complete match is played over TCP by two connections") {
    Harness h;
    Raw a(h.host.port()), b(h.host.port());
    Json c = req("CREATE_MATCH");
    c.set("player_count", 2);
    c.set("rounds", 2);
    c.set("planning_seconds", 0);
    Json created = a.ask(h.host, c);
    REQUIRE(ok(created));
    const std::string id = *created.get_string("match_id");
    CHECK(code(a.ask(h.host, req("GET_SNAPSHOT", id))) == "NOT_STARTED");
    Json ja = a.ask(h.host, req("JOIN_MATCH", id));
    Json jb = b.ask(h.host, req("JOIN_MATCH", id));  // a different connection joins the same match
    REQUIRE(ok(ja));
    REQUIRE(ok(jb));
    CHECK(jb.get_bool("started").value());
    const std::string sa = *ja.get_string("session"), sb = *jb.get_string("session");
    for (int round = 1; round <= 2; ++round) {
        CHECK(ok(a.ask(h.host, submit(id, 1, sa, round, 1, true))));
        CHECK(ok(b.ask(h.host, submit(id, 2, sb, round, 1, true))));
    }
    Json st = b.ask(h.host, req("GET_MATCH_STATUS", id));
    CHECK(st.find("status")->get_string("phase") == "FINISHED");
    REQUIRE(st.find("status")->find("winners") != nullptr);
    CHECK_FALSE(st.find("status")->find("winners")->items().empty());
    // A session token from one connection is useless for another player's slot.
    CHECK(code(a.ask(h.host, submit(id, 2, sa, 3, 1, true))) == "UNAUTHORIZED");
}

TEST_CASE("M1-A host: planning deadlines advance with no incoming request") {
    Harness h;
    Raw a(h.host.port()), b(h.host.port());
    Json c = req("CREATE_MATCH");
    c.set("player_count", 2);
    c.set("planning_seconds", 30);
    const std::string id = *a.ask(h.host, c).get_string("match_id");
    a.ask(h.host, req("JOIN_MATCH", id));
    b.ask(h.host, req("JOIN_MATCH", id));
    REQUIRE(h.server.find_match(id)->state().round == 1);
    h.clock.advance_ms(31'000);
    h.host.run_once(0);  // the host loop alone: no client sends anything
    CHECK(h.server.find_match(id)->state().round == 2);
    h.clock.advance_ms(31'000);
    h.host.run_once(0);
    CHECK(h.server.find_match(id)->state().round == 3);
}

TEST_CASE("M1-A host: an oversize line is refused and the connection keeps working") {
    Harness h;
    Raw a(h.host.port());
    a.send(std::string(5000, 'a') + "\n" + req("GET_MATCH_STATUS", "m9").dump() + "\n");
    auto first = a.line(h.host);
    REQUIRE(first.has_value());
    CHECK(code(*Json::parse(*first)) == "REQUEST_TOO_LARGE");
    auto second = a.line(h.host);
    REQUIRE(second.has_value());
    CHECK(code(*Json::parse(*second)) == "UNKNOWN_MATCH");
}

TEST_CASE("M1-A host: pipelined requests are answered in order; split requests are reassembled") {
    Harness h;
    Raw a(h.host.port());
    a.send(req("GET_MATCH_STATUS", "m1").dump() + "\n" + req("GET_MAP", "m2").dump() + "\n");
    CHECK(code(*Json::parse(*a.line(h.host))) == "UNKNOWN_MATCH");
    CHECK(code(*Json::parse(*a.line(h.host))) == "UNKNOWN_MATCH");
    std::string r = req("CREATE_MATCH").dump() + "\n";
    a.send(r.substr(0, 7));
    for (int i = 0; i < 20; ++i) h.host.run_once(1);  // host has only half a request
    a.send(r.substr(7));
    auto resp = a.line(h.host);
    REQUIRE(resp.has_value());
    CHECK(ok(*Json::parse(*resp)));
}

TEST_CASE("M1-A host: a disconnect mid-request is harmless; the connection cap holds") {
    Harness h(2);
    {
        Raw gone(h.host.port());
        gone.send("{\"type\":\"CREATE_MA");  // never finished
        for (int i = 0; i < 20; ++i) h.host.run_once(1);
    }  // closes here
    for (int i = 0; i < 50 && h.host.client_count() > 0; ++i) h.host.run_once(2);
    CHECK(h.host.client_count() == 0);
    Raw a(h.host.port()), b(h.host.port()), over(h.host.port());
    CHECK(over.closed_by_host(h.host));  // third connection exceeds max_clients
    CHECK(ok(a.ask(h.host, req("CREATE_MATCH"))));
    CHECK(code(b.ask(h.host, req("GET_MAP", "zz"))) == "UNKNOWN_MATCH");
}

// ------------------------------------------------------------------ end to end: real host thread + client sessions

TEST_CASE("M1-A end to end: three auto-playing clients and one absent player finish a match over TCP") {
    SystemClock clock;
    GameServer server(ServerConfig{}, clock);
    TcpHost::Options o;
    o.port = 0;
    TcpHost host(server, o);
    std::string err;
    REQUIRE_MESSAGE(host.listen(err), err);
    std::atomic<bool> stop{false};
    std::thread host_thread([&] { host.run(stop, 20); });  // the only thread that touches `server`

    Connection admin;
    REQUIRE(admin.open("127.0.0.1", host.port(), err));
    Json c = req("CREATE_MATCH");
    c.set("player_count", 4);
    c.set("rounds", 2);
    c.set("planning_seconds", 1);
    auto created = admin.request(c, err);
    REQUIRE(created.has_value());
    const std::string id = *created->get_string("match_id");

    std::vector<ClientResult> results(3);
    std::vector<std::ostringstream> outs(3);
    std::vector<std::thread> clients;
    for (int i = 0; i < 3; ++i) {
        clients.emplace_back([&, i] {
            ClientOptions opt;
            opt.port = host.port();
            opt.match_id = id;
            opt.bot = true;
            opt.seed = static_cast<std::uint64_t>(i + 1);
            opt.refresh_ms = 20;
            results[static_cast<std::size_t>(i)] = run_client(opt, outs[static_cast<std::size_t>(i)]);
        });
    }
    // The fourth seat is taken by a player who never submits anything: the timer must carry the match.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto ghost = admin.request(req("JOIN_MATCH", id), err);
    REQUIRE(ghost.has_value());
    CHECK(ok(*ghost));
    for (auto& t : clients) t.join();

    for (const auto& r : results) {
        INFO(r.error);
        CHECK(r.ok);
        CHECK(r.final_round == 2);
        CHECK_FALSE(r.winners.empty());
        CHECK(r.winners == results[0].winners);  // everyone saw the same result
    }
    CHECK(outs[0].str().find("=== RESULT ===") != std::string::npos);
    CHECK(outs[0].str().find("MATCH OVER") != std::string::npos);

    Json ev = req("GET_EVENTS", id);
    ev.set("since_version", 0);
    auto events = admin.request(ev, err);
    REQUIRE(events.has_value());
    int ghost_timeouts = 0;
    for (const auto& e : events->find("events")->items()) {
        if (e.get_string("type") != "PLANNING_CLOSED") continue;
        for (const auto& m : e.find("params")->find("missing")->items()) if (m.as_int() == 4) ++ghost_timeouts;
    }
    CHECK(ghost_timeouts == 2);  // the absent player's programme was filled with WAITs in both rounds

    stop = true;
    host_thread.join();
}

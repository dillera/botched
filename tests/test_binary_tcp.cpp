#include "binary_tcp_host.hpp"
#include "binary_connection.hpp"
#include "client_core.hpp"
#include "tcp_host.hpp"
#include "doctest.h"
#include "botched/serialize.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <sstream>

using namespace botched;
using namespace botched::server;
using namespace botched::binary;
namespace {
Json query(const char *type, const std::string &match = "") {
    Json j = Json::object();
    j.set("type", type);
    if (!match.empty())
        j.set("match_id", match);
    return j;
}
Json field(Json j, const char *key, Json value) {
    j.set(key, std::move(value));
    return j;
}
Json program(const std::string &m, int player, const std::string &token, int round, int rev, bool ready,
             const std::string &key) {
    auto j = query("SUBMIT_PROGRAM", m);
    j.set("player_id", player);
    j.set("session", token);
    j.set("round", round);
    j.set("revision", rev);
    j.set("ready", ready);
    j.set("request_id", key);
    Json orders = Json::array();
    for (int i = 0; i < 3; ++i)
        orders.push("WAIT");
    j.set("orders", orders);
    return j;
}
struct AtomicClock : Clock {
    std::atomic<int64_t> now{1795000000000LL};
    int64_t now_unix_ms() const override { return now.load(); }
};
TcpHost::Options json_options() {
    TcpHost::Options o;
    o.port = 0;
    return o;
}
BinaryTcpHost::Options binary_options() {
    BinaryTcpHost::Options o;
    o.port = 0;
    return o;
}
ServerConfig configuration() {
    ServerConfig c;
    c.deterministic_token_seed = 42;
    return c;
}
struct Hosts {
    AtomicClock clock;
    GameServer server{configuration(), clock};
    TcpHost json{server, json_options()};
    BinaryTcpHost binary;
    std::atomic<bool> stop{false};
    std::thread worker;
    explicit Hosts(BinaryTcpHost::Options opts = binary_options()) : binary(server, clock, opts) {
        std::string error;
        REQUIRE(json.listen(error));
        REQUIRE(binary.listen(error));
    }
    void start() {
        worker = std::thread([&] {
            while (!stop) {
                json.run_once(1);
                binary.run_once(1);
            }
        });
    }
    ~Hosts() {
        stop = true;
        if (worker.joinable())
            worker.join();
    }
};
template <class C> Json call(C &c, const Json &request) {
    std::string error;
    auto r = c.request(request, error);
    REQUIRE_MESSAGE(r.has_value(), error);
    return *r;
}
int socket_to(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (sockaddr *)&a, sizeof a)) {
        ::close(fd);
        return -1;
    }
    return fd;
}
Bytes framed(const Bytes &payload) {
    Bytes b{uint8_t(payload.size()), uint8_t(payload.size() >> 8)};
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}
Bytes hello(const Bytes &secret = {}) {
    Fields f;
    f.b(1).b(1).b(0).h(128).b(3).i(63).b(!secret.empty());
    uint8_t zero[16]{};
    bw_write_bytes(&f.w, secret.empty() ? zero : secret.data(), 16);
    return packet(1, 0, f.span());
}
struct Raw {
    int fd;
    Bytes buffered;
    explicit Raw(int port) : fd(socket_to(port)) {
        REQUIRE(fd >= 0);
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    }
    ~Raw() {
        if (fd >= 0)
            ::close(fd);
    }
    void send_bytes(const Bytes &b) {
        REQUIRE(::send(fd, b.data(), b.size(), MSG_NOSIGNAL) == (ssize_t)b.size());
    }
    Bytes receive(BinaryTcpHost &host) {
        for (int i = 0; i < 3000; ++i) {
            if (buffered.size() >= 2) {
                size_t n = buffered[0] | unsigned(buffered[1]) << 8;
                if (buffered.size() >= n + 2) {
                    Bytes p(buffered.begin() + 2, buffered.begin() + 2 + n);
                    buffered.erase(buffered.begin(), buffered.begin() + 2 + n);
                    return p;
                }
            }
            host.run_once(1);
            uint8_t b[512];
            auto n = recv(fd, b, sizeof b, 0);
            if (n > 0)
                buffered.insert(buffered.end(), b, b + n);
        }
        FAIL("no binary frame");
        return {};
    }
    Bytes ask(BinaryTcpHost &host, const Bytes &p) {
        send_bytes(framed(p));
        return receive(host);
    }
    bool closed(BinaryTcpHost &host) {
        for (int i = 0; i < 200; ++i) {
            host.run_once(1);
            uint8_t b[512];
            auto n = recv(fd, b, sizeof b, 0);
            if (!n || (n < 0 && errno == ECONNRESET))
                return true;
        }
        return false;
    }
};
uint32_t u32(const Bytes &b, size_t off) {
    Reader r{std::span<const uint8_t>(b).subspan(off)};
    return r.i();
}
} // namespace

TEST_CASE("B3: split/coalesced frames bounded queues invalid lengths and EOF") {
    auto options = binary_options();
    options.write_budget = 1;
    options.incomplete_frame_ms = 15;
    options.max_output_bytes = 484;
    Hosts h(options);
    Raw raw(h.binary.port());
    auto frame = framed(hello());
    for (auto byte : frame) {
        raw.send_bytes(Bytes{byte});
        h.binary.run_once(0);
    }
    auto greeting = raw.receive(h.binary);
    REQUIRE(greeting[0] == 0x81);
    auto a = framed(Bytes{0x7f, 1, 0}), b = framed(Bytes{0x7f, 2, 0});
    a.insert(a.end(), b.begin(), b.end());
    raw.send_bytes(a);
    CHECK(raw.receive(h.binary) == Bytes{255, 1, 0, 2});
    CHECK(raw.receive(h.binary) == Bytes{255, 2, 0, 2});
    CHECK(h.binary.queued_bytes() <= 484);
    for (unsigned length : {0, 1, 2, 129, 241, 65535}) {
        Raw bad(h.binary.port());
        bad.ask(h.binary, hello());
        bad.send_bytes(Bytes{uint8_t(length), uint8_t(length >> 8)});
        CHECK(bad.closed(h.binary));
    }
    {
        Raw incomplete(h.binary.port());
        incomplete.send_bytes(Bytes{30});
        CHECK(incomplete.closed(h.binary));
    }
    {
        Raw gone(h.binary.port());
        gone.ask(h.binary, hello());
        auto p = framed(encode_request(query("CREATE_MATCH"), 1));
        p.resize(5);
        gone.send_bytes(p);
        h.binary.run_once(0);
    }
    CHECK(h.server.handle(query("CREATE_MATCH")).get_string("match_id") == "m1");
    // A flooding reader is disconnected before output exceeds its cap; no unbounded pending frames.
    Raw slow(h.binary.port());
    slow.ask(h.binary, hello());
    Bytes flood;
    for (int ex = 1; ex < 100; ++ex) {
        auto p = framed(Bytes{0x7f, uint8_t(ex), 0});
        flood.insert(flood.end(), p.begin(), p.end());
    }
    slow.send_bytes(flood);
    h.binary.run_once(0);
    CHECK(h.binary.queued_bytes() <= 484);
    CHECK(slow.closed(h.binary));
}

TEST_CASE("B3: lost CREATE JOIN and FETCH replies resume immutable exchanges over sockets") {
    Hosts h;
    Raw original(h.binary.port());
    auto greeting = original.ask(h.binary, hello());
    Bytes secret(greeting.begin() + 28, greeting.begin() + 44);
    auto create = encode_request(field(query("CREATE_MATCH"), "player_count", 2), 1);
    auto first = original.ask(h.binary, create);
    Raw resumed(h.binary.port());
    auto hi = resumed.ask(h.binary, hello(secret));
    CHECK(hi[44] == 1);
    CHECK(resumed.ask(h.binary, create) == first);
    CHECK(original.ask(h.binary, Bytes{0x10, 2, 0}) == Bytes{255, 2, 0, 3});
    auto join = encode_request(query("JOIN_MATCH", "m1"), 2);
    auto joined = resumed.ask(h.binary, join);
    Raw again(h.binary.port());
    again.ask(h.binary, hello(secret));
    CHECK(again.ask(h.binary, join) == joined);
    // Joining through JSON now must allocate P2, proving replay did not allocate a second seat.
    auto j = h.server.handle(query("JOIN_MATCH", "m1"));
    CHECK(j.get_int("player_id") == 2);
    auto expected = h.server.handle(query("GET_SNAPSHOT", "m1"));
    auto snap = again.ask(h.binary, encode_request(query("GET_SNAPSHOT", "m1"), 3));
    REQUIRE(!(snap[20] & 1));
    Fields f;
    f.i(u32(snap, 3)).i(snap[21]);
    auto fetch = packet(3, 4, f.span());
    auto second = again.ask(h.binary, fetch);
    auto token = j.get_string("session").value();
    h.server.handle(program("m1", 2, token, 1, 1, false, "draft"));
    Raw final(h.binary.port());
    final.ask(h.binary, hello(secret));
    CHECK(final.ask(h.binary, fetch) == second);
    auto changed = fetch;
    changed.back() ^= 1;
    CHECK(final.ask(h.binary, changed)[3] == 5);
    Bytes data(snap.begin() + 22, snap.end());
    data.insert(data.end(), second.begin() + 22, second.end());
    unsigned ex = 5;
    while (!(second[20] & 1)) {
        Fields next;
        next.i(u32(second, 3)).i(u32(second, 7) + second[21]);
        second = final.ask(h.binary, packet(3, ex++, next.span()));
        data.insert(data.end(), second.begin() + 22, second.end());
    }
    CHECK(decode_response(data, 0x15, u32(snap, 15)) == expected);
}

TEST_CASE("B3: all hazard maps finish with mixed binary JSON seats bots and H3 equivalence") {
    for (const char *map : {"training-01", "foundry-01", "laserworks-01", "trapworks-01", "turretworks-01",
                            "tarworks-01", "shuntworks-01", "crossroads-01", "factory-01"}) {
        CAPTURE(map);
        Hosts h;
        h.start();
        std::string error;
        client::Connection json;
        client::BinaryConnection binary(128);
        REQUIRE(json.open("127.0.0.1", h.json.port(), error));
        REQUIRE(binary.open("127.0.0.1", h.binary.port(), error));
        CHECK(call(binary, query("LIST_MAPS")) == call(json, query("LIST_MAPS")));
        auto create = query("CREATE_MATCH");
        create.set("map_id", map);
        create.set("player_count", 3);
        create.set("bots", 1);
        create.set("rounds", 2);
        create.set("planning_seconds", 0);
        if (std::string(map) != "training-01")
            create.set("target_score", 1000); // cap ending, baseline fixed ending
        auto m = call(binary, create).get_string("match_id").value();
        auto b = call(binary, query("JOIN_MATCH", m));
        auto j = call(json, query("JOIN_MATCH", m));
        CHECK(call(binary, query("GET_MAP", m)) == call(json, query("GET_MAP", m)));
        auto token = b.get_string("session").value();
        auto draft = program(m, 1, token, 1, 1, false, "b:7");
        auto accepted = call(json, draft);
        CHECK(call(binary, draft) == accepted);
        auto conflict = draft;
        conflict.set("revision", 2);
        CHECK(call(binary, conflict).get_string("code") == "REQUEST_ID_REUSED");
        auto initial = call(binary, field(query("GET_EVENTS", m), "since_version", 0));
        auto ack = binary.acknowledged_version();
        CHECK(ack > 0);
        for (int round = 1; round <= 2; ++round) {
            auto p = program(m, 1, token, round, 2, true, "bin:" + std::to_string(round));
            CHECK(call(binary, p).get_bool("ok") == true);
            CHECK(call(json, p) ==
                  call(binary, p)); // exact H3 across encodings, including replay after round closes below
            auto q = program(m, 2, j.get_string("session").value(), round, 1, true,
                             "json:" + std::to_string(round));
            CHECK(call(json, q).get_bool("ok") == true);
            CHECK(call(json, p) == call(binary, p));
            CHECK(call(binary, query("GET_SNAPSHOT", m)) == call(json, query("GET_SNAPSHOT", m)));
            CHECK(call(binary, query("GET_MATCH_STATUS", m)) == call(json, query("GET_MATCH_STATUS", m)));
        }
        auto status = call(json, query("GET_MATCH_STATUS", m));
        CHECK(status.find("status")->get_string("phase") == "FINISHED");
        Json events = Json::array();
        uint32_t since = 0;
        const auto want = call(json, field(query("GET_EVENTS", m), "since_version", 0));
        while (since < (uint32_t)want.get_int("state_version").value()) {
            auto q = field(field(query("GET_EVENTS", m), "since_version", (int)since), "max_versions", 1);
            auto window = call(binary, q);
            since = window.get_int("state_version").value();
            for (const auto &e : window.find("events")->items())
                events.push(e);
            CHECK(binary.acknowledged_version() >= ack);
            ack = binary.acknowledged_version();
        }
        CHECK(events == *want.find("events"));
        binary.close_socket();
        CHECK(call(binary, query("GET_MATCH_STATUS", m)) == status);
        CHECK(binary.acknowledged_version() == ack);
        call(binary, field(query("GET_EVENTS", m), "since_version", INT32_MAX));
        CHECK(binary.acknowledged_version() == ack);
    }
}

TEST_CASE("B3: planning advances while clients disconnect stop reading or send partial frames") {
    auto options = binary_options();
    options.write_budget = 1;
    Hosts h(options);
    auto create = query("CREATE_MATCH");
    create.set("player_count", 2);
    create.set("rounds", 2);
    create.set("planning_seconds", 1);
    auto m = h.server.handle(create).get_string("match_id").value();
    h.server.handle(query("JOIN_MATCH", m));
    h.server.handle(query("JOIN_MATCH", m));
    Raw slow(h.binary.port()), partial(h.binary.port());
    slow.ask(h.binary, hello());
    slow.send_bytes(framed(encode_request(query("GET_SNAPSHOT", m), 1)));
    partial.send_bytes(Bytes{30, 0, 1});
    {
        Raw disconnected(h.binary.port());
        disconnected.ask(h.binary, hello());
    }
    h.clock.now += 1001;
    h.binary.run_once(0);
    CHECK(h.server.find_match(m)->state().round == 2);
    h.clock.now += 1001;
    h.binary.run_once(0);
    CHECK(h.server.find_match(m)->finished());
    CHECK(h.binary.queued_bytes() <= options.max_output_bytes * options.max_clients);
}

TEST_CASE("B3: terminal binary and JSON clients play together through completion") {
    Hosts h;
    h.start();
    std::string error;
    client::Connection admin;
    REQUIRE(admin.open("127.0.0.1", h.json.port(), error));
    auto q = query("CREATE_MATCH");
    q.set("player_count", 2);
    q.set("rounds", 2);
    q.set("planning_seconds", 0);
    auto m = call(admin, q).get_string("match_id").value();
    client::ClientOptions a;
    a.match_id = m;
    a.port = h.binary.port();
    a.binary = true;
    a.bot = true;
    a.refresh_ms = 1;
    auto b = a;
    b.port = h.json.port();
    b.binary = false;
    std::ostringstream ao, bo;
    client::ClientResult ar, br;
    std::thread t([&] { ar = client::run_client(a, ao); });
    br = client::run_client(b, bo);
    t.join();
    CHECK_MESSAGE(ar.ok, ar.error);
    CHECK_MESSAGE(br.ok, br.error);
    CHECK(ar.winners == br.winners);
    CHECK(ar.final_round == 2);
}

namespace {
// Real TCP fault proxy. Drops a whole response after the upstream committed it, then accepts
// the client's reconnect. Every forwarded response is split into single-byte writes.
struct DropProxy {
    int listener = -1, port = 0, upstream;
    AtomicClock *clock = nullptr;
    std::atomic<int64_t> expire_ms{0};
    std::atomic<int> drop{0};
    std::atomic<bool> stop{false};
    std::atomic<unsigned> losses{0};
    std::thread worker;
    explicit DropProxy(int target, AtomicClock *timer = nullptr) : upstream(target), clock(timer) {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(::bind(listener, (sockaddr *)&a, sizeof a) == 0);
        REQUIRE(::listen(listener, 8) == 0);
        socklen_t n = sizeof a;
        getsockname(listener, (sockaddr *)&a, &n);
        port = ntohs(a.sin_port);
        worker = std::thread([&] { run(); });
    }
    ~DropProxy() {
        stop = true;
        worker.join();
        ::close(listener);
    }
    bool io(int fd, uint8_t *p, size_t n, bool write) {
        while (n && !stop) {
            pollfd item{fd, short(write ? POLLOUT : POLLIN), 0};
            if (poll(&item, 1, 20) <= 0)
                continue;
            auto count = write ? ::send(fd, p, n, MSG_NOSIGNAL) : ::recv(fd, p, n, 0);
            if (count <= 0)
                return false;
            p += count;
            n -= count;
        }
        return !n;
    }
    bool receive(int fd, Bytes &data) {
        uint8_t prefix[2];
        if (!io(fd, prefix, 2, false))
            return false;
        size_t n = prefix[0] | unsigned(prefix[1]) << 8;
        if (n > 240 || n < 3)
            return false;
        data = {prefix[0], prefix[1]};
        data.resize(n + 2);
        return io(fd, data.data() + 2, n, false);
    }
    void run() {
        while (!stop) {
            pollfd p{listener, POLLIN, 0};
            if (poll(&p, 1, 20) <= 0)
                continue;
            int client = accept(listener, nullptr, nullptr);
            if (client < 0)
                continue;
            int server = socket_to(upstream);
            if (server >= 0) {
                Bytes request, response;
                while (receive(client, request) && io(server, request.data(), request.size(), true) &&
                       receive(server, response)) {
                    int expected = request[2];
                    if (drop.compare_exchange_strong(expected, 0)) {
                        ++losses;
                        if (clock)
                            clock->now += expire_ms.exchange(0);
                        break;
                    }
                    bool sent = true;
                    for (auto &byte : response)
                        if (!io(client, &byte, 1, true)) {
                            sent = false;
                            break;
                        }
                    if (!sent)
                        break;
                }
                ::close(server);
            }
            ::close(client);
        }
    }
};
} // namespace
TEST_CASE("B3: client automatically recovers lost mutation and page responses with the same bytes") {
    Hosts h;
    h.start();
    DropProxy proxy(h.binary.port());
    std::string error;
    client::BinaryConnection c(128);
    REQUIRE(c.open("127.0.0.1", proxy.port, error));
    proxy.drop = 0x11;
    auto m = call(c, field(query("CREATE_MATCH"), "player_count", 2)).get_string("match_id").value();
    CHECK(m == "m1");
    proxy.drop = 0x12;
    auto seat = call(c, query("JOIN_MATCH", m));
    CHECK(seat.get_int("player_id") == 1);
    client::Connection json;
    REQUIRE(json.open("127.0.0.1", h.json.port(), error));
    auto other = call(json, query("JOIN_MATCH", m));
    CHECK(other.get_int("player_id") == 2);
    proxy.drop = 3;
    CHECK(call(c, query("GET_SNAPSHOT", m)) == call(json, query("GET_SNAPSHOT", m)));
    proxy.drop = 0x14;
    auto q = program(m, 1, seat.get_string("session").value(), 1, 1, false, "b:77");
    auto accepted = call(c, q);
    CHECK(accepted == call(json, q));
    CHECK(proxy.losses == 4);
    auto events = field(query("GET_EVENTS", m), "since_version", 0);
    call(c, events);
    auto ack = c.acknowledged_version();
    c.close_socket();
    call(c, events);
    CHECK(c.acknowledged_version() == ack);
}
TEST_CASE("B3: client reports ambiguous mutation after session lease loss without creating another room") {
    Hosts h;
    h.start();
    DropProxy proxy(h.binary.port(), &h.clock);
    std::string error;
    client::BinaryConnection c;
    REQUIRE(c.open("127.0.0.1", proxy.port, error));
    proxy.expire_ms = 1800001;
    proxy.drop = 0x11;
    auto response = c.request(query("CREATE_MATCH"), error);
    CHECK_FALSE(response);
    CHECK(error.find("uncertain") != std::string::npos);
    CHECK(proxy.losses == 1);
    client::Connection json;
    REQUIRE(json.open("127.0.0.1", h.json.port(), error));
    CHECK(call(json, query("GET_MATCH_STATUS", "m1")).get_bool("ok") == true);
    CHECK(call(json, query("CREATE_MATCH")).get_string("match_id") == "m2");
}
TEST_CASE("B3: target ending and multi-window playback use the same JSON semantics") {
    Hosts h;
    std::string text = "# # # # # # # #\n# S1E G1 D1 . . . #\n# . . . . . . #\n# . . . . . . #\n# . . . . . "
                       ". #\n# . . . . . . #\n# S2 D2 G2 . . . #\n# # # # # # # #\n";
    auto loaded = load_map("target-test", text, {2, true});
    REQUIRE(loaded.ok());
    MapInfo info;
    info.max_players = 2;
    REQUIRE(h.server.register_map(std::make_shared<Map>(*loaded.map), info));
    h.start();
    std::string error;
    client::Connection binary(true), json;
    REQUIRE(binary.open("127.0.0.1", h.binary.port(), error));
    REQUIRE(json.open("127.0.0.1", h.json.port(), error));
    auto create = query("CREATE_MATCH");
    create.set("player_count", 2);
    create.set("target_score", 5);
    create.set("rounds", 10);
    create.set("map_id", "target-test");
    create.set("planning_seconds", 0);
    auto m = call(binary, create).get_string("match_id").value();
    auto b = call(binary, query("JOIN_MATCH", m));
    auto j = call(json, query("JOIN_MATCH", m));
    // Eventless versions force more than one wire window before the delivery and MATCH_ENDED.
    for (int rev = 1; rev <= 40; ++rev)
        CHECK(
            call(binary, program(m, 1, b.get_string("session").value(), 1, rev, false, "")).get_bool("ok") ==
            true);
    auto deliver = program(m, 1, b.get_string("session").value(), 1, 41, true, "delivery");
    Json orders = Json::array();
    orders.push("FORWARD_2");
    orders.push("WAIT");
    orders.push("WAIT");
    deliver.set("orders", orders);
    CHECK(call(binary, deliver).get_bool("ok") == true);
    CHECK(call(json, program(m, 2, j.get_string("session").value(), 1, 1, true, "wait")).get_bool("ok") ==
          true);
    CHECK(call(binary, query("GET_SNAPSHOT", m)) == call(json, query("GET_SNAPSHOT", m)));
    auto events = call(binary, field(query("GET_EVENTS", m), "since_version", 0));
    CHECK(events == call(json, field(query("GET_EVENTS", m), "since_version", 0)));
    CHECK(events.find("events")->items().back().find("params")->get_string("reason") == "target");
    CHECK(call(binary, query("GET_MATCH_STATUS", m)).find("status")->get_string("phase") == "FINISHED");
}

TEST_CASE("B3: connection cap rejects excess clients without blocking other service") {
    auto options = binary_options();
    options.max_clients = 2;
    Hosts h(options);
    Raw one(h.binary.port()), two(h.binary.port()), extra(h.binary.port());
    CHECK(extra.closed(h.binary));
    CHECK(h.binary.client_count() == 2);
    CHECK(one.ask(h.binary, hello())[0] == 0x81);
    CHECK(two.ask(h.binary, hello())[0] == 0x81);
}
TEST_CASE("B3: authenticated rebinding and explicit lobby start use existing seats") {
    Hosts h;
    h.start();
    std::string error;
    client::Connection json;
    client::BinaryConnection binary;
    REQUIRE(json.open("127.0.0.1", h.json.port(), error));
    REQUIRE(binary.open("127.0.0.1", h.binary.port(), error));
    auto m = call(json, field(field(query("CREATE_MATCH"), "player_count", 3), "planning_seconds", 0))
                 .get_string("match_id")
                 .value();
    auto a = call(json, query("JOIN_MATCH", m));
    call(json, query("JOIN_MATCH", m));
    REQUIRE(binary.bind(m, 1, a.get_string("session").value(), error));
    CHECK_FALSE(binary.bind(m, 2, "wrong", error));
    CHECK(call(binary, query("GET_MATCH_STATUS", m)).find("status")->get_int("joined") == 2);
    CHECK(call(binary, query("START_MATCH", m)).get_bool("ok") == true);
    auto p = program(m, 1, a.get_string("session").value(), 1, 1, false, "b:8");
    auto answer = call(binary, p);
    CHECK(answer.get_bool("ok") == true);
    CHECK(answer == call(json, p));
}

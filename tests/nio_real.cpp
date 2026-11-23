// Opt-in executable: requires real configured fujinet-nio-lib channel (FN_PORT on Linux).
#include "nio_peer.hpp"
#include "botched/client/nio_fujinet.h"
#include "fujinet-nio.h"
#include "binary_tcp_host.hpp"
#include "binary_connection.hpp"
#include "tcp_host.hpp"
#include "client_core.hpp"
#include <atomic>
#include <iostream>
using namespace botched;
using namespace botched::test;
Json q(const char *type, const std::string &m = "") {
    auto j = Json::object();
    j.set("type", type);
    if (!m.empty())
        j.set("match_id", m);
    return j;
}
Json with(Json j, const char *name, Json value) {
    j.set(name, value);
    return j;
}
template <class C> Json call(C &c, const Json &j) {
    std::string error;
    auto r = c.request(j, error);
    ensure(r.has_value(), error);
    return *r;
}
Json program(const std::string &m, int player, const std::string &token, int round, bool ready,
             int revision = 1) {
    auto j = q("SUBMIT_PROGRAM", m);
    j.set("player_id", player);
    j.set("session", token);
    j.set("round", round);
    j.set("revision", revision);
    j.set("ready", ready);
    j.set("request_id", "b:" + std::to_string(round * 10 + revision));
    auto a = Json::array();
    a.push("WAIT");
    a.push("WAIT");
    a.push("WAIT");
    j.set("orders", a);
    return j;
}

namespace {
unsigned real_not_ready = 0;
struct NioClock : server::Clock {
    std::atomic<int64_t> now{1795000000000LL};
    int64_t now_unix_ms() const override { return now.load(); }
};
uint8_t probe_info(void *ctx, uint16_t handle, uint8_t *flags) {
    auto rc = bn_fujinet_ops.info(ctx, handle, flags);
    if (rc == BN_IO_OK) {
        uint8_t byte = 0, read_flags = 0;
        uint16_t count = 0;
        auto result = fn_read(handle, 0, &byte, 1, &count, &read_flags);
        if (result != FN_ERR_NOT_READY || count != 0)
            return BN_IO_ERROR;
        ++real_not_ready;
    }
    return rc;
}
} // namespace
int main() {
    try {
        ensure(fn_init() == FN_OK, "fn_init: configure FN_PORT to the real NIO channel");
        NioClock clock;
        server::ServerConfig cfg;
        cfg.deterministic_token_seed = 42;
        server::GameServer server(cfg, clock);
        auto target_map =
            load_map("target-nio",
                     "# # # # # # # #\n# S1E G1 D1 . . . #\n# . . . . . . #\n# . . . . . . #\n# . . . . . . "
                     "#\n# . . . . . . #\n# S2 D2 G2 . . . #\n# # # # # # # #\n",
                     {2, true});
        ensure(target_map.ok(), "target fixture");
        server::MapInfo target_info;
        target_info.max_players = 2;
        ensure(server.register_map(std::make_shared<Map>(*target_map.map), target_info),
               "register target fixture");
        server::BinaryTcpHost::Options bo;
        bo.port = 0;
        server::BinaryTcpHost binary(server, clock, bo);
        server::TcpHost::Options jo;
        jo.port = 0;
        server::TcpHost json(server, jo);
        std::string error;
        ensure(binary.listen(error), error);
        ensure(json.listen(error), error);
        std::atomic<bool> stop{false};
        struct Guard {
            std::atomic<bool> &stop;
            std::thread worker;
            ~Guard() {
                stop = true;
                worker.join();
            }
        } guard{stop, std::thread([&] {
                    while (!stop) {
                        binary.run_once(1);
                        json.run_once(1);
                    }
                })};
        bn_ops real_ops = bn_fujinet_ops;
        real_ops.info = probe_info; // actual Read before any game request must be NotReady
        size_t pages = 0, bytes = 0, max_payload = 0;
        for (const char *map :
             {"training-01", "foundry-01", "laserworks-01", "trapworks-01", "turretworks-01", "tarworks-01",
              "shuntworks-01", "crossroads-01", "factory-01"}) {
            NioPeer nio(&real_ops, nullptr, "tcp://127.0.0.1:" + std::to_string(binary.port()), 128, 2);
            client::BinaryConnection direct(128);
            client::Connection oracle;
            ensure(direct.open("127.0.0.1", binary.port(), error), error);
            ensure(oracle.open("127.0.0.1", json.port(), error), error);
            ensure(nio.call(q("LIST_MAPS")) == call(direct, q("LIST_MAPS")), "map list equivalence");
            auto create = q("CREATE_MATCH");
            create.set("map_id", map);
            create.set("player_count", 3);
            create.set("bots", 1);
            create.set("rounds", 2);
            create.set("planning_seconds", 0);
            if (std::string(map) != "training-01")
                create.set("target_score", 1000);
            auto m = nio.call(create).get_string("match_id").value();
            auto seat = nio.call(q("JOIN_MATCH", m));
            auto peer = call(direct, q("JOIN_MATCH", m));
            ensure(nio.call(q("GET_MAP", m)) == call(direct, q("GET_MAP", m)), "map equivalence");
            auto draft = program(m, 1, seat.get_string("session").value(), 1, false);
            auto accepted = nio.call(draft);
            auto expected_snapshot = call(oracle, q("GET_SNAPSHOT", m));
            auto first_page = nio.exchange(binary::encode_request(q("GET_SNAPSHOT", m), nio.c.next_exchange));
            auto other_draft = program(m, 2, peer.get_string("session").value(), 1, false);
            other_draft.set("request_id", "other-draft");
            ensure(call(oracle, other_draft).get_bool("ok") == true, "concurrent JSON draft");
            ensure(nio.collect(first_page) == expected_snapshot, "immutable NIO snapshot pages");

            ensure(call(oracle, draft) == accepted, "NIO -> JSON H3");
            ensure(nio.call(draft) == accepted, "NIO retry H3");
            ensure(bn_reconnect(&nio.c, nio_now()), "reconnect");
            nio.pump(BN_READY);
            for (int round = 1; round <= 2; ++round) {
                auto p = program(m, 1, seat.get_string("session").value(), round, true, 2);
                auto answer = call(oracle, p);
                ensure(nio.call(p) == answer, "JSON -> NIO H3");
                ensure(call(direct, program(m, 2, peer.get_string("session").value(), round, true, 2))
                               .get_bool("ok") == true,
                       "direct ready");
                ensure(nio.call(p) == answer, "NIO replay after close");
                ensure(nio.call(q("GET_SNAPSHOT", m)) == call(direct, q("GET_SNAPSHOT", m)),
                       "snapshot equivalence");
                ensure(nio.call(q("GET_MATCH_STATUS", m)) == call(direct, q("GET_MATCH_STATUS", m)),
                       "status equivalence");
            }
            auto want = call(oracle, with(q("GET_EVENTS", m), "since_version", 0));
            auto events = Json::array();
            uint32_t since = 0, ack = 0;
            while (since < (uint32_t)want.get_int("state_version").value()) {
                auto window =
                    nio.call(with(with(q("GET_EVENTS", m), "since_version", (int)since), "max_versions", 1));
                since = window.get_int("state_version").value();
                for (const auto &event : window.find("events")->items())
                    events.push(event);
                ensure(nio.c.stream.ack_version >= ack, "watermark regressed");
                ack = nio.c.stream.ack_version;
            }
            ensure(events == *want.find("events"), "event sequence equivalence");
            ensure(bn_reconnect(&nio.c, nio_now()), "reconnect with acknowledged events");
            nio.pump(BN_READY);
            nio.call(with(q("GET_EVENTS", m), "since_version", INT32_MAX));
            ensure(nio.c.stream.ack_version == ack, "future cursor regression");
            ensure(nio.call(q("GET_MATCH_STATUS", m)).find("status")->get_string("phase") == "FINISHED",
                   "match completion");
            pages += nio.pages;
            bytes += nio.bytes;
            max_payload = std::max(max_payload, nio.max_payload);
            std::cout << map << " NIO/direct/JSON equivalent, pages=" << nio.pages
                      << " tx=" << nio.c.write_cursor << " rx=" << nio.c.read_cursor << '\n'
                      << std::flush;
            ensure(bn_dispose(&nio.c), "NIO close");
        }

        {
            NioPeer nio(&real_ops, nullptr, "tcp://127.0.0.1:" + std::to_string(binary.port()), 128, 2);
            client::BinaryConnection direct(128);
            client::Connection oracle;
            ensure(direct.open("127.0.0.1", binary.port(), error), error);
            ensure(oracle.open("127.0.0.1", json.port(), error), error);
            auto create = q("CREATE_MATCH");
            create.set("map_id", "target-nio");
            create.set("player_count", 2);
            create.set("target_score", 5);
            create.set("rounds", 10);
            create.set("planning_seconds", 0);
            auto m = nio.call(create).get_string("match_id").value();
            auto a = nio.call(q("JOIN_MATCH", m));
            auto b = call(direct, q("JOIN_MATCH", m));
            for (int revision = 1; revision <= 40; ++revision) {
                auto draft = program(m, 1, a.get_string("session").value(), 1, false, revision);
                draft.set("request_id", "");
                ensure(nio.call(draft).get_bool("ok") == true, "eventless draft");
            }
            auto deliver = program(m, 1, a.get_string("session").value(), 1, true, 41);
            auto orders = Json::array();
            orders.push("FORWARD_2");
            orders.push("WAIT");
            orders.push("WAIT");
            deliver.set("orders", orders);
            ensure(nio.call(deliver).get_bool("ok") == true, "delivery");
            ensure(call(direct, program(m, 2, b.get_string("session").value(), 1, true)).get_bool("ok") ==
                       true,
                   "target peer");
            auto want = call(oracle, with(q("GET_EVENTS", m), "since_version", 0));
            auto events = Json::array();
            uint32_t since = 0;
            while (since < (uint32_t)want.get_int("state_version").value()) {
                auto window = nio.call(with(q("GET_EVENTS", m), "since_version", (int)since));
                since = window.get_int("state_version").value();
                for (const auto &e : window.find("events")->items())
                    events.push(e);
            }
            ensure(events == *want.find("events"), "target/eventless window equivalence");
            ensure(events.items().back().find("params")->get_string("reason") == "target", "target ending");
            ensure(nio.call(q("GET_SNAPSHOT", m)) == call(direct, q("GET_SNAPSHOT", m)), "target snapshot");
            ensure(nio.call(q("GET_MATCH_STATUS", m)).find("status")->get_string("phase") == "FINISHED",
                   "target finished");
            std::cout << "target-nio: 40 eventless versions and target ending equivalent\n";
            pages += nio.pages;
            bytes += nio.bytes;
            max_payload = std::max(max_payload, nio.max_payload);
            ensure(bn_dispose(&nio.c), "target close");
        }

        {
            NioPeer nio(&real_ops, nullptr, "tcp://127.0.0.1:" + std::to_string(binary.port()), 128, 2);
            client::BinaryConnection direct(128);
            ensure(direct.open("127.0.0.1", binary.port(), error), error);
            auto create = q("CREATE_MATCH");
            create.set("player_count", 2);
            create.set("rounds", 2);
            create.set("planning_seconds", 1);
            auto m = nio.call(create).get_string("match_id").value();
            nio.call(q("JOIN_MATCH", m));
            call(direct, q("JOIN_MATCH", m));
            ensure(bn_dispose(&nio.c), "deadline disconnect");
            clock.now += 1001;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            ensure(call(direct, q("GET_SNAPSHOT", m)).find("snapshot")->get_int("round") == 2,
                   "deadline round 2");
            clock.now += 1001;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            ensure(bn_reconnect(&nio.c, nio_now()), "deadline resume");
            nio.pump(BN_READY);
            auto status = nio.call(q("GET_MATCH_STATUS", m));
            ensure(status == call(direct, q("GET_MATCH_STATUS", m)), "deadline status equivalence");
            ensure(status.find("status")->get_string("phase") == "FINISHED",
                   "deadline finish with disconnected NIO client");
            std::cout << "NIO disconnect across two planning deadlines: resumed finished seat\n";
            pages += nio.pages;
            bytes += nio.bytes;
            max_payload = std::max(max_payload, nio.max_payload);
            ensure(bn_dispose(&nio.c), "deadline close");
        }
        std::cout << "REAL NIO PASS (P128/K4): 9 shipped maps + target/deadline scenarios, " << pages << " pages, "
                  << bytes << " payload bytes, largest=" << max_payload
                  << " sizeof(bn_client)=" << sizeof(bn_client) << '\n';
        std::cout << "Actual NIO NotReady observations=" << real_not_ready << '\n';
        fn_shutdown();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "NIO acceptance failed: " << e.what() << '\n';
        fn_shutdown();
        return 1;
    }
}

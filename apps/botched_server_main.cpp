// Headless server host: one JSON request per line on stdin, one JSON response per line on
// stdout. This is the transport-neutral harness; a FujiNet-NIO channel adapter replaces
// the stdio loop later without touching GameServer.
//
//   {"type":"CREATE_MATCH","player_count":2,"planning_seconds":60}
//   {"type":"JOIN_MATCH","match_id":"m1"}   (repeat per player; the last join starts the match)
//   {"type":"SUBMIT_PROGRAM","match_id":"m1","player_id":1,"session":"...","round":1,
//    "revision":1,"orders":["FORWARD_2","TURN_RIGHT","TOGGLE"],"ready":true,"request_id":"c-1"}
#include <cstdlib>
#include <iostream>
#include <string>

#include "line_reader.hpp"
#include "server.hpp"

int main(int argc, char** argv) {
    botched::server::ServerConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--planning-seconds" && i + 1 < argc) {
            cfg.default_planning_seconds = std::atoi(argv[++i]);
        } else if (a == "--bot-log") {
            cfg.bot_log = &std::cerr;
        } else if (a == "--maps-dir" && i + 1 < argc) {
            cfg.maps_dir = argv[++i];
        } else {
            std::cerr << "usage: botched-server [--planning-seconds N] [--maps-dir DIR] [--bot-log]   (JSON lines on stdin/stdout)\n";
            return 2;
        }
    }
    botched::server::SystemClock clock;
    botched::server::GameServer srv(cfg, clock);
    print_map_report(srv.map_report(), std::cerr);
    if (srv.list_maps().empty()) {
        std::cerr << "botched-server: no usable maps (use --maps-dir DIR or set BOTCHED_MAPS_DIR)\n";
        return 1;
    }
    std::string line;
    for (;;) {
        // One request per line, read with a hard size bound (kMaxRequestBytes + room for CRLF).
        auto st = botched::server::read_line_bounded(std::cin, line, botched::server::kMaxRequestBytes + 2);
        if (st == botched::server::LineStatus::Eof) break;
        if (st == botched::server::LineStatus::TooLong) {
            std::cout << botched::server::make_error("REQUEST_TOO_LARGE", "request line is too long").dump() << '\n'
                      << std::flush;
            continue;
        }
        if (line.empty()) continue;
        std::cout << srv.handle_text(line).dump() << '\n' << std::flush;
    }
    return 0;
}

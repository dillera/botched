// botched-tcp-server: the Botched! game server on a TCP port (JSON lines, single thread).
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include "tcp_host.hpp"

namespace {
std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }
}  // namespace

int main(int argc, char** argv) {
    using namespace botched::server;
    ServerConfig cfg;
    TcpHost::Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) std::exit(2);
            return argv[++i];
        };
        if (a == "--port") opt.port = std::atoi(next());
        else if (a == "--bind") opt.bind_address = next();
        else if (a == "--planning-seconds") cfg.default_planning_seconds = std::atoi(next());
        else if (a == "--max-matches") cfg.max_matches = std::atoi(next());
        else if (a == "--maps-dir") cfg.maps_dir = next();
        else if (a == "--bot-log") cfg.bot_log = &std::cerr;
        else {
            std::cerr << "usage: botched-tcp-server [--port N] [--bind ADDR] [--planning-seconds N] [--max-matches N] [--maps-dir DIR] [--bot-log]\n"
                         "  default: 127.0.0.1:7878, 60 s planning. Use --bind 0.0.0.0 for LAN play (no authentication!).\n";
            return 2;
        }
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);

    SystemClock clock;
    GameServer server(cfg, clock);
    print_map_report(server.map_report(), std::cerr);
    if (server.list_maps().empty()) {
        std::cerr << "botched-tcp-server: no usable maps (use --maps-dir DIR or set BOTCHED_MAPS_DIR)\n";
        return 1;
    }
    TcpHost host(server, opt);
    std::string err;
    if (!host.listen(err)) {
        std::cerr << "botched-tcp-server: " << err << '\n';
        return 1;
    }
    std::cout << "botched-tcp-server listening on " << opt.bind_address << ":" << host.port() << " (planning "
              << cfg.default_planning_seconds << " s per round). Ctrl-C to stop.\n" << std::flush;
    host.run(g_stop);
    std::cout << "stopped\n";
    return 0;
}

// botched-tcp-server: the Botched! game server on a TCP port (JSON lines, single thread).
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "tcp_host.hpp"
#include "binary_tcp_host.hpp"
#include <memory>

namespace {
std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }
}  // namespace

int main(int argc, char** argv) {
    using namespace botched::server;
    ServerConfig cfg;
    TcpHost::Options opt;
    int binary_port = -1;
    std::ofstream trace_file;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) std::exit(2);
            return argv[++i];
        };
        if (a == "--port") opt.port = std::atoi(next());
        else if (a == "--binary-port") binary_port = std::atoi(next());
        else if (a == "--bind") opt.bind_address = next();
        else if (a == "--planning-seconds") cfg.default_planning_seconds = std::atoi(next());
        else if (a == "--max-matches") cfg.max_matches = std::atoi(next());
        else if (a == "--maps-dir") cfg.maps_dir = next();
        else if (a == "--bot-log") cfg.bot_log = &std::cerr;
        else if (a == "--trace") cfg.trace = &std::cerr;
        else if (a == "--trace-file") {
            trace_file.open(next(), std::ios::out | std::ios::trunc);
            if (!trace_file) { std::cerr << "botched-tcp-server: cannot open trace file\n"; return 2; }
            cfg.trace = &trace_file;
        }
        else {
            std::cerr << "usage: botched-tcp-server [--port N] [--binary-port N] [--bind ADDR] [--planning-seconds N] [--max-matches N] [--maps-dir DIR] [--bot-log] [--trace | --trace-file PATH]\n"
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
    std::unique_ptr<BinaryTcpHost> binary;
    if (binary_port >= 0) {
        BinaryTcpHost::Options options;
        options.port = binary_port; options.bind_address = opt.bind_address;
        if(binary_port > 65535) { std::cerr << "invalid binary port\n"; return 2; }
        binary = std::make_unique<BinaryTcpHost>(server, clock, options);
        if(!binary->listen(err)) { std::cerr << "binary listener: " << err << '\n'; return 1; }
        std::cout << "binary wire v1 listening on " << opt.bind_address << ':' << binary->port() << '\n' << std::flush;
    }
    if (!binary) host.run(g_stop);
    else while (!g_stop.load()) {
        host.run_once(10);
        binary->run_once(10);
    }
    std::cout << "stopped\n";
    return 0;
}

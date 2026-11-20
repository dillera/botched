// botched-client: play Botched! in a terminal against a botched-tcp-server.
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "client_core.hpp"

namespace {
void usage() {
    std::cerr <<
        "usage: botched-client (--create | --join MATCH_ID) [options]\n"
        "  --host H              server host (default 127.0.0.1)\n"
        "  --port N              server port (default 7878)\n"
        "  --create              create a match and join it as player 1\n"
        "    --players N         seats, 2-4 (default 2)\n"
        "    --rounds N          rounds (default 12)\n"
        "    --planning-seconds N  planning time per round (default 60)\n"
        "  --join MATCH_ID       join a match created by someone else\n"
        "  --bot                 auto-play instead of using the keyboard (testing/demos)\n"
        "    --seed N            bot seed          --think-ms N   bot pause before submitting\n"
        "  --plain               no ANSI screen control (default when output is not a terminal)\n";
}
}  // namespace

int main(int argc, char** argv) {
    botched::client::ClientOptions opt;
    opt.tty = isatty(1) != 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            return argv[++i];
        };
        if (a == "--host") opt.host = next();
        else if (a == "--port") opt.port = std::atoi(next());
        else if (a == "--create") opt.create = true;
        else if (a == "--join") opt.match_id = next();
        else if (a == "--players") opt.players = std::atoi(next());
        else if (a == "--rounds") opt.rounds = std::atoi(next());
        else if (a == "--planning-seconds") opt.planning_seconds = std::atoi(next());
        else if (a == "--bot") opt.bot = true;
        else if (a == "--seed") opt.seed = std::strtoull(next(), nullptr, 10);
        else if (a == "--think-ms") opt.bot_think_ms = std::atoi(next());
        else if (a == "--plain") opt.tty = false;
        else { usage(); return 2; }
    }
    if (!opt.create && opt.match_id.empty()) { usage(); return 2; }
    if (opt.bot) opt.refresh_ms = 200;
    botched::client::ClientResult r = botched::client::run_client(opt, std::cout);
    if (!r.ok) {
        std::cerr << "botched-client: " << r.error << '\n';
        return 1;
    }
    return 0;
}

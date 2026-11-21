#include <cctype>
// botched-client: play Botched! in a terminal against a botched-tcp-server.
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "client_core.hpp"
#ifdef BOTCHED_HAVE_SDL
#include "sdl_ui.hpp"
#endif

namespace {
void usage() {
    std::cerr <<
        "usage: botched-client (--create | --join MATCH_ID) [options]\n"
        "  --host H              server host (default 127.0.0.1)\n"
        "  --port N              server port (default 7878)\n"
        "  --create              create a match and join it as player 1\n"
        "    --players N         seats, 2-4 (default 2)\n"
        "    --map-id ID         map to play (see --list-maps; default training-01)\n"
        "  --list-maps           print the server's maps and exit\n"
        "    --bots N            seats played by the server (0-3; at least one human)\n"
        "    --rounds N          rounds (default 12)\n"
        "    --planning-seconds N  planning time per round (default 60)\n"
        "  --join MATCH_ID       join a match created by someone else\n"
        "  --bot                 auto-play instead of using the keyboard (testing/demos)\n"
        "    --seed N            bot seed          --think-ms N   bot pause before submitting\n"
        "  --plain               no ANSI screen control (default when output is not a terminal)\n"
        "  --ui                  graphical SDL window (board sprites, panels, scrollable log)\n"
        "    --programs \"F2 R T;W W W\"   ui: scripted orders per round (demos and tests)\n"
        "    --anim-speed X --no-anim   ui: round replay speed (default 1.0) / disable the replay\n"
        "    --frames N --screenshot FILE.bmp   ui: render N frames, save the last one, exit (testing)\n"
        "  --ascii               plain ASCII board (default unless the locale is UTF-8)\n"
        "  --no-color            no colours (also honours NO_COLOR)\n";
}
}  // namespace

int main(int argc, char** argv) {
    botched::client::ClientOptions opt;
    opt.tty = isatty(1) != 0;
    bool ascii = false, no_color = std::getenv("NO_COLOR") != nullptr;
    {
        const char* loc = std::getenv("LC_ALL");
        if (!loc || !*loc) loc = std::getenv("LC_CTYPE");
        if (!loc || !*loc) loc = std::getenv("LANG");
        std::string l = loc ? loc : "";
        for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        ascii = l.find("utf-8") == std::string::npos && l.find("utf8") == std::string::npos;
    }
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
        else if (a == "--bots") opt.bots = std::atoi(next());
        else if (a == "--map-id") opt.map_id = next();
        else if (a == "--list-maps") opt.list_maps = true;
        else if (a == "--planning-seconds") opt.planning_seconds = std::atoi(next());
        else if (a == "--bot") opt.bot = true;
        else if (a == "--seed") opt.seed = std::strtoull(next(), nullptr, 10);
        else if (a == "--think-ms") opt.bot_think_ms = std::atoi(next());
        else if (a == "--plain") opt.tty = false;
        else if (a == "--ui") opt.ui = true;
        else if (a == "--programs") {  // "R F2 L;F2 F W;..." one entry per round
            std::string all = next(), cur;
            for (char c : all + ";") { if (c == ';') { opt.programs.push_back(cur); cur.clear(); } else cur += c; }
        }
        else if (a == "--no-anim") opt.animate = false;
        else if (a == "--anim-speed") opt.anim_speed = std::atof(next());
        else if (a == "--frames") opt.ui_frames = std::atoi(next());
        else if (a == "--screenshot") opt.ui_screenshot = next();
        else if (a == "--ascii") ascii = true;
        else if (a == "--no-color") no_color = true;
        else { usage(); return 2; }
    }
    if (!opt.list_maps && !opt.create && opt.match_id.empty()) { usage(); return 2; }
    opt.unicode = opt.tty && !ascii;
    opt.color = opt.tty && !no_color;
    if (opt.bot) opt.refresh_ms = 200;
    botched::client::ClientResult r;
    if (opt.ui) {
#ifdef BOTCHED_HAVE_SDL
        r = botched::ui::run_ui(opt);
        if (!r.ok && r.error == "left the match") return 0;  // window closed by the player
#else
        std::cerr << "botched-client: this build has no SDL support (install SDL2 and SDL2_ttf development packages and rebuild)\n";
        return 2;
#endif
    } else {
        r = botched::client::run_client(opt, std::cout);
    }
    if (!r.ok) {
        std::cerr << "botched-client: " << r.error << '\n';
        return 1;
    }
    return 0;
}

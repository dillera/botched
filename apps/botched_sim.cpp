// Reference application: runs a complete Botched! match with simulated players, entirely
// in-process and headless. Doubles as a determinism check (--check).
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "sim_runner.hpp"

namespace {

void usage() {
    std::cerr <<
        "usage: botched-sim [options]\n"
        "  --players N      2-4 simulated players (default 4)\n"
        "  --seed S         bot/noise seed (default 1)\n"
        "  --rounds R       rounds to play (default 12)\n"
        "  --flaky P        percent chance a bot misses a round, forcing a timeout (default 0)\n"
        "  --map FILE       custom map text (default: built-in training-01)\n"
        "  --verbose        print the board after every round\n"
        "  --events         print every event as one JSON line\n"
        "  --check          run twice and verify identical state and event hashes\n";
}

}  // namespace

int main(int argc, char** argv) {
    botched::sim::SimOptions opt;
    opt.out = &std::cout;
    bool check = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            return argv[++i];
        };
        if (a == "--players") opt.players = std::atoi(next());
        else if (a == "--seed") opt.seed = std::strtoull(next(), nullptr, 10);
        else if (a == "--rounds") opt.rounds = std::atoi(next());
        else if (a == "--flaky") opt.flaky_percent = std::atoi(next());
        else if (a == "--map") opt.map_file = next();
        else if (a == "--verbose") opt.verbose = true;
        else if (a == "--events") opt.print_events = true;
        else if (a == "--check") check = true;
        else { usage(); return 2; }
    }
    if (opt.players < 2 || opt.players > 4) { usage(); return 2; }

    botched::sim::SimResult r = botched::sim::run_simulation(opt);
    if (!r.ok) {
        std::cerr << "simulation failed: " << r.error << '\n';
        return 1;
    }

    std::cout << "=== match finished after " << r.rounds_played << " rounds ===\n";
    if (const auto* ps = r.final_snapshot.find("players")) {
        for (const auto& p : ps->items())
            std::cout << "  P" << p.get_int("id").value_or(0) << "  score " << p.get_int("score").value_or(0)
                      << "  deliveries " << p.get_int("deliveries").value_or(0) << "  damage taken "
                      << p.get_int("damage_taken").value_or(0) << '\n';
    }
    std::cout << "winner(s):";
    for (int w : r.winners) std::cout << " P" << w;
    std::cout << "\nevents: " << r.events << "  timeouts: " << r.timeouts << '\n'
              << "state hash: " << r.state_hash << "\nevent hash: " << r.event_hash << '\n';

    if (check) {
        botched::sim::SimOptions again = opt;
        again.out = nullptr;
        botched::sim::SimResult r2 = botched::sim::run_simulation(again);
        if (!r2.ok || r2.state_hash != r.state_hash || r2.event_hash != r.event_hash) {
            std::cerr << "DETERMINISM CHECK FAILED\n";
            return 1;
        }
        std::cout << "determinism check: identical on re-run\n";
    }
    return 0;
}

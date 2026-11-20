// Terminal client for Botched!: joins a match on a TCP host, shows the factory, lets a human
// enter three orders per round, and (with `bot`) can auto-play using the simulator's Bot over
// exactly the same request path. It speaks only the public JSON API.
#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "botched/json.hpp"

namespace botched::client {

struct ClientOptions {
    std::string host = "127.0.0.1";
    int port = 7878;
    bool create = false;           // create a match, then join it as player 1
    std::string match_id;          // join this match (when not creating)
    int players = 2;               // with create: seats (2-4)
    int rounds = 12;               // with create
    int planning_seconds = 60;     // with create
    bool bot = false;              // auto-play instead of reading the keyboard
    std::uint64_t seed = 1;        // bot noise seed
    bool tty = false;              // redraw in place with ANSI codes
    int refresh_ms = 500;          // poll interval
    int bot_think_ms = 0;          // pause before a bot submits (demo pacing)
};

struct ClientResult {
    bool ok = false;
    std::string error;
    int player_id = 0;
    std::string match_id;
    std::vector<int> winners;
    int final_round = 0;
};

// Runs a whole session: lobby, rounds, results. Output goes to `out`. Keyboard input is read from
// fd 0 unless `opt.bot`.
ClientResult run_client(const ClientOptions& opt, std::ostream& out);

// ---- helpers (public for tests) -----------------------------------------------------------

// "F2 R T" -> {"FORWARD_2","TURN_RIGHT","TOGGLE"}. Accepts the short codes and full names.
std::optional<std::array<std::string, 3>> parse_program(const std::string& line, std::string& error);
const char* order_help();
std::string order_code(const std::string& token);  // "FORWARD_2" -> "F2"

// "2026-11-20T20:40:00Z" -> unix milliseconds.
std::optional<std::int64_t> parse_utc_ms(const std::string& text);

// Human-readable lines for an array of events (orders grouped per tick, then what happened).
std::vector<std::string> summarize_events(const Json& events);

// Blocking line-oriented JSON-over-TCP connection.
class Connection {
public:
    ~Connection();
    bool open(const std::string& host, int port, std::string& error);
    // Sends one request line and waits for one response line.
    std::optional<Json> request(const Json& req, std::string& error, int timeout_ms = 10000);
    void close();
private:
    int fd_ = -1;
    std::string buf_;
};

}  // namespace botched::client

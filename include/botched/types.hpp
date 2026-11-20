// Botched! core vocabulary. Pure C++20, no platform dependencies.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace botched {

inline constexpr const char* kProtocol = "botched/1";
inline constexpr const char* kRulesVersion = "botched/1";
inline constexpr int kRegisters = 3;       // orders per round / ticks per round
inline constexpr int kDefaultRounds = 12;
inline constexpr int kMaxPlayers = 4;
inline constexpr int kMaxHealth = 3;       // damage counter 0..2, disabled at 3
inline constexpr int kDeliveryScore = 5;

enum class Dir : std::uint8_t { N = 0, E = 1, S = 2, W = 3 };

constexpr Dir turn_right(Dir d) { return static_cast<Dir>((static_cast<int>(d) + 1) & 3); }
constexpr Dir turn_left(Dir d) { return static_cast<Dir>((static_cast<int>(d) + 3) & 3); }
constexpr Dir opposite(Dir d) { return static_cast<Dir>((static_cast<int>(d) + 2) & 3); }
constexpr int dir_dx(Dir d) { return d == Dir::E ? 1 : d == Dir::W ? -1 : 0; }
constexpr int dir_dy(Dir d) { return d == Dir::S ? 1 : d == Dir::N ? -1 : 0; }

const char* to_string(Dir d);
std::optional<Dir> parse_dir(std::string_view s);

struct Pos {
    int x = 0;
    int y = 0;
    friend constexpr bool operator==(Pos a, Pos b) { return a.x == b.x && a.y == b.y; }
    friend constexpr bool operator!=(Pos a, Pos b) { return !(a == b); }
};

enum class Order : std::uint8_t {
    Forward1, Forward2, Back1, TurnLeft, TurnRight, TurnAround, Drop, Toggle, Wait
};

const char* to_string(Order o);
std::optional<Order> parse_order(std::string_view s);

enum class Terrain : std::uint8_t {
    Floor, Wall, Spawn, Dock, Dispenser, Conveyor, Console, LaserEmitter, Pit
};

const char* to_string(Terrain t);

enum class Phase : std::uint8_t { Planning, Executing, Finished };
const char* to_string(Phase p);
std::optional<Phase> parse_phase(std::string_view s);

}  // namespace botched

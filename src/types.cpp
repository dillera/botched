#include "botched/types.hpp"

namespace botched {

const char* to_string(Dir d) {
    switch (d) {
        case Dir::N: return "N";
        case Dir::E: return "E";
        case Dir::S: return "S";
        case Dir::W: return "W";
    }
    return "?";
}

std::optional<Dir> parse_dir(std::string_view s) {
    if (s == "N") return Dir::N;
    if (s == "E") return Dir::E;
    if (s == "S") return Dir::S;
    if (s == "W") return Dir::W;
    return std::nullopt;
}

namespace {
struct OrderName { Order o; const char* n; };
constexpr OrderName kOrders[] = {
    {Order::Forward1, "FORWARD_1"}, {Order::Forward2, "FORWARD_2"}, {Order::Back1, "BACK_1"},
    {Order::TurnLeft, "TURN_LEFT"}, {Order::TurnRight, "TURN_RIGHT"},
    {Order::TurnAround, "TURN_AROUND"}, {Order::Drop, "DROP"}, {Order::Toggle, "TOGGLE"},
    {Order::Wait, "WAIT"},
};
}  // namespace

const char* to_string(Order o) {
    for (const auto& e : kOrders) if (e.o == o) return e.n;
    return "?";
}

std::optional<Order> parse_order(std::string_view s) {
    for (const auto& e : kOrders) if (s == e.n) return e.o;
    return std::nullopt;
}

const char* to_string(Terrain t) {
    switch (t) {
        case Terrain::Floor: return "FLOOR";
        case Terrain::Wall: return "WALL";
        case Terrain::Spawn: return "SPAWN";
        case Terrain::Dock: return "DOCK";
        case Terrain::Dispenser: return "DISPENSER";
        case Terrain::Conveyor: return "CONVEYOR";
        case Terrain::Console: return "CONSOLE";
        case Terrain::LaserEmitter: return "LASER_EMITTER";
        case Terrain::Trap: return "TRAP";
        case Terrain::TrapConsole: return "TRAP_CONSOLE";
        case Terrain::Pit: return "PIT";
    }
    return "?";
}

const char* to_string(Phase p) {
    switch (p) {
        case Phase::Planning: return "PLANNING";
        case Phase::Executing: return "EXECUTING";
        case Phase::Finished: return "FINISHED";
    }
    return "?";
}

std::optional<Phase> parse_phase(std::string_view s) {
    if (s == "PLANNING") return Phase::Planning;
    if (s == "EXECUTING") return Phase::Executing;
    if (s == "FINISHED") return Phase::Finished;
    return std::nullopt;
}

}  // namespace botched

// Mutable simulation state and the structured event log.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "botched/json.hpp"
#include "botched/types.hpp"

namespace botched {

struct Robot {
    int id = 0;  // player id 1..4
    Pos pos;     // meaningless while respawn_pending
    Dir heading = Dir::N;
    int damage = 0;            // 0..2 while active; 3 only while respawn_pending after a laser disable
    int carrying = 0;          // crate id, 0 = none
    int deliveries = 0;
    int score = 0;
    int damage_taken = 0;      // cumulative laser/trap hits over the match (tie-break)
    int sabotage = 0;          // G2-A points earned from owned machinery; score = 5 x deliveries + sabotage
    bool respawn_pending = false;
    friend bool operator==(const Robot&, const Robot&) = default;
};

struct Crate {
    int id = 0;
    bool carried = false;
    Pos pos;           // valid when !carried
    int carrier = 0;   // player id when carried
    friend bool operator==(const Crate&, const Crate&) = default;
};

struct Group {
    char id = 'A';
    bool reversed = false;
    friend bool operator==(const Group&, const Group&) = default;
};

// G2-A proximity trap state (device positions come from the map).
struct TrapState {
    int id = 0;
    bool armed = true;  // maps start armed
    int owner = 0;      // player id, 0 = neutral
    friend bool operator==(const TrapState&, const TrapState&) = default;
};

// The world. Everything the rules engine reads or writes.
struct GameState {
    Phase phase = Phase::Planning;
    int round = 0;  // 1-based once started
    int tick = 0;   // 0 during planning, 1..3 while executing / after a tick
    int rounds_total = kDefaultRounds;  // fixed-round mode: the match length; score-target mode: the safety cap
    int target_score = 0;               // 0 = fixed-round mode; N > 0 = first to N points (checked after a whole round)
    int next_crate_id = 1;
    std::vector<Robot> robots;   // index = player id - 1
    std::vector<Crate> crates;   // ascending id
    std::vector<Group> groups;   // ascending id
    std::vector<TrapState> traps;  // ascending id; empty on maps without traps

    int player_count() const { return static_cast<int>(robots.size()); }
    Robot& robot(int id) { return robots[static_cast<std::size_t>(id - 1)]; }
    const Robot& robot(int id) const { return robots[static_cast<std::size_t>(id - 1)]; }
    Crate* crate(int id);
    const Crate* crate(int id) const;
    Group* group(char id);
    const Group* group(char id) const;
    TrapState* trap(int id);
    const TrapState* trap(int id) const;
    Robot* robot_at(Pos p);                 // active robot on tile
    Crate* loose_crate_at(Pos p);
    void remove_crate(int id);
    friend bool operator==(const GameState&, const GameState&) = default;
};

struct Event {
    int state_version = 0;
    int round = 0;
    int tick = 0;
    int sequence = 0;
    std::string type;
    Json params = Json::object();
};

// Collects events and stamps round/tick/sequence. Sequence restarts at 1 whenever
// (round, tick) changes. state_version is set by the owner (Match) per publication.
class EventLog {
public:
    void set_version(int v) { version_ = v; }
    void begin(int round, int tick);
    void emit(std::string type, Json params = Json::object());
    const std::vector<Event>& events() const { return events_; }
    std::vector<Event> take();
    int round() const { return round_; }
    int tick() const { return tick_; }
    int sequence() const { return seq_; }
    void restore_cursor(int round, int tick, int seq) { round_ = round; tick_ = tick; seq_ = seq; }

private:
    std::vector<Event> events_;
    int version_ = 0;
    int round_ = -1;
    int tick_ = -1;
    int seq_ = 0;
};

using Programme = std::array<Order, kRegisters>;

}  // namespace botched

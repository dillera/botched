// Round playback for the graphical client (no SDL): replays the server's ordered event stream
// onto a local copy of the board, one visible step at a time. The client never predicts
// movement: every position, heading, score and crate change comes from an event, and the board
// the player finally sees is replaced by the authoritative snapshot anyway.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "botched/json.hpp"
#include "botched/types.hpp"

namespace botched::ui {

struct RobotView {
    int id = 0;
    bool active = false;  // on the board (false while disabled / waiting to respawn)
    int x = 0, y = 0;
    Dir heading = Dir::N;
    int carry_id = 0;     // crate id carried, 0 none
    int damage = 0, score = 0, deliveries = 0;
    bool disabled = false;
};

struct CrateView {
    int id = 0;
    int x = 0, y = 0;
};

// The part of a snapshot the board draws.
struct BoardView {
    std::vector<RobotView> robots;
    std::vector<CrateView> crates;  // loose crates only
    std::vector<bool> reversed = std::vector<bool>(26, false);

    RobotView* robot(int id);
    const RobotView* robot(int id) const;
    const RobotView* robot_at(int x, int y) const;
    const CrateView* crate_at(int x, int y) const;
    bool group_reversed(char g) const { return g >= 'A' && g <= 'Z' && reversed[static_cast<std::size_t>(g - 'A')]; }
    bool equals(const BoardView& other) const;
    std::string diff(const BoardView& other) const;  // "" when equal (test diagnostics)
};

BoardView read_board(const Json& snapshot);
// Commits one event's state change (movement, pickup, score, ...). Unknown types change nothing.
void apply_event(BoardView& b, const Json& event);

// ---- execution queue ----------------------------------------------------------------------
struct QueueEntry {
    bool machinery = false;  // the conveyor/laser/hazard phase that follows a tick's orders
    int round = 0, tick = 0; // tick 1..3 (the "slot" number)
    int player = 0;
    std::string order;       // order token ("FORWARD_2"); empty for machinery
    bool no_op = false;      // ORDER_NO_OP: could not act
    bool skipped = false;    // ORDER_SKIPPED_DISABLED
};

// Entries in the exact order the server executed them, derived from ORDER_* events only:
// per tick one entry per player, then one machinery entry.
std::vector<QueueEntry> build_queue(const Json& events);
// "P1(1) P2(1) * P2(2) P1(2) * ..." ('*' = machinery) for tests and logs.
std::string queue_text(const std::vector<QueueEntry>& q, int round);

// ---- playback ------------------------------------------------------------------------------
struct Step {
    enum Kind {
        Mark, Turn, Move, Push, Blocked, Pickup, Deliver, Drop, Toggle, CrateMove, CrateGone,
        Laser, Disable, Respawn, Spawn, Quiet, Instant
    } kind = Instant;
    enum Phase { Orders, Machinery, RoundEdge } phase = Orders;
    int ms = 0;
    int queue_index = -1;    // entry highlighted while this step plays; -1 none
    int round = 0, tick = 0;
    bool belt = false;       // movement caused by a conveyor
    int player = 0;
    int cx = 0, cy = 0;      // effect location (impact, pop, flash)
    Dir from = Dir::N, to = Dir::N;      // Turn / Blocked direction in `to`
    char group = 'A';                    // Toggle: belt group that flipped
    struct Mover { int id; int fx, fy, tx, ty; bool crate; };
    std::vector<Mover> movers;
    std::vector<std::size_t> events;     // indices committed by this step
    bool commit_first = false;           // commit events when the step starts (else when it ends)
    bool tick_start = false;             // first step of a tick (narration is released per tick)
    std::string caption;
    std::string order_code;  // Mark: short order code ("F2")
};

class Playback {
public:
    void set_speed(double s) { speed_ = s < 0.1 ? 0.1 : s > 8 ? 8 : s; }
    double speed() const { return speed_; }

    // Begin (or extend) playback from `base` with a batch of events.
    void start(const BoardView& base, const Json& events);
    // Advance wall-clock time; steps are paced by speed.
    void advance(int dt_ms);
    // Jump to the end of everything queued: all remaining events are committed.
    void skip();
    bool active() const { return pos_ < steps_.size(); }

    const BoardView& board() const { return board_; }
    // Current step (valid when active()) and progress 0..1 within it.
    const Step* current() const { return active() ? &steps_[pos_] : nullptr; }
    double progress() const;
    // Queue of the round being shown, which entries are finished and which one is live.
    const std::vector<QueueEntry>& queue() const { return queue_; }
    int queue_round() const { return queue_round_; }
    int queue_live() const;       // index of the highlighted entry, or -1
    int queue_done() const;       // entries [0, done) are finished
    // Log lines to release: number of steps completed that start a new tick, for synced narration.
    int tick_steps_started() const { return ticks_started_; }
    int ticks_scheduled() const { return ticks_scheduled_; }
    std::size_t total_steps() const { return steps_.size(); }
    std::size_t step_index() const { return pos_; }

private:
    void build(const Json& events, std::size_t first_event);
    void begin_step();
    void finish_step();
    void commit(std::size_t step);
    void finished_queue();

    BoardView board_;
    std::vector<Json> events_;
    std::vector<Step> steps_;
    std::vector<QueueEntry> all_queue_;  // across rounds; steps index into it
    std::vector<QueueEntry> queue_;      // entries of queue_round_
    int queue_base_ = 0;                 // all_queue_ index of queue_[0]
    int queue_round_ = 0;
    std::size_t pos_ = 0;
    int elapsed_ = 0;
    bool began_ = false;
    double speed_ = 1.0;
    int ticks_started_ = 0;
    int ticks_scheduled_ = 0;
    std::vector<char> committed_;
    bool done_all_ = false;
};

}  // namespace botched::ui

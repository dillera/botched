// Round playback (SDL-free): the execution queue and event replay are derived from the server's
// ordered events; the replayed board must end exactly on the authoritative snapshot.
#include <array>
#include <string>
#include <vector>

#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include "bot.hpp"
#include "doctest.h"
#include "map_fixture.hpp"
#include "ui_playback.hpp"

using namespace botched;
using namespace botched::ui;

namespace {

using Prog = std::array<const char*, 3>;

struct Round {
    Json events = Json::array();
    BoardView before, after;
    int number = 0;
};

// Plays the documented training-01 scenario with two players, round by round.
struct Scenario {
    Match match;
    std::vector<Round> rounds;
    Scenario() : match(make()) {
        play(1, {"TURN_RIGHT", "FORWARD_2", "TURN_LEFT"}, {"TURN_LEFT", "FORWARD_2", "TURN_RIGHT"});
        play(2, {"FORWARD_2", "FORWARD_1", "WAIT"}, {"FORWARD_2", "FORWARD_1", "FORWARD_1"});
        // Round 3: P1 rides the belt, P2 pushes and is pushed, and a robot ends in the pit.
        play(3, {"TURN_RIGHT", "FORWARD_1", "FORWARD_1"}, {"WAIT", "FORWARD_2", "FORWARD_1"});
    }
    static Match make() {
        MatchConfig c;
        c.match_id = "m";
        c.map = std::make_shared<const Map>(training01());
        c.player_count = 2;
        c.rounds = 6;
        return Match(c);
    }
    void play(int round, Prog a, Prog b) {
        Round r;
        r.number = round;
        r.before = read_board(match.snapshot());
        match.submit_program(1, round, 1, {a[0], a[1], a[2]}, true, "a" + std::to_string(round));
        match.submit_program(2, round, 1, {b[0], b[1], b[2]}, true, "b" + std::to_string(round));
        const int version = match.state_version();
        match.run_round();
        const auto evs = match.events_since(version);
        for (const auto& e : *evs) r.events.push(event_to_json(e, "m"));
        r.after = read_board(match.snapshot());
        rounds.push_back(std::move(r));
    }
};

bool has_event(const Json& events, const char* type, const char* key = nullptr, const char* value = nullptr) {
    for (const auto& e : events.items()) {
        if (e.get_string("type").value_or("") != type) continue;
        if (!key) return true;
        const Json* p = e.find("params");
        if (p && p->get_string(key).value_or("") == value) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("playback: execution queue shows the rotating priority, with a machinery phase after each tick") {
    Scenario sc;
    const auto q1 = build_queue(sc.rounds[0].events);
    const auto q2 = build_queue(sc.rounds[1].events);
    CHECK(queue_text(q1, 1) == "P1(1) P2(1) * P2(2) P1(2) * P1(3) P2(3) *");
    CHECK(queue_text(q2, 2) == "P2(1) P1(1) * P1(2) P2(2) * P2(3) P1(3) *");
    // The queue is derived from ORDER_* events and nothing else: it carries each player's order.
    CHECK(q1[0].order == "TURN_RIGHT");
    CHECK(q1[1].order == "TURN_LEFT");
}

TEST_CASE("playback: priority rotation holds for 3 and 4 players") {
    for (int n : {3, 4}) {
        MatchConfig c;
        c.match_id = "m";
        c.map = std::make_shared<const Map>(training01());
        c.player_count = n;
        c.rounds = 3;
        Match m(c);
        for (int round = 1; round <= 3; ++round) {
            const int version = m.state_version();
            m.run_round();  // everyone WAITs
            Json events = Json::array();
            const auto evs = m.events_since(version);
            for (const auto& e : *evs) events.push(event_to_json(e, "m"));
            const auto q = build_queue(events);
            std::string expect;
            for (int tick = 1; tick <= 3; ++tick) {
                const int start = (((round - 1) * 3 + tick - 1) % n) + 1;
                for (int i = 0; i < n; ++i) {
                    if (!expect.empty()) expect += ' ';
                    expect += "P" + std::to_string((start - 1 + i) % n + 1) + "(" + std::to_string(tick) + ")";
                }
                expect += " *";
            }
            CHECK(queue_text(q, round) == expect);
        }
    }
}

TEST_CASE("playback: the scenario round contains a belt ride, a push and a pit fall") {
    Scenario sc;
    const Json& ev = sc.rounds[2].events;
    CHECK(has_event(ev, "ROBOT_PUSHED"));
    CHECK(has_event(ev, "ROBOT_MOVED", "cause", "CONVEYOR"));
    CHECK(has_event(ev, "ROBOT_DISABLED", "cause", "PIT"));
}

TEST_CASE("playback: replay ends exactly on the authoritative snapshot, every round") {
    Scenario sc;
    for (const auto& r : sc.rounds) {
        Playback pb;
        pb.start(r.before, r.events);
        REQUIRE(pb.active());
        for (int guard = 0; pb.active() && guard < 100000; ++guard) pb.advance(50);
        CHECK_FALSE(pb.active());
        CHECK_MESSAGE(pb.board().equals(r.after), "round " << r.number << ": " << pb.board().diff(r.after));
    }
}

TEST_CASE("playback: skipping lands on the same authoritative state at any moment") {
    Scenario sc;
    const Round& r = sc.rounds[2];
    Playback probe;
    probe.start(r.before, r.events);
    const std::size_t total = probe.total_steps();
    REQUIRE(total > 10);
    for (int ms : {0, 1, 400, 1500, 4000, 7000}) {
        Playback pb;
        pb.start(r.before, r.events);
        pb.advance(ms);
        pb.skip();
        CHECK_FALSE(pb.active());
        CHECK_MESSAGE(pb.board().equals(r.after), "skip after " << ms << "ms: " << pb.board().diff(r.after));
    }
    Playback pb;  // skip immediately, before any frame
    pb.start(r.before, r.events);
    pb.skip();
    CHECK(pb.board().equals(r.after));
}

TEST_CASE("playback: intermediate frames are real prefixes of the event stream, never the final state") {
    Scenario sc;
    const Round& r = sc.rounds[2];
    // Board after applying the first k events, for every k.
    std::vector<BoardView> prefix{r.before};
    BoardView b = r.before;
    for (const auto& e : r.events.items()) {
        apply_event(b, e);
        prefix.push_back(b);
    }
    REQUIRE(prefix.back().equals(r.after));

    Playback pb;
    pb.start(r.before, r.events);
    int frames = 0, distinct = 0, first_final = -1;
    std::size_t last_matched = 0;
    BoardView prev = pb.board();
    while (pb.active()) {
        pb.advance(10);
        ++frames;
        bool matched = false;
        for (std::size_t k = last_matched; k < prefix.size(); ++k)
            if (pb.board().equals(prefix[k])) { matched = true; last_matched = k; break; }
        CHECK_MESSAGE(matched, "frame " << frames << " is not a prefix state: " << pb.board().diff(prefix[last_matched]));
        if (first_final < 0 && pb.board().equals(r.after)) first_final = frames;
        if (!pb.board().equals(prev)) { ++distinct; prev = pb.board(); }
    }
    CHECK(distinct >= 8);          // many visible intermediate states
    CHECK(first_final > frames * 4 / 5);  // the final state only appears in the last stretch of the replay
    CHECK(frames > 200);           // and it takes real time (2-player round ~10 s at 1x)
}

TEST_CASE("playback: belt movement and voluntary movement are separate step kinds in separate phases") {
    Scenario sc;
    const Round& r = sc.rounds[1];  // P1 WAITs on a belt in tick 3
    Playback pb;
    pb.start(r.before, r.events);
    bool saw_belt_wait = false, orders_before_machinery = true;
    int seen_machinery_in_tick[4] = {0, 0, 0, 0};
    while (pb.active()) {
        const Step* s = pb.current();
        REQUIRE(s->tick >= 0);
        REQUIRE(s->tick <= 3);
        if (s->kind == Step::Move && s->belt) {
            CHECK(s->phase == Step::Machinery);
            if (s->caption.find("ordered WAIT") != std::string::npos) saw_belt_wait = true;
            ++seen_machinery_in_tick[s->tick];
        }
        if (s->kind == Step::Move && !s->belt && s->phase != Step::Orders) orders_before_machinery = false;
        if (s->phase == Step::Orders && seen_machinery_in_tick[s->tick]) orders_before_machinery = false;
        pb.advance(25);
    }
    CHECK(saw_belt_wait);
    CHECK(orders_before_machinery);
}

TEST_CASE("playback: pushes move the victim and the pusher together; the pit fall disables after the move") {
    Scenario sc;
    const Round& r = sc.rounds[2];
    Playback pb;
    pb.start(r.before, r.events);
    bool push_step = false, pit_after_move = false, respawn_seen = false;
    bool entered_pit = false;
    while (pb.active()) {
        const Step* s = pb.current();
        if (s->kind == Step::Push) { push_step = true; CHECK(s->movers.size() >= 2); }
        if (s->kind == Step::Move && s->cx == 5 && s->cy == 4) entered_pit = true;
        if (s->kind == Step::Disable && entered_pit) {
            pit_after_move = true;
            CHECK(pb.board().robot(s->player)->active);  // still visible while the effect plays
        }
        if (s->kind == Step::Respawn) respawn_seen = true;
        pb.advance(25);
    }
    CHECK(push_step);
    CHECK(pit_after_move);
    CHECK(respawn_seen);  // round end restores the disabled robot at its spawn
}

TEST_CASE("playback: speed scales the pace and events arriving mid-playback extend it") {
    Scenario sc;
    const Round& r1 = sc.rounds[0];
    auto run = [&](double speed) {
        Playback pb;
        pb.set_speed(speed);
        pb.start(r1.before, r1.events);
        int ms = 0;
        while (pb.active()) { pb.advance(10); ms += 10; }
        return ms;
    };
    const int slow = run(1.0), fast = run(4.0);
    CHECK(fast < slow / 3);
    CHECK(fast > slow / 5);

    Playback pb;
    pb.start(r1.before, r1.events);
    pb.advance(1000);
    pb.start(sc.rounds[0].after, sc.rounds[1].events);  // next round's events while still playing
    pb.skip();
    CHECK(pb.board().equals(sc.rounds[1].after));
}

TEST_CASE("playback: four-player bot matches replay to the authoritative state, at a watchable pace") {
    MatchConfig c;
    c.match_id = "m";
    c.map = std::make_shared<const Map>(training01());
    c.player_count = 4;
    c.rounds = 12;
    Match m(c);
    std::vector<sim::Bot> bots;
    for (int p = 1; p <= 4; ++p) bots.emplace_back(p, m.map(), 1000003ULL + static_cast<unsigned>(p));
    for (int round = 1; round <= 12; ++round) {
        const BoardView before = read_board(m.snapshot());
        const Json snap = m.snapshot();
        for (int p = 1; p <= 4; ++p) {
            auto pr = bots[static_cast<std::size_t>(p - 1)].plan(snap);
            m.submit_program(p, round, 1, {pr[0], pr[1], pr[2]}, true, "r" + std::to_string(round) + "p" + std::to_string(p));
        }
        const int version = m.state_version();
        m.run_round();
        const auto evs = m.events_since(version);
        Json events = Json::array();
        for (const auto& e : *evs) events.push(event_to_json(e, "m"));
        const BoardView after = read_board(m.snapshot());

        const auto q = build_queue(events);
        std::string expect;  // 4 players: priority starts at ((abs_tick-1) % 4) + 1
        for (int tick = 1; tick <= 3; ++tick) {
            const int start = (((round - 1) * 3 + tick - 1) % 4) + 1;
            for (int i = 0; i < 4; ++i) expect += (expect.empty() ? "" : " ") + std::string("P") + std::to_string((start - 1 + i) % 4 + 1) + "(" + std::to_string(tick) + ")";
            expect += " *";
        }
        CHECK(queue_text(q, round) == expect);

        Playback pb;
        pb.start(before, events);
        int ms = 0;
        while (pb.active()) { pb.advance(10); ms += 10; }
        CHECK_MESSAGE(pb.board().equals(after), "round " << round << ": " << pb.board().diff(after));
        CHECK(ms >= 8000);   // a four-player round is meant to take roughly 10-15 s at 1x
        CHECK(ms <= 20000);
    }
}

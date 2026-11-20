// Acceptance tests 8-10: consoles, lasers, disables, pits, crate drop/destroy, respawn.
#include "rig.hpp"

using namespace rig;

namespace {
const char* kConsoleRoom =
    "# # # # # # #\n"
    "# . C> C> . . #\n"
    "# . . T . . #\n"
    "# # # # # # #\n";
}  // namespace

TEST_CASE("8. TOGGLE takes effect in the same tick's conveyor phase") {
    Rig r(kConsoleRoom, 1);
    r.place(1, 2, 2, Dir::N);  // west of console (3,2)
    r.loose(1, 2, 1);
    r.program(1, {Order::Toggle});
    r.tick(1);
    CHECK(r.s.group('A')->reversed);
    CHECK(r.s.crate(1)->pos == Pos{1, 1});  // moved west: the belt was already reversed
    CHECK(r.index_of("GROUP_TOGGLED") < r.index_of("CRATE_MOVED"));
}

TEST_CASE("8. without a toggle the same crate moves east") {
    Rig r(kConsoleRoom, 1);
    r.place(1, 2, 2, Dir::N);
    r.loose(1, 2, 1);
    r.tick(1);
    CHECK(r.s.crate(1)->pos == Pos{3, 1});
}

TEST_CASE("8. two toggles of the same group cancel") {
    Rig r(kConsoleRoom, 2);
    r.place(1, 2, 2, Dir::N);
    r.place(2, 4, 2, Dir::N);  // east of the console
    r.program(1, {Order::Toggle});
    r.program(2, {Order::Toggle});
    r.tick(1);
    CHECK_FALSE(r.s.group('A')->reversed);
    CHECK(r.count("GROUP_TOGGLED") == 2);
}

TEST_CASE("8. TOGGLE away from every console is a no-op") {
    Rig r(kConsoleRoom, 1);
    r.place(1, 5, 1, Dir::N);
    r.program(1, {Order::Toggle});
    r.tick(1);
    CHECK_FALSE(r.s.group('A')->reversed);
    CHECK(r.count("ORDER_NO_OP") == 1);
}

TEST_CASE("8. with several adjacent consoles the smallest console id is toggled") {
    // Consoles in (y,x) scan order: (2,1) group B = T001, (1,2) group A = T002.
    Rig r("# # # # #\n# . TB . #\n# TA . . #\n# # # # #\n", 1);
    REQUIRE(r.map.consoles.size() == 2);
    CHECK(r.map.consoles[0].id < r.map.consoles[1].id);
    r.place(1, 2, 2, Dir::N);  // below TB (2,1)? (2,1) is N of (2,2); TA at (1,2) is W of (2,2)
    r.program(1, {Order::Toggle});
    r.tick(1);
    CHECK(r.s.group('B')->reversed);
    CHECK_FALSE(r.s.group('A')->reversed);
}

TEST_CASE("8. reversed belts push robots the other way") {
    Rig r(kConsoleRoom, 1);
    r.place(1, 3, 1, Dir::N);
    r.s.group('A')->reversed = true;
    r.tick(1);
    CHECK(r.at(1) == Pos{2, 1});
}

TEST_CASE("9. a laser stops at a wall and hits every robot in front of it") {
    Rig r("# # # # # # # #\n# L> . . . # . #\n# # # # # # # #\n", 3);
    r.place(1, 2, 1);
    r.place(2, 4, 1);
    r.place(3, 6, 1);  // behind the wall
    r.tick(1);
    CHECK(r.s.robot(1).damage == 1);
    CHECK(r.s.robot(2).damage == 1);
    CHECK(r.s.robot(3).damage == 0);
    CHECK(r.count("LASER_HIT") == 2);
}

TEST_CASE("9. rays are not stopped by robots or crates") {
    Rig r("# # # # # # #\n# L> . . . . #\n# # # # # # #\n", 2);
    r.place(1, 2, 1);
    r.place(2, 5, 1);
    r.loose(1, 3, 1);
    r.tick(1);
    CHECK(r.s.robot(1).damage == 1);
    CHECK(r.s.robot(2).damage == 1);
}

TEST_CASE("9. overlapping beams hit a robot only once per tick") {
    Rig r("# # # # # # # #\n# L> . . . . L< #\n# # # # # # # #\n", 1);
    r.place(1, 3, 1);
    r.tick(1);
    CHECK(r.s.robot(1).damage == 1);
    CHECK(r.s.robot(1).damage_taken == 1);
    CHECK(r.count("LASER_HIT") == 1);
    r.tick(2);  // a new tick may hit again
    CHECK(r.s.robot(1).damage == 2);
}

TEST_CASE("9. a ray stops at another emitter") {
    Rig r("# # # # # # #\n# L> . L> . . #\n# # # # # # #\n", 1);
    r.place(1, 4, 1);
    r.tick(1);
    CHECK(r.s.robot(1).damage == 1);  // only the second emitter's ray reaches it
    CHECK(r.count("LASER_HIT") == 1);
}

TEST_CASE("9. vertical emitters work too") {
    Rig r("# # #\n# Lv #\n# . #\n# . #\n# # #\n", 1);
    r.place(1, 1, 3);
    r.tick(1);
    CHECK(r.s.robot(1).damage == 1);
}

TEST_CASE("10. the third laser hit disables the robot, drops its crate on its tile, skips its remaining orders") {
    Rig r("# # # # # # #\n# L> . . . . #\n# # # # # # #\n", 1);
    r.place(1, 3, 1);
    r.s.robot(1).damage = 2;
    r.loose(1, 0, 0);
    r.give(1, 1);
    r.program(1, {Order::Wait, Order::Forward1, Order::Forward1});
    r.tick(1);
    CHECK(r.s.robot(1).respawn_pending);
    CHECK_FALSE(r.s.crate(1)->carried);
    CHECK(r.s.crate(1)->pos == Pos{3, 1});
    r.log.take();
    r.tick(2);
    r.tick(3);
    CHECK(r.count("ORDER_SKIPPED_DISABLED") == 2);
    CHECK(r.count("ORDER_EXECUTED") == 0);
}

TEST_CASE("10. a crate that cannot be dropped on disable is destroyed") {
    Rig r("# # # # # # #\n# L> . . . . #\n# # # # # # #\n", 1);
    r.place(1, 3, 1);
    r.s.robot(1).damage = 2;
    r.loose(1, 0, 0);
    r.give(1, 1);
    r.loose(2, 3, 1);  // already a loose crate under the robot
    r.tick(1);
    CHECK(r.s.crate(1) == nullptr);
    CHECK(r.count("CRATE_DESTROYED") == 1);
    CHECK(r.s.crate(2) != nullptr);
}

TEST_CASE("10. pit disable drops the crate on the last safe tile") {
    Rig r("# # # # #\n# . . O #\n# # # # #\n", 1);
    r.place(1, 2, 1, Dir::E);
    r.loose(1, 0, 0);
    r.give(1, 1);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.s.robot(1).respawn_pending);
    CHECK(r.s.crate(1)->pos == Pos{2, 1});
    CHECK_FALSE(r.s.crate(1)->carried);
}

TEST_CASE("10. pit disable destroys the crate if the last safe tile already holds one") {
    Rig r("# # # # #\n# . . O #\n# # # # #\n", 1);
    r.place(1, 2, 1, Dir::E);
    r.loose(1, 0, 0);
    r.give(1, 1);
    r.loose(2, 2, 1);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.s.crate(1) == nullptr);
    CHECK(r.count("CRATE_DESTROYED") == 1);
}

TEST_CASE("10. a robot pushed into a pit drops its crate where it stood; the pusher collects it") {
    Rig r("# # # # # #\n# . . . O #\n# # # # # #\n", 2);
    r.place(1, 1, 1, Dir::E);
    r.place(2, 2, 1, Dir::E);
    r.loose(1, 0, 0);
    r.give(2, 1);
    // P1 pushes P2 east: P2 ends on (3,1) which is not a pit; push again with FORWARD.
    r.program(1, {Order::Forward2});
    r.tick(1);
    // First step: P2 -> (3,1); second step: P2 -> pit (4,1), crate dropped on (3,1) where P1 arrives.
    CHECK(r.s.robot(2).respawn_pending);
    CHECK(r.at(1) == Pos{3, 1});
    CHECK(r.s.robot(1).carrying == 1);  // collected on arrival, after the farthest robot resolved
}

TEST_CASE("10. lasers do not add damage to a robot disabled by a pit in the same tick") {
    Rig r("# # # # # #\n# L> . O . #\n# # # # # #\n", 1);
    r.place(1, 2, 1, Dir::E);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.s.robot(1).respawn_pending);
    CHECK(r.s.robot(1).damage == 0);
}

TEST_CASE("10. respawn: free spawn, redirect by (distance, y, x), and pending retry") {
    const char* kSpawn =
        "# # # # #\n"
        "# . . . #\n"
        "# . S2 . #\n"
        "# . . . #\n"
        "# # # # #\n";
    {
        Rig r(kSpawn, 2);
        r.place(2, 0, 0);
        r.s.robot(2).respawn_pending = true;
        r.s.robot(2).damage = 3;
        r.s.robot(2).score = 7;
        r.engine().process_respawns(r.s);
        CHECK_FALSE(r.s.robot(2).respawn_pending);
        CHECK(r.at(2) == Pos{2, 2});
        CHECK(r.s.robot(2).damage == 0);
        CHECK(r.s.robot(2).score == 7);
        CHECK(r.s.robot(2).heading == Dir::W);
    }
    {
        Rig r(kSpawn, 2);
        r.place(1, 2, 2);  // sits on player 2's spawn
        r.s.robot(2).respawn_pending = true;
        r.engine().process_respawns(r.s);
        CHECK(r.at(2) == Pos{2, 1});  // distance 1, smallest y
        CHECK(r.count("RESPAWN_REDIRECTED") == 1);
    }
    {
        Rig r(kSpawn, 2);
        r.place(1, 2, 2);
        r.place(2, 0, 0);
        // Block the whole distance-1 ring and part of distance 2 with extra robots by shrinking the board.
        r.s.robot(2).respawn_pending = true;
        r.map.tile({2, 1}).terrain = Terrain::Wall;
        r.engine().process_respawns(r.s);
        CHECK(r.at(2) == Pos{1, 2});  // (2,1) is now a wall: next is y=2, x=1
    }
    {
        // Nowhere to go: stays pending.
        Rig r("# # #\n# S2 #\n# # #\n", 2);
        r.place(1, 1, 1);
        r.s.robot(2).respawn_pending = true;
        r.engine().process_respawns(r.s);
        CHECK(r.s.robot(2).respawn_pending);
    }
}

TEST_CASE("10. redirected respawns never use pits") {
    Rig r("# # # # #\n# O S2 O #\n# # # # #\n", 2);
    r.place(1, 2, 1);
    r.s.robot(2).respawn_pending = true;
    r.engine().process_respawns(r.s);
    CHECK(r.s.robot(2).respawn_pending);  // both neighbours are pits
}

TEST_CASE("10. start_round retries pending respawns") {
    Rig r("# # # # #\n# . . . #\n# . S2 . #\n# . . . #\n# # # # #\n", 2);
    r.s.round = 3;
    r.s.robot(2).respawn_pending = true;
    r.engine().start_round(r.s);
    CHECK_FALSE(r.s.robot(2).respawn_pending);
    CHECK(r.s.round == 4);
}

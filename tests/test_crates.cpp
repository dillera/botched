// Acceptance tests 5-7: pickup, delivery, carrying, drop, conveyors.
#include "rig.hpp"

using namespace rig;

namespace {
// Row y=1:  # S1 D1 . G1 . D2 S2 #   (players 1 and 2 with docks and a dispenser)
const char* kDockRoom =
    "# # # # # # # # #\n"
    "# S1 D1 . G1 . D2 S2 #\n"
    "# . . . . . . . #\n"
    "# # # # # # # # #\n";
}  // namespace

TEST_CASE("5. dispensers spawn one crate per round with stable ids; occupied dispensers skip") {
    Rig r(kDockRoom, 2);
    r.s.round = 0;
    Engine e = r.engine();
    e.start_round(r.s);
    REQUIRE(r.s.crates.size() == 1);
    CHECK(r.s.crates[0].id == 1);
    CHECK(r.s.crates[0].pos == Pos{4, 1});
    CHECK(r.count("CRATE_SPAWNED") == 1);
    // Loose crate already there: no second crate.
    e.start_round(r.s);
    CHECK(r.s.crates.size() == 1);
    // Robot standing on the dispenser blocks the spawn.
    r.s.crates.clear();
    r.place(1, 4, 1);
    e.start_round(r.s);
    CHECK(r.s.crates.empty());
}

TEST_CASE("5. entering a crate tile empty-handed picks it up") {
    Rig r(kDockRoom, 2);
    r.place(1, 3, 1, Dir::E);
    r.loose(1, 4, 1);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.s.robot(1).carrying == 1);
    CHECK(r.s.crate(1)->carried);
    CHECK(r.s.crate(1)->carrier == 1);
    CHECK(r.count("CRATE_PICKED_UP") == 1);
}

TEST_CASE("5. delivery on own dock scores 5 and removes the crate") {
    Rig r(kDockRoom, 2);
    r.place(1, 3, 1, Dir::W);
    r.loose(1, 0, 0);
    r.give(1, 1);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.at(1) == Pos{2, 1});
    CHECK(r.s.robot(1).score == 5);
    CHECK(r.s.robot(1).deliveries == 1);
    CHECK(r.s.robot(1).carrying == 0);
    CHECK(r.s.crate(1) == nullptr);
    CHECK(r.count("CRATE_DELIVERED") == 1);
}

TEST_CASE("5. a rival dock does not accept the crate") {
    Rig r(kDockRoom, 2);
    r.place(1, 5, 1, Dir::E);
    r.loose(1, 0, 0);
    r.give(1, 1);
    r.program(1, {Order::Forward1});  // onto D2
    r.tick(1);
    CHECK(r.at(1) == Pos{6, 1});
    CHECK(r.s.robot(1).score == 0);
    CHECK(r.s.robot(1).carrying == 1);
    CHECK(r.count("CRATE_DELIVERED") == 0);
}

TEST_CASE("5. carrying capacity is one: a loaded robot does not collect a second crate") {
    Rig r(kDockRoom, 2);
    r.place(1, 3, 1, Dir::E);
    r.loose(1, 0, 0);
    r.give(1, 1);
    r.loose(2, 4, 1);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.s.robot(1).carrying == 1);
    CHECK_FALSE(r.s.crate(2)->carried);
    CHECK(r.s.crate(2)->pos == Pos{4, 1});
}

TEST_CASE("5. a robot starting an order on its own dock holding a crate delivers first") {
    Rig r(kDockRoom, 2);
    r.place(1, 2, 1, Dir::E);
    r.loose(1, 0, 0);
    r.give(1, 1);
    r.program(1, {Order::Wait});
    r.tick(1);
    CHECK(r.s.robot(1).score == 5);
    CHECK(r.index_of("CRATE_DELIVERED") < r.index_of("ORDER_EXECUTED"));
}

TEST_CASE("5. standing still on a loose crate does not pick it up; only movement does") {
    Rig r(kDockRoom, 2);
    r.place(1, 3, 1, Dir::E);
    r.loose(1, 3, 1);
    r.program(1, {Order::Wait});
    r.tick(1);
    CHECK(r.s.robot(1).carrying == 0);
}

TEST_CASE("5. a pushed robot collects a crate on its new tile (no delivery on a rival dock)") {
    Rig r(kDockRoom, 2);
    r.place(1, 4, 1, Dir::W);  // pusher moving W
    r.place(2, 3, 1, Dir::E);  // pushed onto (2,1) = D1 but is player 2: no delivery
    r.loose(1, 2, 1);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.at(2) == Pos{2, 1});
    CHECK(r.s.robot(2).carrying == 1);  // collected while pushed
    CHECK(r.s.robot(2).score == 0);     // D1 belongs to player 1
}

TEST_CASE("6. DROP places the crate on an empty tile, does not score") {
    Rig r(kDockRoom, 2);
    r.place(1, 3, 2, Dir::E);
    r.loose(1, 0, 0);
    r.give(1, 1);
    r.program(1, {Order::Drop});
    r.tick(1);
    CHECK(r.s.robot(1).carrying == 0);
    CHECK_FALSE(r.s.crate(1)->carried);
    CHECK(r.s.crate(1)->pos == Pos{3, 2});
    CHECK(r.s.robot(1).score == 0);
}

TEST_CASE("6. DROP onto a crate-occupied tile is a logged no-op") {
    Rig r(kDockRoom, 2);
    r.place(1, 3, 2, Dir::E);
    r.loose(1, 0, 0);
    r.give(1, 1);
    r.loose(2, 3, 2);
    r.program(1, {Order::Drop});
    r.tick(1);
    CHECK(r.s.robot(1).carrying == 1);
    CHECK(r.count("ORDER_NO_OP") == 1);
    CHECK(r.count("CRATE_DROPPED") == 0);
}

TEST_CASE("6. DROP without a crate is a no-op") {
    Rig r(kDockRoom, 2);
    r.program(1, {Order::Drop});
    r.tick(1);
    CHECK(r.count("ORDER_NO_OP") == 1);
}

namespace {
// Conveyor strip: row 1 has three east conveyors at x=2..4, console off to the side.
const char* kBelt =
    "# # # # # # # #\n"
    "# . C> C> C> . . #\n"
    "# . . T . . . #\n"
    "# # # # # # # #\n";
}  // namespace

TEST_CASE("6. a loose crate on a conveyor moves one cell per tick") {
    Rig r(kBelt, 1);
    r.place(1, 1, 2);
    r.loose(1, 2, 1);
    r.tick(1);
    CHECK(r.s.crate(1)->pos == Pos{3, 1});
    CHECK(r.count("CRATE_MOVED") == 1);
}

TEST_CASE("6. crate contention: ascending crate id wins a shared target, the other stays") {
    // (2,1) pushes S to (2,2); (2,3) pushes N to (2,2). Both target the same cell.
    Rig q("# # # # #\n# . Cv . #\n# . . . #\n# . C^ . #\n# # # # #\n", 1);
    q.place(1, 1, 2);
    q.loose(5, 2, 1);
    q.loose(3, 2, 3);
    q.tick(1);
    CHECK(q.s.crate(3)->pos == Pos{2, 2});  // lower id wins
    CHECK(q.s.crate(5)->pos == Pos{2, 1});  // stays
}

TEST_CASE("6. a crate pushed into a wall stays; into a pit is destroyed; onto a loose crate stays") {
    Rig r("# # # # # #\n# C> C> . O #\n# # # # # #\n", 1);
    r.place(1, 0, 0);
    r.loose(1, 1, 1);
    r.loose(2, 2, 1);
    r.tick(1);
    // crate 1 would target (2,1) which holds crate 2 when crate 1 is processed first: blocked.
    CHECK(r.s.crate(1)->pos == Pos{1, 1});
    CHECK(r.s.crate(2)->pos == Pos{3, 1});
    Rig p("# # # # #\n# C> O . #\n# # # # #\n", 1);
    p.place(1, 0, 0);
    p.loose(1, 1, 1);
    p.tick(1);
    CHECK(p.s.crate(1) == nullptr);
    CHECK(p.count("CRATE_DESTROYED") == 1);
    Rig w("# # #\n# C> #\n# # #\n", 1);
    w.place(1, 0, 0);
    w.loose(1, 1, 1);
    w.tick(1);
    CHECK(w.s.crate(1)->pos == Pos{1, 1});
}

TEST_CASE("6. robots move before crates in the conveyor phase") {
    Rig r(kBelt, 1);
    r.place(1, 3, 1, Dir::N);  // standing on belt (3,1)
    r.loose(1, 2, 1);          // crate on belt (2,1) will land on (3,1) after the robot left
    r.tick(1);
    CHECK(r.at(1) == Pos{4, 1});
    CHECK(r.s.crate(1)->pos == Pos{3, 1});
    CHECK(r.s.robot(1).carrying == 0);
}

TEST_CASE("6. a conveyor carries a crate onto an empty-handed robot, who collects it") {
    Rig k("# # # # #\n# C> . . #\n# # # # #\n", 1);
    k.place(1, 2, 1, Dir::N);
    k.loose(1, 1, 1);
    k.tick(1);
    CHECK(k.s.robot(1).carrying == 1);
    CHECK(k.count("CRATE_PICKED_UP") == 1);
}

TEST_CASE("6. a crate carried onto the robot's own dock is delivered immediately") {
    Rig r("# # # # #\n# C> D1 . #\n# # # # #\n", 1);
    r.place(1, 2, 1, Dir::N);
    r.loose(1, 1, 1);
    r.tick(1);
    CHECK(r.s.robot(1).score == 5);
    CHECK(r.s.crate(1) == nullptr);
}

TEST_CASE("7. conveyor snapshot: an entity that arrives on a belt this phase does not move twice") {
    Rig r(kBelt, 1);
    r.place(1, 1, 1, Dir::E);
    r.program(1, {Order::Forward1});  // steps onto belt (2,1) during the orders phase
    r.tick(1);
    // The snapshot is taken when the conveyor phase starts (after orders), so a robot that
    // stepped onto the belt during orders rides exactly one cell.
    CHECK(r.at(1) == Pos{3, 1});
}

TEST_CASE("7. a robot pushed onto a second belt by a conveyor move gains no extra move") {
    // Belt: (1,1) > (2,1) > (3,1). Robot 1 on (1,1), robot 2 on (2,1).
    Rig r("# # # # # # #\n# C> C> C> . . #\n# # # # # # #\n", 2);
    r.place(1, 1, 1, Dir::N);
    r.place(2, 2, 1, Dir::N);
    r.tick(1);
    // Priority tick 1: P1 first. P1 pushes P2 east to (3,1) and moves to (2,1); P2 has been
    // displaced this phase, so its own proposal is skipped (no second move).
    CHECK(r.at(1) == Pos{2, 1});
    CHECK(r.at(2) == Pos{3, 1});
}

TEST_CASE("7. a crate carried by the belt moves at most once even across consecutive belt tiles") {
    Rig r(kBelt, 1);
    r.place(1, 1, 2);
    r.loose(1, 2, 1);
    r.tick(1);
    CHECK(r.s.crate(1)->pos == Pos{3, 1});  // one cell, although (3,1) is also a belt
}

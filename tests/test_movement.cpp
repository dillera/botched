// Acceptance tests 1-4: rotation, movement, FORWARD_2, push chains, pits, priority.
#include "rig.hpp"

using namespace rig;

TEST_CASE("1. heading rotation covers all three turn orders") {
    Rig r(kRoom, 1);
    r.place(1, 3, 2, Dir::N);
    r.program(1, {Order::TurnRight, Order::TurnLeft, Order::TurnAround});
    r.tick(1);
    CHECK(r.s.robot(1).heading == Dir::E);
    r.tick(2);
    CHECK(r.s.robot(1).heading == Dir::N);
    r.tick(3);
    CHECK(r.s.robot(1).heading == Dir::S);
    CHECK(r.count("ROBOT_ROTATED") == 3);
    CHECK(r.at(1) == Pos{3, 2});  // rotation never translates
}

TEST_CASE("1. forward and back move relative to heading; back keeps heading") {
    Rig r(kRoom, 1);
    r.place(1, 3, 2, Dir::E);
    r.program(1, {Order::Forward1, Order::Back1, Order::Back1});
    r.tick(1);
    CHECK(r.at(1) == Pos{4, 2});
    r.tick(2);
    CHECK(r.at(1) == Pos{3, 2});
    r.tick(3);
    CHECK(r.at(1) == Pos{2, 2});
    CHECK(r.s.robot(1).heading == Dir::E);
}

TEST_CASE("1. walls and the border block movement without damage") {
    Rig r(kRoom, 1);
    r.place(1, 1, 1, Dir::N);  // wall directly north
    r.program(1, {Order::Forward1, Order::TurnLeft, Order::Forward1});  // then facing W: wall
    r.ticks(3);
    CHECK(r.at(1) == Pos{1, 1});
    CHECK(r.count("MOVE_BLOCKED") == 2);
    CHECK(r.s.robot(1).damage == 0);
}

TEST_CASE("1. movement off the map edge is blocked like a wall") {
    // Open edge: no wall ring on the right side.
    Rig r("# # #\n. . .\n# # #\n", 1);
    r.place(1, 2, 1, Dir::E);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.at(1) == Pos{2, 1});
    CHECK(r.count("MOVE_BLOCKED") == 1);
}

TEST_CASE("2. FORWARD_2 keeps the first step when the second is blocked") {
    Rig r(kRoom, 1);
    r.place(1, 5, 2, Dir::E);  // (6,2) free, (7,2) wall
    r.program(1, {Order::Forward2});
    r.tick(1);
    CHECK(r.at(1) == Pos{6, 2});
    CHECK(r.count("ROBOT_MOVED") == 1);
    CHECK(r.count("MOVE_BLOCKED") == 1);
}

TEST_CASE("2. FORWARD_2 moves two cells when clear and stops at once if the first is blocked") {
    Rig r(kRoom, 1);
    r.place(1, 2, 2, Dir::E);
    r.program(1, {Order::Forward2});
    r.tick(1);
    CHECK(r.at(1) == Pos{4, 2});
    Rig b(kRoom, 1);
    b.place(1, 6, 2, Dir::E);
    b.program(1, {Order::Forward2});
    b.tick(1);
    CHECK(b.at(1) == Pos{6, 2});
    CHECK(b.count("MOVE_BLOCKED") == 1);  // only one attempt
}

TEST_CASE("2. FORWARD_2 stops after the first substep if the robot is disabled in a pit") {
    Rig r("# # # # # #\n# . O . . #\n# # # # # #\n", 1);
    r.place(1, 1, 1, Dir::E);
    r.program(1, {Order::Forward2});
    r.tick(1);
    CHECK(r.s.robot(1).respawn_pending);
    CHECK(r.count("ROBOT_MOVED") == 1);
}

TEST_CASE("3. two-robot push chain moves both one cell") {
    Rig r(kRoom, 2);
    r.place(1, 1, 2, Dir::E);
    r.place(2, 2, 2, Dir::W);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.at(1) == Pos{2, 2});
    CHECK(r.at(2) == Pos{3, 2});
    CHECK(r.s.robot(2).heading == Dir::W);  // pushed robots keep their heading
    CHECK(r.count("ROBOT_PUSHED") == 1);
}

TEST_CASE("3. three-robot push chain moves all three") {
    Rig r(kRoom, 3);
    r.place(1, 1, 2, Dir::E);
    r.place(2, 2, 2);
    r.place(3, 3, 2);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.at(1) == Pos{2, 2});
    CHECK(r.at(2) == Pos{3, 2});
    CHECK(r.at(3) == Pos{4, 2});
    CHECK(r.count("ROBOT_PUSHED") == 2);
}

TEST_CASE("3. a chain whose end is blocked fails with no partial pushes") {
    Rig r(kRoom, 3);
    r.place(1, 4, 2, Dir::E);
    r.place(2, 5, 2);
    r.place(3, 6, 2);  // (7,2) is a wall
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.at(1) == Pos{4, 2});
    CHECK(r.at(2) == Pos{5, 2});
    CHECK(r.at(3) == Pos{6, 2});
    CHECK(r.count("MOVE_BLOCKED") == 1);
    CHECK(r.count("ROBOT_PUSHED") == 0);
}

TEST_CASE("3. backing into a robot pushes it backwards") {
    Rig r(kRoom, 2);
    r.place(1, 3, 2, Dir::E);
    r.place(2, 2, 2, Dir::E);
    r.program(1, {Order::Back1});
    r.tick(1);
    CHECK(r.at(1) == Pos{2, 2});
    CHECK(r.at(2) == Pos{1, 2});
}

TEST_CASE("3. pushing a robot into a pit disables it and the push completes") {
    Rig r("# # # # # #\n# . . O . #\n# # # # # #\n", 3);
    r.place(1, 1, 1, Dir::E);
    r.place(2, 2, 1, Dir::W);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.at(1) == Pos{2, 1});
    CHECK(r.s.robot(2).respawn_pending);
    CHECK(r.s.robot(2).damage == 0);  // pit is a disable, not damage
    CHECK(r.count("ROBOT_DISABLED") == 1);
}

TEST_CASE("3. a chain pushing the far robot into a pit moves the whole chain") {
    Rig r("# # # # # # #\n# . . . O . #\n# # # # # # #\n", 3);
    r.place(1, 1, 1, Dir::E);
    r.place(2, 2, 1);
    r.place(3, 3, 1);
    r.program(1, {Order::Forward1});
    r.tick(1);
    CHECK(r.at(1) == Pos{2, 1});
    CHECK(r.at(2) == Pos{3, 1});
    CHECK(r.s.robot(3).respawn_pending);
}

TEST_CASE("3. an active robot walking into a pit is disabled at once") {
    Rig r("# # # # #\n# . O . #\n# # # # #\n", 1);
    r.place(1, 1, 1, Dir::E);
    r.program(1, {Order::Forward1, Order::Forward1, Order::Forward1});
    r.ticks(3);
    CHECK(r.s.robot(1).respawn_pending);
    CHECK(r.count("ORDER_SKIPPED_DISABLED") == 2);  // remaining registers skipped
}

TEST_CASE("4. priority rotates across four ticks for four players") {
    CHECK(priority_order(1, 4) == std::vector<int>{1, 2, 3, 4});
    CHECK(priority_order(2, 4) == std::vector<int>{2, 3, 4, 1});
    CHECK(priority_order(3, 4) == std::vector<int>{3, 4, 1, 2});
    CHECK(priority_order(4, 4) == std::vector<int>{4, 1, 2, 3});
    CHECK(priority_order(5, 4) == std::vector<int>{1, 2, 3, 4});
    CHECK(priority_order(2, 2) == std::vector<int>{2, 1});
    CHECK(priority_order(2, 3) == std::vector<int>{2, 3, 1});
}

TEST_CASE("4. the winner of a contested cell follows the rotating priority") {
    // Players 1 and 2 both move into (3,2). Only the higher-priority one gets it; the other
    // then pushes/bumps accordingly. Absolute tick 1 -> player 1 first, tick 2 -> player 2 first.
    auto run = [](int round, int tick) {
        Rig r(kRoom, 2);
        r.s.round = round;
        r.place(1, 2, 2, Dir::E);
        r.place(2, 4, 2, Dir::W);
        r.program(1, {Order::Forward1, Order::Forward1, Order::Forward1});
        r.program(2, {Order::Forward1, Order::Forward1, Order::Forward1});
        r.tick(tick);
        return std::make_pair(r.at(1), r.at(2));
    };
    // Tick index 1 (round 1, tick 1): P1 moves to (3,2), then P2 pushes P1 back to (2,2).
    auto a = run(1, 1);
    CHECK(a.first == Pos{2, 2});
    CHECK(a.second == Pos{3, 2});
    // Tick index 2 (round 1, tick 2): P2 moves first to (3,2), then P1 pushes P2 to (4,2).
    auto b = run(1, 2);
    CHECK(b.first == Pos{3, 2});
    CHECK(b.second == Pos{4, 2});
}

TEST_CASE("4. orders are resolved sequentially in priority order, observable in event order") {
    Rig r(kRoom, 4);
    r.s.round = 1;
    for (int p = 1; p <= 4; ++p) { r.place(p, 1 + p, 1 + (p % 2), Dir::S); r.program(p, {Order::Wait}); }
    r.tick(2);  // absolute tick 2 -> 2,3,4,1
    std::vector<int> seen;
    for (const auto& e : r.log.events())
        if (e.type == "ORDER_EXECUTED") seen.push_back(static_cast<int>(ev_param(e, "player").as_int()));
    CHECK(seen == std::vector<int>{2, 3, 4, 1});
}

// SDL-free parts of the graphical client: layout and the scrollable message log.
#include <string>
#include <vector>

#include "botched/map.hpp"
#include "doctest.h"
#include "ui_model.hpp"

using namespace botched;
using namespace botched::ui;

TEST_CASE("ui layout: panels fit the screen and never overlap, for any map size") {
    for (auto dims : {std::pair{12, 10}, std::pair{4, 4}, std::pair{30, 20}, std::pair{1, 1}}) {
        Layout l = compute_layout(dims.first, dims.second);
        const Rect screen{0, 0, kScreenW, kScreenH};
        std::vector<Rect> panels = {l.title, l.board_panel, l.scores, l.robot, l.orders, l.messages};
        for (const auto& p : panels) CHECK(p.inside(screen));
        for (std::size_t i = 0; i < panels.size(); ++i)
            for (std::size_t j = i + 1; j < panels.size(); ++j) CHECK_FALSE(panels[i].overlaps(panels[j]));
        CHECK(l.board.inside(l.board_panel));
        CHECK(l.queue.inside(l.board_panel));
        CHECK(l.skip_button.inside(l.board_panel));
        CHECK_FALSE(l.queue.overlaps(l.board));
        CHECK_FALSE(l.skip_button.overlaps(l.queue));
        CHECK(l.board.w == l.cell * dims.first);
        CHECK(l.board.h == l.cell * dims.second);
        CHECK(l.log_text.inside(l.messages));
        CHECK(l.scrollbar.inside(l.messages));
        for (const auto& s : l.slots) CHECK(s.inside(l.orders));
        for (const auto& b : l.palette) CHECK(b.inside(l.orders));
        CHECK(l.lock_button.inside(l.orders));
        CHECK(l.clear_button.inside(l.orders));
        CHECK_FALSE(l.lock_button.overlaps(l.clear_button));
        for (const auto& s : l.slots) CHECK_FALSE(s.overlaps(l.lock_button));
        for (std::size_t i = 0; i + 1 < l.palette.size(); ++i) CHECK_FALSE(l.palette[i].overlaps(l.palette[i + 1]));
    }
}

TEST_CASE("ui layout: the training-01 board is large enough to read") {
    Layout l = compute_layout(12, 10);
    CHECK(l.cell >= 48);  // still readable with the queue strip below the board
}

TEST_CASE("scroll log: follows the newest line until the reader scrolls up") {
    ScrollLog log;
    log.set_viewport(4);
    for (int i = 0; i < 10; ++i) log.add("line " + std::to_string(i));
    CHECK(log.following());
    CHECK(log.first_visible() == 6);
    CHECK(log.visible_count() == 4);
    CHECK(log.at(log.first_visible()).text == "line 6");

    log.scroll(3);  // read older lines
    CHECK_FALSE(log.following());
    CHECK(log.at(log.first_visible()).text == "line 3");
    log.add("line 10");  // new output must not move the view
    log.add("line 11");
    CHECK(log.at(log.first_visible()).text == "line 3");
    CHECK(log.unseen() == 2);

    log.to_bottom();
    CHECK(log.following());
    CHECK(log.unseen() == 0);
    CHECK(log.at(log.first_visible() + log.visible_count() - 1).text == "line 11");
}

TEST_CASE("scroll log: scrolling is clamped; short logs and resizes behave") {
    ScrollLog log;
    log.set_viewport(5);
    log.add("a");
    log.add("b");
    log.scroll(10);
    CHECK(log.following());  // fewer lines than the viewport: nothing to scroll
    CHECK(log.first_visible() == 0);
    CHECK(log.visible_count() == 2);
    for (int i = 0; i < 20; ++i) log.add("x" + std::to_string(i));
    log.scroll(1000);
    CHECK(log.offset() == log.max_offset());
    CHECK(log.first_visible() == 0);
    log.scroll(-1000);
    CHECK(log.following());
    log.to_top();
    log.set_viewport(30);  // window got taller than the history
    CHECK(log.following());
    CHECK(log.first_visible() == 0);
}

TEST_CASE("scroll log: bounded history drops the oldest lines and keeps the pinned view valid") {
    ScrollLog log(50);
    log.set_viewport(10);
    for (int i = 0; i < 200; ++i) log.add("n" + std::to_string(i));
    CHECK(log.size() == 50);
    CHECK(log.at(0).text == "n150");
    log.to_top();
    for (int i = 200; i < 260; ++i) log.add("n" + std::to_string(i));
    CHECK(log.size() == 50);
    CHECK(log.offset() <= log.max_offset());
    CHECK(log.visible_count() == 10);
}

TEST_CASE("ui: log lines are classified for colouring") {
    CHECK(classify_line("-- round 2, tick 1 --") == ScrollLog::Header);
    CHECK(classify_line("    P1 DELIVERED crate 3 (score 5)") == ScrollLog::Good);
    CHECK(classify_line("    P2 DISABLED (PIT)") == ScrollLog::Bad);
    CHECK(classify_line("  belt moves P1 (5,3) -> (6,3)   (they ordered WAIT)") == ScrollLog::Belt);
    CHECK(classify_line("  belt: P1 pushes P2 (6,3) -> (7,3)") == ScrollLog::Push);
    CHECK(classify_line("  P2 W: waits") == ScrollLog::Info);
}

// The planning-phase preview of the coming round's execution order (SDL strip under the board).
#include <memory>
#include <string>
#include <vector>

#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"
#include "map_fixture.hpp"
#include "ui_playback.hpp"

using namespace botched;

namespace {
std::shared_ptr<const Map> training() {
    MapLoad l = load_map("training-01", read_map_resource("training-01.map"));
    REQUIRE(l.ok());
    return std::make_shared<const Map>(std::move(*l.map));
}
}  // namespace

TEST_CASE("preview queue: the exact order of every tick, with machinery after each, for the round being planned") {
    // Rotating priority differs between rounds; the preview must equal what the engine then executes.
    Match m(MatchConfig{"pv", training(), 3});
    for (int round = 1; round <= 3; ++round) {
        const std::vector<ui::QueueEntry> preview = ui::build_preview_queue(m.state().round, 3, 2, {"FORWARD_1", "", "WAIT"});
        const int version = m.state_version();
        for (int p = 1; p <= 3; ++p) m.submit_program(p, m.state().round, 1, {"WAIT", "WAIT", "WAIT"}, true, "");
        m.run_round();
        Json events = Json::array();
        const auto evs = m.events_since(version);
        REQUIRE(evs.has_value());
        for (const auto& e : *evs) events.push(event_to_json(e, "pv"));
        const auto real = ui::build_queue(events);
        REQUIRE(preview.size() == 12);   // 3 ticks x (3 players + machinery)
        REQUIRE(real.size() == preview.size());
        for (std::size_t i = 0; i < real.size(); ++i) {
            CHECK(preview[i].machinery == real[i].machinery);
            CHECK(preview[i].tick == real[i].tick);
            CHECK(preview[i].player == real[i].player);
        }
        CHECK(ui::queue_text(preview, round) == ui::queue_text(real, round));
    }
}

TEST_CASE("preview queue: my orders (or empty slots) are shown, other players' orders stay hidden") {
    const auto q = ui::build_preview_queue(1, 3, 2, {"FORWARD_1", "", "TURN_LEFT"});
    int mine = 0, hidden = 0, machinery = 0;
    for (const auto& e : q) {
        CHECK(e.preview);
        if (e.machinery) { ++machinery; continue; }
        if (e.player == 2) {
            ++mine;
            CHECK_FALSE(e.hidden);
            CHECK(e.order == (e.tick == 1 ? "FORWARD_1" : e.tick == 2 ? "" : "TURN_LEFT"));
        } else {
            ++hidden;
            CHECK(e.hidden);
            CHECK(e.order.empty());   // nothing about another player's programme leaks in
        }
    }
    CHECK(mine == 3);
    CHECK(hidden == 6);
    CHECK(machinery == 3);
    CHECK(q.back().machinery);
}

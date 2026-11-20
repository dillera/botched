#include "botched/state.hpp"

#include <algorithm>

namespace botched {

Crate* GameState::crate(int id) {
    for (auto& c : crates) if (c.id == id) return &c;
    return nullptr;
}
const Crate* GameState::crate(int id) const {
    for (const auto& c : crates) if (c.id == id) return &c;
    return nullptr;
}
Group* GameState::group(char id) {
    for (auto& g : groups) if (g.id == id) return &g;
    return nullptr;
}
const Group* GameState::group(char id) const {
    for (const auto& g : groups) if (g.id == id) return &g;
    return nullptr;
}
Robot* GameState::robot_at(Pos p) {
    for (auto& r : robots) if (!r.respawn_pending && r.pos == p) return &r;
    return nullptr;
}
Crate* GameState::loose_crate_at(Pos p) {
    for (auto& c : crates) if (!c.carried && c.pos == p) return &c;
    return nullptr;
}
void GameState::remove_crate(int id) {
    crates.erase(std::remove_if(crates.begin(), crates.end(), [&](const Crate& c) { return c.id == id; }),
                 crates.end());
}

void EventLog::begin(int round, int tick) {
    if (round != round_ || tick != tick_) {
        round_ = round;
        tick_ = tick;
        seq_ = 0;
    }
}

void EventLog::emit(std::string type, Json params) {
    Event e;
    e.state_version = version_;
    e.round = round_;
    e.tick = tick_;
    e.sequence = ++seq_;
    e.type = std::move(type);
    e.params = std::move(params);
    events_.push_back(std::move(e));
}

std::vector<Event> EventLog::take() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

}  // namespace botched

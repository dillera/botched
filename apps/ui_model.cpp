#include "ui_model.hpp"

#include <algorithm>

namespace botched::ui {

Layout compute_layout(int map_w, int map_h) {
    Layout l;
    const int m = 8;
    l.title = {m, m, kScreenW - 2 * m, 48};
    l.board_panel = {m, 64, 840, 632};
    const int pad = 12, head = 28;  // panel caption row
    const int qh = 46;  // execution queue strip
    const int aw = l.board_panel.w - 2 * pad, ah = l.board_panel.h - 2 * pad - head - qh - 6;
    if (map_w < 1) map_w = 1;
    if (map_h < 1) map_h = 1;
    l.cell = std::max(8, std::min(aw / map_w, ah / map_h));
    const int bw = l.cell * map_w, bh = l.cell * map_h;
    l.board = {l.board_panel.x + (l.board_panel.w - bw) / 2, l.board_panel.y + head + pad + (ah - bh) / 2, bw, bh};

    l.queue = {l.board_panel.x + pad, l.board_panel.y + l.board_panel.h - pad - qh, l.board_panel.w - 2 * pad - 92, qh};
    l.skip_button = {l.queue.x + l.queue.w + 8, l.queue.y + 6, 76, 34};

    const int rx = l.board_panel.x + l.board_panel.w + m, rw = kScreenW - m - rx;
    l.scores = {rx, 64, rw, 250};
    l.robot = {rx, 322, rw, 106};
    l.orders = {rx, 436, rw, 260};
    l.messages = {m, 704, kScreenW - 2 * m, kScreenH - 704 - m};
    l.log_text = {l.messages.x + 10, l.messages.y + 34, l.messages.w - 10 - 26, l.messages.h - 34 - 8};
    l.scrollbar = {l.messages.x + l.messages.w - 18, l.log_text.y, 10, l.log_text.h};

    const int ox = l.orders.x + 14;
    for (int i = 0; i < 3; ++i) l.slots[static_cast<std::size_t>(i)] = {ox + i * 92, l.orders.y + 62, 84, 84};
    const int bwid = 48, gap = 4;
    for (int i = 0; i < kOrderKinds; ++i)
        l.palette[static_cast<std::size_t>(i)] = {ox + i * (bwid + gap), l.orders.y + 172, bwid, 56};
    l.lock_button = {ox + 3 * 92 + 8, l.orders.y + 62, l.orders.w - 14 - (3 * 92 + 8) - 14, 50};
    l.clear_button = {l.lock_button.x, l.orders.y + 62 + 58, l.lock_button.w, 26};
    l.start_button = {l.board_panel.x + l.board_panel.w / 2 - 110, l.board_panel.y + l.board_panel.h / 2 + 40, 220, 54};
    return l;
}

void ScrollLog::add(std::string text, Kind kind) {
    lines_.push_back({std::move(text), kind});
    if (offset_ > 0) ++offset_;  // keep the lines being read where they are
    ++unseen_;
    while (lines_.size() > capacity_) {
        lines_.pop_front();
        if (offset_ > 0) --offset_;
    }
    clamp();
}

int ScrollLog::max_offset() const {
    return std::max(0, static_cast<int>(lines_.size()) - viewport_);
}

void ScrollLog::clamp() {
    offset_ = std::max(0, std::min(offset_, max_offset()));
    if (offset_ == 0) unseen_ = 0;
    unseen_ = std::min(unseen_, static_cast<int>(lines_.size()));
}

void ScrollLog::scroll(int lines_up) {
    offset_ += lines_up;
    clamp();
}

std::size_t ScrollLog::first_visible() const {
    return static_cast<std::size_t>(std::max(0, static_cast<int>(lines_.size()) - viewport_ - offset_));
}

std::size_t ScrollLog::visible_count() const {
    return std::min<std::size_t>(static_cast<std::size_t>(viewport_), lines_.size() - first_visible());
}

ScrollLog::Kind classify_line(const std::string& l) {
    auto has = [&](const char* s) { return l.find(s) != std::string::npos; };
    if (l.rfind("-- ", 0) == 0) return ScrollLog::Header;
    if (has("DELIVERED") || has("picks up") || has("respawned")) return ScrollLog::Good;
    if (has("DISABLED") || has("laser") || has("destroyed") || has("blocked") || has("nothing happened")) return ScrollLog::Bad;
    if (has("pushes")) return ScrollLog::Push;
    if (has("belt")) return ScrollLog::Belt;
    return ScrollLog::Info;
}

}  // namespace botched::ui

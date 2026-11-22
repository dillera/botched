// SDL-free parts of the graphical client: screen layout and the scrollable message log.
// Kept separate so they can be unit-tested without a display.
#pragma once

#include <array>
#include <deque>
#include <string>

namespace botched::ui {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    bool inside(const Rect& o) const { return x >= o.x && y >= o.y && x + w <= o.x + o.w && y + h <= o.y + o.h; }
    bool overlaps(const Rect& o) const { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; }
};

inline constexpr int kScreenW = 1360;
inline constexpr int kScreenH = 900;
inline constexpr int kOrderKinds = 9;

// Fixed logical layout (the window scales it). The board, the player panels and the message log
// never overlap, so a long log can never push the board off screen.
struct Layout {
    Rect title;
    Rect board_panel;   // frame around the board
    Rect board;         // the grid itself (cells are square, centred in the panel)
    int cell = 0;
    Rect scores;        // scoreboard panel
    Rect robot;         // "your robot" status panel
    Rect orders;        // order entry panel
    Rect messages;      // scrollable log frame
    Rect log_text;      // text viewport inside the frame
    Rect scrollbar;
    std::array<Rect, 3> slots;
    std::array<Rect, kOrderKinds> palette;
    Rect lock_button;
    Rect clear_button;
    Rect start_button;  // lobby only (drawn over the board panel)
    Rect queue;         // execution queue strip, bottom of the board panel
    Rect skip_button;   // skips round playback
};

// The right-hand column (scoreboard / robot status / orders) is sized to its content: the scoreboard by the number
// of players, the robot panel by how many status lines it prints, and all three end on the board panel's bottom
// edge so the message log sits cleanly underneath.
inline constexpr int kRobotLineH = 16;
inline int robot_panel_lines(bool has_traps, bool has_turrets) { return 4 + (has_traps ? 1 : 0) + (has_turrets ? 1 : 0); }
Layout compute_layout(int map_w, int map_h, int players = 4, int robot_lines = 6);

// Append-only log with a bounded history and a scroll position. offset 0 means "following the
// newest line"; scrolling up pins the view so arriving lines do not move what you are reading.
class ScrollLog {
public:
    enum Kind { Info, Header, Belt, Push, Good, Bad, Notice };
    struct Line {
        std::string text;
        Kind kind = Info;
    };

    explicit ScrollLog(std::size_t capacity = 2000) : capacity_(capacity) {}
    void add(std::string text, Kind kind = Info);
    void set_viewport(int lines) { viewport_ = lines < 1 ? 1 : lines; clamp(); }
    void scroll(int lines_up);  // positive = towards older lines
    void to_bottom() { offset_ = 0; unseen_ = 0; }
    void to_top() { offset_ = max_offset(); }

    std::size_t size() const { return lines_.size(); }
    const Line& at(std::size_t i) const { return lines_[i]; }
    std::size_t first_visible() const;
    std::size_t visible_count() const;
    int viewport() const { return viewport_; }
    bool following() const { return offset_ == 0; }
    int unseen() const { return unseen_; }
    int max_offset() const;
    int offset() const { return offset_; }

private:
    void clamp();
    std::deque<Line> lines_;
    std::size_t capacity_;
    int viewport_ = 8;
    int offset_ = 0;
    int unseen_ = 0;
};

// Classifies a summarize_events() line for colouring.
ScrollLog::Kind classify_line(const std::string& line);

}  // namespace botched::ui

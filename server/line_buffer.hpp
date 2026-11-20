// Incremental line splitter for byte streams (TCP). Bounded: a line longer than max_line is
// discarded up to its newline and reported once as too_long, never stored.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace botched::server {

class LineBuffer {
public:
    struct Line {
        std::string text;     // without '\n' and a trailing '\r'
        bool too_long = false;  // the line exceeded max_line; text is empty
    };

    explicit LineBuffer(std::size_t max_line) : max_(max_line) {}

    // Appends every line completed by `data` to `out`.
    void feed(const char* data, std::size_t n, std::vector<Line>& out) {
        for (std::size_t i = 0; i < n; ++i) {
            const char c = data[i];
            if (c == '\n') {
                if (discarding_) {
                    out.push_back({std::string(), true});
                    discarding_ = false;
                } else {
                    if (!buf_.empty() && buf_.back() == '\r') buf_.pop_back();
                    out.push_back({std::move(buf_), false});
                }
                buf_.clear();
            } else if (!discarding_) {
                if (buf_.size() >= max_) {
                    discarding_ = true;
                    buf_.clear();
                } else {
                    buf_.push_back(c);
                }
            }
        }
    }

    std::size_t pending() const { return buf_.size(); }

private:
    std::size_t max_;
    std::string buf_;
    bool discarding_ = false;
};

}  // namespace botched::server

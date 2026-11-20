// Bounded line input for hosts that read JSON lines (the stdio reference host, and any
// future byte-stream adapter). std::getline would allocate without limit for a peer that
// never sends a newline.
#pragma once

#include <cstddef>
#include <istream>
#include <string>

namespace botched::server {

enum class LineStatus {
    Ok,       // `line` holds one complete line (without '\n' or a trailing '\r')
    TooLong,  // line exceeded max_bytes: it was discarded up to its newline; `line` is empty
    Eof,      // no more input
};

// Reads at most max_bytes of a line into `line`. An over-long line is consumed (so the next
// call starts at the next line) but never stored beyond max_bytes: memory use is bounded.
inline LineStatus read_line_bounded(std::istream& in, std::string& line, std::size_t max_bytes) {
    line.clear();
    bool any = false;
    bool too_long = false;
    for (;;) {
        int c = in.rdbuf()->sbumpc();
        if (c == std::char_traits<char>::eof()) {
            if (!any) return LineStatus::Eof;
            break;
        }
        any = true;
        if (c == '\n') break;
        if (too_long) continue;
        if (line.size() >= max_bytes) {
            too_long = true;
            line.clear();
            continue;
        }
        line.push_back(static_cast<char>(c));
    }
    if (too_long) { line.clear(); return LineStatus::TooLong; }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return LineStatus::Ok;
}

}  // namespace botched::server

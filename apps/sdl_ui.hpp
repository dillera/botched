// Graphical (SDL2) client: the same network protocol and player flow as the terminal client,
// presented as a board with drawn sprites, scoreboard panels and a scrollable message log.
#pragma once

#include "client_core.hpp"

namespace botched::ui {

// Runs a whole session in a window. Returns when the window closes or the match result has been
// dismissed. `result.ok` is true when the match ran to the end.
client::ClientResult run_ui(const client::ClientOptions& opt);

}  // namespace botched::ui

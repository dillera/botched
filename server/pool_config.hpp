#pragma once
#include "bot.hpp"
#include <optional>
#include <string_view>

namespace botched::server {
struct Offering {
  std::string id, name;
  int instances = 0;
  Json rules; // existing CREATE_MATCH fields; capacity may be inferred from map
              // metadata
  std::vector<sim::NamedBot> bots;
};
struct PoolConfig {
  std::optional<int> max_matches, planning_seconds;
  std::vector<Offering> offerings;
};
// Configuration-only decimal support; the network Json parser remains
// integer-only. Throws invalid_argument with a field path. Does not create
// rooms or mutate a server.
PoolConfig parse_pool_config(std::string_view);
PoolConfig load_pool_config(const std::string &path);
} // namespace botched::server

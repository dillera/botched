#include "pool_config.hpp"
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace botched::server {
namespace {
[[noreturn]] void bad(const std::string &path, const std::string &why) {
  throw std::invalid_argument(path + ": " + why);
}
// Preserve JSON shape/string/duplicate-key validation in the existing parser.
// Replace numeric lexemes OUTSIDE strings with indices into a private table.
// Numbers remain numbers (a user-supplied string can never impersonate one).
struct Document {
  Json root;
  std::vector<std::string> numbers;
  explicit Document(std::string_view text) {
    if (text.size() > (1u << 20))
      bad("config", "file exceeds 1 MiB");
    std::string shape;
    for (size_t i = 0; i < text.size();) {
      char c = text[i];
      if (c == '"') {
        shape += text[i++];
        while (i < text.size()) {
          c = text[i++];
          shape += c;
          if (c == '\\' && i < text.size())
            shape += text[i++];
          else if (c == '"')
            break;
        }
      } else if (c == '-' || (c >= '0' && c <= '9')) {
        size_t start = i;
        if (text[i] == '-')
          ++i;
        auto digit = [&] {
          return i < text.size() && text[i] >= '0' && text[i] <= '9';
        };
        if (!digit())
          bad("config", "invalid number");
        if (text[i] == '0')
          ++i;
        else
          while (digit())
            ++i;
        if (i < text.size() && text[i] == '.') {
          ++i;
          if (!digit())
            bad("config", "invalid decimal");
          while (digit())
            ++i;
        }
        if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
          ++i;
          if (i < text.size() && (text[i] == '+' || text[i] == '-'))
            ++i;
          if (!digit())
            bad("config", "invalid exponent");
          while (digit())
            ++i;
        }
        // Do not turn malformed adjacent numbers (01, 1-2) into one index.
        if (i < text.size() && text[i] != ',' && text[i] != ']' &&
            text[i] != '}' && text[i] != ' ' && text[i] != '\t' &&
            text[i] != '\n' && text[i] != '\r')
          bad("config", "invalid numeric delimiter");
        shape += std::to_string(numbers.size());
        numbers.emplace_back(text.substr(start, i - start));
      } else {
        shape += c;
        ++i;
      }
    }
    auto parsed = Json::parse(shape);
    if (!parsed)
      bad("config",
          "invalid JSON (duplicate keys and non-ASCII strings are rejected)");
    root = std::move(*parsed);
  }
  const std::string &lexeme(const Json &j, const std::string &path) const {
    if (!j.is_int() || j.as_int() < 0 || size_t(j.as_int()) >= numbers.size())
      bad(path, "expected number");
    return numbers[size_t(j.as_int())];
  }
  int integer(const Json &j, const std::string &path, int lo, int hi) const {
    const auto &s = lexeme(j, path);
    int n;
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size() || n < lo || n > hi)
      bad(path, "expected integer in [" + std::to_string(lo) + "," +
                    std::to_string(hi) + "]");
    return n;
  }
  double weight(const Json &j, const std::string &path) const {
    const auto &s = lexeme(j, path);
    double n;
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size() ||
        !std::isfinite(n) || n < 0 || n > 1)
      bad(path, "expected finite weight in [0,1]");
    return n;
  }
};
void fields(const Json &j, const std::string &path,
            std::initializer_list<std::string_view> allowed) {
  if (!j.is_object())
    bad(path, "expected object");
  for (const auto &[k, v] : j.members()) {
    (void)v;
    bool known = false;
    for (auto a : allowed)
      if (k == a)
        known = true;
    if (!known)
      bad(path + "." + k, "unknown field");
  }
}
const Json &required(const Json &j, const char *key, std::string_view path) {
  auto v = j.find(key);
  if (!v)
    bad(std::string(path) + "." + key, "required field");
  return *v;
}
std::string name(const Json &j, const std::string &path, bool token = false) {
  if (!j.is_string() || j.as_string().empty() || j.as_string().size() > 32)
    bad(path, "expected 1-32 printable ASCII characters");
  for (unsigned char c : j.as_string()) {
    bool valid = c >= 32 && c <= 126;
    if (token)
      valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' ||
              c == '-';
    if (!valid)
      bad(path, "invalid character");
  }
  return j.as_string();
}
} // namespace
PoolConfig parse_pool_config(std::string_view text) {
  Document d(text);
  PoolConfig out;
  fields(d.root, "config", {"server", "offerings"});
  if (auto s = d.root.find("server")) {
    fields(*s, "server",
           {"max_concurrent_matches", "default_planning_seconds", "finished_retention_seconds"});
    if (auto v = s->find("max_concurrent_matches"))
      out.max_matches = d.integer(*v, "server.max_concurrent_matches", 1,
                                  std::numeric_limits<int>::max());
    if (auto v = s->find("finished_retention_seconds"))
      out.finished_retention_seconds = d.integer(*v, "server.finished_retention_seconds", 1, 86400);
    if (auto v = s->find("default_planning_seconds"))
      out.planning_seconds =
          d.integer(*v, "server.default_planning_seconds", 0, 86400);
  }
  const auto &offerings = required(d.root, "offerings", "config");
  if (!offerings.is_array())
    bad("offerings", "expected array");
  std::set<std::string> ids;
  for (size_t i = 0; i < offerings.items().size(); ++i) {
    const auto &o = offerings.items()[i];
    std::string path = "offerings[" + std::to_string(i) + "]";
    fields(o, path,
           {"id", "name", "map_id", "instances", "player_count", "bots",
            "rounds", "target_score", "planning_seconds"});
    Offering item;
    item.id = name(required(o, "id", path), path + ".id", true);
    if (!ids.insert(item.id).second)
      bad(path + ".id", "duplicate offering ID");
    item.name = name(required(o, "name", path), path + ".name");
    item.instances =
        d.integer(required(o, "instances", path), path + ".instances", 1,
                  std::numeric_limits<int>::max());
    item.rules = Json::object();
    item.rules.set("type", "CREATE_MATCH");
    item.rules.set("map_id",
                   name(required(o, "map_id", path), path + ".map_id", true));
    // Use CREATE_MATCH's validator for actual rule ranges, map compatibility
    // and defaults.
    for (auto key :
         {"player_count", "rounds", "target_score", "planning_seconds"})
      if (auto v = o.find(key))
        item.rules.set(key, d.integer(*v, path + "." + key, 0,
                                      std::numeric_limits<int>::max()));
    const auto &bots = required(o, "bots", path);
    if (!bots.is_array() || bots.items().size() > 3)
      bad(path + ".bots", "expected array of at most three named bots");
    std::set<std::string> names;
    for (size_t b = 0; b < bots.items().size(); ++b) {
      auto bp = path + ".bots[" + std::to_string(b) + "]";
      const auto &bot = bots.items()[b];
      fields(bot, bp, {"name", "personality"});
      auto n = name(required(bot, "name", bp), bp + ".name");
      if (!names.insert(n).second)
        bad(bp + ".name", "duplicate bot name");
      const auto &blend = required(bot, "personality", bp);
      fields(blend, bp + ".personality", {"hauler", "opportunist", "saboteur"});
      std::array<double, 3> weights{};
      int k = 0;
      for (auto key : {"hauler", "opportunist", "saboteur"})
        weights[k++] = d.weight(required(blend, key, bp + ".personality"),
                                bp + ".personality." + key);
      try {
        item.bots.push_back({n, sim::PersonalityBlend::from_weights(weights)});
      } catch (const std::invalid_argument &e) {
        bad(bp + ".personality", e.what());
      }
    }
    item.rules.set("bots", int(item.bots.size()));
    out.offerings.push_back(std::move(item));
  }
  return out;
}
PoolConfig load_pool_config(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    bad(path, "cannot open configuration");
  std::string text;
  char block[4096];
  while (in.read(block, sizeof block) || in.gcount()) {
    text.append(block, size_t(in.gcount()));
    if (text.size() > (1u << 20))
      bad(path, "file exceeds 1 MiB");
  }
  if (!in.eof())
    bad(path, "configuration read failed");
  return parse_pool_config(text);
}
} // namespace botched::server

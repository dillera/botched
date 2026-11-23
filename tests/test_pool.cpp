#include "binary_bridge.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"
#include "pool_config.hpp"
#include "server.hpp"
#include <algorithm>
#include <limits>
#include <sstream>
using namespace botched;
using namespace botched::server;
namespace {
const char *base =
    R"({"server":{"max_concurrent_matches":4},"offerings":[{"id":"named","name":"Named bots","map_id":"foundry-01","player_count":4,"instances":2,"bots":[{"name":"Nasty","personality":{"hauler":0,"opportunist":0,"saboteur":1}},{"name":"Sneaky Pete","personality":{"hauler":1,"opportunist":0,"saboteur":0}},{"name":"Bob","personality":{"hauler":0,"opportunist":1,"saboteur":0}}],"rounds":3,"planning_seconds":0},{"id":"duel","name":"Human duel","map_id":"training-01","player_count":2,"instances":1,"bots":[],"rounds":2,"planning_seconds":0}]})";
Json query(const char *type, const std::string &id = "") {
  auto q = Json::object();
  q.set("type", type);
  if (!id.empty())
    q.set("match_id", id);
  return q;
}
Json &member(Json &o, const std::string &name) {
  for (auto &[k, v] : o.members())
    if (k == name)
      return v;
  throw std::runtime_error(name);
}
void erase(Json &o, const std::string &key) {
  auto &v = o.members();
  v.erase(std::remove_if(v.begin(), v.end(),
                         [&](auto &x) { return x.first == key; }),
          v.end());
}
Json &first(Json &root) { return member(root, "offerings").items()[0]; }
Json &bot(Json &root) { return member(first(root), "bots").items()[0]; }
void provision(GameServer &s, const PoolConfig &p, std::ostringstream &report) {
  std::string error;
  INFO(error);
  REQUIRE(s.provision_pool(p.offerings, report, error));
}
} // namespace
TEST_CASE(
    "S1 configuration schema rejects malformed bot identities and weights") {
  std::vector<std::function<void(Json &)>> edits = {
      [](Json &j) { first(j).set("instances", 0); },
      [](Json &j) { first(j).set("instances", -1); },
      [](Json &j) { first(j).set("instances", true); },
      [](Json &j) { first(j).set("bots", 3); },
      [](Json &j) { first(j).set("bots", Json::object()); },
      [](Json &j) { erase(bot(j), "name"); },
      [](Json &j) { bot(j).set("name", ""); },
      [](Json &j) { bot(j).set("name", std::string(33, 'x')); },
      [](Json &j) { bot(j).set("name", "line\nfeed"); },
      [](Json &j) { bot(j).set("name", false); },
      [](Json &j) { bot(j).set("name", "Bob"); },
      [](Json &j) { erase(bot(j), "personality"); },
      [](Json &j) { member(bot(j), "personality").set("hauler", -1); },
      [](Json &j) { member(bot(j), "personality").set("hauler", 2); },
      [](Json &j) { member(bot(j), "personality").set("hauler", "0"); },
      [](Json &j) { member(bot(j), "personality").set("hauler", false); },
      [](Json &j) { member(bot(j), "personality").set("hauler", 1); },
      [](Json &j) { member(bot(j), "personality").set("surprise", 0); },
      [](Json &j) { erase(member(bot(j), "personality"), "hauler"); },
      [](Json &j) { bot(j).set("personality", Json::array()); },
      [](Json &j) { member(j, "offerings").items()[1].set("id", "named"); },
      [](Json &j) { first(j).set("id", "bad id"); },
      [](Json &j) { first(j).set("name", ""); },
      [](Json &j) { first(j).set("recycle", true); },
      [](Json &j) { j.set("lobby", true); },
      [](Json &j) { member(j, "server").set("max_concurrent_matches", 0); },
      [](Json &j) {
        member(j, "server").set("default_planning_seconds", 86401);
      }};
  for (auto &edit : edits) {
    auto j = *Json::parse(base);
    edit(j);
    CHECK_THROWS_AS(parse_pool_config(j.dump()), std::invalid_argument);
  }
  for (auto number : {"NaN", "Infinity", "-Infinity", "1e9999", "0.5", "-0.1",
                      "01", "1.", "1e", "1e-9999"}) {
    std::string text = base;
    auto pos = text.find("\"saboteur\":1");
    text.replace(pos, 12, std::string("\"saboteur\":") + number);
    CHECK_THROWS_AS(parse_pool_config(text), std::invalid_argument);
  }
  CHECK_THROWS_AS(parse_pool_config("{\"offerings\":[],\"offerings\":[]}"),
                  std::invalid_argument);
  CHECK(parse_pool_config("{\"offerings\":[]}").offerings.empty());
  CHECK_THROWS_AS(parse_pool_config(std::string((1u << 20) + 1, ' ')),
                  std::invalid_argument);
  // Decimal/exponent support is configuration-only; the wire JSON contract
  // stays integer-only.
  CHECK_FALSE(Json::parse("{\"weight\":0.5}"));
  std::string text = base;
  auto pos = text.find("\"saboteur\":1");
  text.replace(pos, 12, "\"saboteur\":1e0");
  CHECK(parse_pool_config(text).offerings[0].bots[0].blend.millionths[2] ==
        1000000);
}
TEST_CASE("S1 entire pool is validated before allocating room IDs or reporting "
          "entries") {
  std::vector<std::function<void(Json &)>> edits = {
      [](Json &j) {
        member(j, "offerings").items()[1].set("map_id", "missing");
      },
      [](Json &j) { erase(member(j, "offerings").items()[1], "player_count"); },
      [](Json &j) { first(j).set("player_count", 1); },
      [](Json &j) { first(j).set("player_count", 5); },
      [](Json &j) { first(j).set("player_count", 3); },
      [](Json &j) { first(j).set("rounds", 0); },
      [](Json &j) { first(j).set("rounds", 201); },
      [](Json &j) { first(j).set("target_score", 1001); },
      [](Json &j) { first(j).set("planning_seconds", 86401); },
      [](Json &j) { first(j).set("instances", 4); }};
  for (auto &edit : edits) {
    auto j = *Json::parse(base);
    edit(j);
    auto p = parse_pool_config(j.dump());
    ServerConfig cfg;
    cfg.max_matches = 4;
    ManualClock clock;
    GameServer s(cfg, clock);
    std::ostringstream log;
    std::string error;
    CHECK_FALSE(s.provision_pool(p.offerings, log, error));
    CHECK_FALSE(error.empty());
    CHECK(log.str().empty());
    CHECK(s.handle(query("CREATE_MATCH")).get_string("match_id") == "m1");
  }
}
TEST_CASE("S1 deterministic blend rounding and pure archetype decisions") {
  using namespace sim;
  CHECK(PersonalityBlend::from_weights({.2, .3, .5}).traits() ==
        Traits{41, 25, 34, 93, 132, 11, 9, 6});
  CHECK(PersonalityBlend::from_weights({.9, .1, 0}).traits() ==
        Traits{77, 17, 6, 136, 64, 6, 6, 8});
  CHECK(PersonalityBlend::from_weights({.5, 0, .5}).traits() ==
        Traits{50, 20, 30, 105, 120, 10, 8, 7});
  CHECK(PersonalityBlend::from_weights({1. / 3, 1. / 3, 1. / 3}).traits() ==
        Traits{50, 23, 27, 103, 113, 10, 8, 7});
  CHECK_THROWS(PersonalityBlend::from_weights(
      {std::numeric_limits<double>::quiet_NaN(), 0, 1}));
  CHECK_THROWS(PersonalityBlend::from_weights(
      {std::numeric_limits<double>::infinity(), 0, 1}));
  CHECK_NOTHROW(PersonalityBlend::from_weights({.2, .3, .5000005}));
  CHECK_THROWS(PersonalityBlend::from_weights({.2, .3, .500002}));
  ManualClock clock;
  GameServer server(ServerConfig{}, clock);
  auto map = server.registered_map("foundry-01");
  REQUIRE(map);
  for (unsigned p = 0; p < 3; ++p)
    for (unsigned seed : {1u, 7u, 111u}) {
      std::array<double, 3> weights{};
      weights[p] = 1;
      auto blend = PersonalityBlend::from_weights(weights);
      CHECK(blend.traits() == traits_of(Personality(p)));
      MatchConfig cfg;
      cfg.map = std::make_shared<Map>(*map);
      cfg.player_count = 4;
      cfg.rounds = 6;
      cfg.match_id = "pure";
      Match match(cfg);
      Bot legacy(1, *map, seed, Personality(p)),
          named(1, *map, seed, {"Named", blend});
      CHECK(named.personality() == Personality(p));
      for (int round = 1; round <= 6; ++round) {
        auto a = legacy.plan(match.snapshot()),
             b = named.plan(match.snapshot());
        CHECK(a == b);
        CHECK(legacy.goal_kind() == named.goal_kind());
        CHECK(legacy.goals_chosen() == named.goals_chosen());
        match.submit_program(1, round, 1, {a[0], a[1], a[2]}, true,
                             "r" + std::to_string(round));
        match.run_round();
      }
    }
  CHECK(PersonalityBlend::from_weights({.2, .3, .5}).dominant() == Personality::Saboteur);
  CHECK(PersonalityBlend::from_weights({.5, .5, 0}).dominant() == Personality::Hauler);
  CHECK(PersonalityBlend::from_weights({0, .5, .5}).dominant() == Personality::Opportunist);
  CHECK(PersonalityBlend::from_weights({.5, 0, .5}).dominant() == Personality::Hauler);
  CHECK(PersonalityBlend::from_weights({1./3, 1./3, 1./3}).dominant() == Personality::Hauler);
  // Equal quantized traits, different dominant original weights: metadata must
  // neither round away the winner nor affect planning/RNG consumption.
  auto blend = PersonalityBlend::from_weights({.5000001, .4999999, 0});
  auto other = PersonalityBlend::from_weights({.4999999, .5000001, 0});
  CHECK(blend.millionths == other.millionths);
  Bot a(1, *map, 42, {"Mixed", blend}), b(1, *map, 42, {"Mixed", other});
  CHECK(a.personality() == Personality::Hauler);
  CHECK(b.personality() == Personality::Opportunist);
  CHECK(other.describe().find("weights(H/O/S)=0.4999999/0.5000001/0") != std::string::npos);
  MatchConfig cfg;
  cfg.map = std::make_shared<Map>(*map);
  cfg.player_count = 4;
  cfg.rounds = 8;
  cfg.match_id = "mixed";
  Match m(cfg);
  for (int r = 1; r <= 8; ++r) {
    auto orders = a.plan(m.snapshot());
    CHECK(orders == b.plan(m.snapshot()));
    CHECK(a.traits() == blend.traits());
    CHECK(a.traits() == b.traits());
    CHECK(a.goal_kind() == b.goal_kind());
    CHECK(a.goals_chosen() == b.goals_chosen());
    m.submit_program(1, r, 1, {orders[0], orders[1], orders[2]}, true,
                     std::to_string(r));
    m.run_round();
  }
  CHECK(a.describe().find("Mixed") != std::string::npos);
  CHECK(a.describe().find("blend(H/O/S") != std::string::npos);
}
TEST_CASE("S1 configured rooms wait, isolate instances, retain results and "
          "share the cap") {
  auto p = parse_pool_config(base);
  ServerConfig cfg;
  cfg.max_matches = 4;
  std::ostringstream bots, trace;
  cfg.bot_log = &bots;
  cfg.trace = &trace;
  ManualClock clock;
  GameServer s(cfg, clock);
  std::ostringstream report;
  provision(s, p, report);
  CHECK(report.str().find("Sneaky Pete") != std::string::npos);
  CHECK(report.str().find("no recycling") != std::string::npos);
  for (auto id : {"m1", "m2", "m3"})
    CHECK(s.handle(query("GET_MATCH_STATUS", id))
              .find("status")
              ->get_string("phase") == "LOBBY");
  clock.advance_ms(1000000);
  s.poll();
  CHECK_FALSE(s.find_match("m1"));
  auto first = s.handle(query("JOIN_MATCH", "m3"));
  CHECK(first.get_bool("started") == false);
  CHECK_FALSE(s.handle(query("START_MATCH", "m3")).get_bool("ok").value());
  auto second = s.handle(query("JOIN_MATCH", "m3"));
  CHECK(second.get_bool("started") == true);
  CHECK(first.get_string("session") != second.get_string("session"));
  auto named = s.handle(query("JOIN_MATCH", "m1"));
  CHECK(named.get_bool("started") == true);
  CHECK(named.get_int("player_id") == 1);
  CHECK_FALSE(s.find_match("m2"));
  CHECK(s.find_match("m1")->state().round == 1);
  CHECK(bots.str().find("P2 name=\"Nasty\"") != std::string::npos);
  CHECK(bots.str().find("P3 name=\"Sneaky Pete\"") != std::string::npos);
  CHECK(bots.str().find("P4 name=\"Bob\"") != std::string::npos);
  CHECK(trace.str().find("blend(H/O/S") != std::string::npos);
  for (int round = 1; round <= 3; ++round) {
    auto q = query("SUBMIT_PROGRAM", "m1");
    q.set("player_id", 1);
    q.set("session", *named.find("session"));
    q.set("round", round);
    q.set("revision", 1);
    q.set("ready", true);
    q.set("request_id", std::to_string(round));
    auto orders = Json::array();
    for (int i = 0; i < 3; ++i)
      orders.push("WAIT");
    q.set("orders", orders);
    CHECK(s.handle(q).get_bool("ok") == true);
  }
  CHECK(s.find_match("m1")->finished());
  auto finished = s.handle(query("GET_SNAPSHOT", "m1"));
  clock.advance_ms(999999);
  s.poll();
  CHECK(s.handle(query("GET_SNAPSHOT", "m1")) == finished);
  CHECK(s.handle(query("CREATE_MATCH")).get_string("match_id") == "m4");
  CHECK(s.handle(query("CREATE_MATCH")).get_string("code") ==
        "TOO_MANY_MATCHES");
  CHECK_FALSE(s.find_match("m2"));
  // Client status retains only the old fields; no configured identity escapes.
  auto status = s.handle(query("GET_MATCH_STATUS", "m1"));
  CHECK(status.dump().find("Nasty") == std::string::npos);
  CHECK(status.dump().find("blend") == std::string::npos);
}

TEST_CASE("S1 named definitions are copied per instance with independent "
          "credentials and bot state") {
  auto pool = parse_pool_config(base);
  ServerConfig cfg;
  cfg.deterministic_token_seed = 42;
  ManualClock clock;
  GameServer server(cfg, clock);
  std::ostringstream report;
  provision(server, pool, report);
  auto a = server.handle(query("JOIN_MATCH", "m1"));
  auto b = server.handle(query("JOIN_MATCH", "m2"));
  REQUIRE(a.get_bool("started") == true);
  REQUIRE(b.get_bool("started") == true);
  CHECK(a.get_string("session") != b.get_string("session"));
  auto untouched = server.handle(query("GET_SNAPSHOT", "m2"));
  auto q = query("SUBMIT_PROGRAM", "m1");
  q.set("player_id", 1);
  q.set("session", *a.find("session"));
  q.set("round", 1);
  q.set("revision", 1);
  q.set("ready", true);
  q.set("request_id", "first");
  Json orders = Json::array();
  for (int i = 0; i < 3; ++i)
    orders.push("WAIT");
  q.set("orders", orders);
  REQUIRE(server.handle(q).get_bool("ok") == true);
  CHECK(server.find_match("m1")->state().round == 2);
  CHECK(server.handle(query("GET_SNAPSHOT", "m2")) == untouched);
  std::istringstream lines(report.str());
  std::vector<Json> records;
  for (std::string line; std::getline(lines, line);)
    if (line.starts_with("pool {"))
      records.push_back(*Json::parse(line.substr(5)));
  REQUIRE(records.size() == 3);
  CHECK(*records[0].find("bot_names") == *records[1].find("bot_names"));
  CHECK(*records[0].find("bot_blends") == *records[1].find("bot_blends"));
}

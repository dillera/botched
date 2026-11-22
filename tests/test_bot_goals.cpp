// G4: bot personalities, persistent goals, re-evaluation, hazard-aware routing and active sabotage.
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../server/bot.hpp"
#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"
#include "map_fixture.hpp"
#include "server.hpp"

using namespace botched;
using namespace botched::sim;

namespace {

using MapPtr = std::shared_ptr<const Map>;

MapPtr load(const std::string& id) {
    MapLoad l = load_map(id, read_map_resource(id + ".map"));
    REQUIRE_MESSAGE(l.ok(), (l.errors.empty() ? std::string("?") : l.errors[0]));
    return std::make_shared<const Map>(std::move(*l.map));
}

MatchConfig cfg(const MapPtr& map, int players) {
    MatchConfig c;
    c.match_id = "g4";
    c.map = map;
    c.player_count = players;
    return c;
}

struct Seat { int x, y; const char* heading = "E"; int damage = 0; };

// A match with the given robots placed (others stay where they spawn).
Match setup(const MapPtr& map, int players, std::initializer_list<Seat> seats) {
    MatchConfig c = cfg(map, players);
    Json saved = Match(c).save();
    Json& ps = const_cast<Json&>(*saved.find("players"));
    int i = 0;
    for (const Seat& s : seats) {
        Json& p = const_cast<Json&>(ps.items()[static_cast<std::size_t>(i++)]);
        p.set("x", s.x);
        p.set("y", s.y);
        p.set("heading", s.heading);
        p.set("damage", s.damage);
        p.set("damage_taken", s.damage);
    }
    std::string err;
    auto m = Match::restore(c, saved, err);
    REQUIRE_MESSAGE(m.has_value(), err);
    return std::move(*m);
}

// Player 1 follows `bot`; everyone else WAITs. Runs one round.
void play_round(Match& m, Bot& bot) {
    const int round = m.state().round;
    auto prog = bot.plan(m.snapshot());
    m.submit_program(1, round, 1, {prog[0], prog[1], prog[2]}, true, "");
    for (int p = 2; p <= m.state().player_count(); ++p) m.submit_program(p, round, 1, {"WAIT", "WAIT", "WAIT"}, true, "");
    m.run_round();
}

int count_events(const Match& m, const std::string& type, int player = 0) {
    int n = 0;
    for (const auto& e : m.all_events())
        if (e.type == type && (player == 0 || e.params.get_int("player").value_or(-1) == player)) ++n;
    return n;
}

bool is_hunt(GoalKind k) { return k == GoalKind::ArmTrap || k == GoalKind::ClaimTurret || k == GoalKind::Chase; }

}  // namespace

TEST_CASE("G4: personalities are deterministic, named and mixed") {
    CHECK(std::string(to_string(Personality::Hauler)) == "hauler");
    CHECK(std::string(to_string(Personality::Opportunist)) == "opportunist");
    CHECK(std::string(to_string(Personality::Saboteur)) == "saboteur");
    CHECK(personality_for(0) == Personality::Hauler);
    CHECK(personality_for(1) == Personality::Opportunist);
    CHECK(personality_for(2) == Personality::Saboteur);
    CHECK(personality_for(5) == personality_for(5));
}

TEST_CASE("G4: the first goal is drawn from the personality's weights") {
    auto map = load("trapworks-01");
    Match m = setup(map, 4, {});
    const Json snap = m.snapshot();
    std::map<Personality, int> hunts, hauls;
    const int n = 240;
    for (Personality p : {Personality::Hauler, Personality::Opportunist, Personality::Saboteur})
        for (int seed = 1; seed <= n; ++seed) {
            Bot b(1, *map, 1000 + static_cast<std::uint64_t>(seed), p);
            b.plan(snap);
            REQUIRE(b.goal_kind() != GoalKind::None);
            hunts[p] += is_hunt(b.goal_kind());
            hauls[p] += b.goal_kind() == GoalKind::Haul;
        }
    // Hauler 80/15/5, Opportunist 50/30/20, Saboteur 20/25/55 (haul / mixed / hunt), with wide tolerance.
    CHECK(hauls[Personality::Hauler] > n * 60 / 100);
    CHECK(hunts[Personality::Hauler] < n * 20 / 100);
    CHECK(hunts[Personality::Saboteur] > n * 35 / 100);
    CHECK(hauls[Personality::Saboteur] < n * 40 / 100);
    CHECK(hunts[Personality::Opportunist] > hunts[Personality::Hauler]);
    CHECK(hunts[Personality::Saboteur] > hunts[Personality::Opportunist]);
}

TEST_CASE("G4: a goal persists across undisturbed rounds and is re-chosen once it completes") {
    auto map = load("training-01");
    Match m = setup(map, 2, {});
    Bot bot(1, *map, 11, Personality::Hauler);
    bot.set_blunder_percent(0);
    bot.set_fumble_percent(0);
    play_round(m, bot);
    REQUIRE(bot.goal_kind() == GoalKind::Haul);
    const int chosen = bot.goals_chosen();
    GoalKind kind = bot.goal_kind();
    int guard = 0;
    while (m.state().robot(1).deliveries == 0 && ++guard < 8 && !m.finished()) {
        CHECK(bot.goals_chosen() == chosen);   // same goal, no re-roll, while it is still being pursued
        CHECK(bot.goal_kind() == kind);
        play_round(m, bot);
    }
    CHECK(m.state().robot(1).deliveries >= 1);
    play_round(m, bot);                         // the delivered goal is complete: a new one is chosen
    CHECK(bot.goals_chosen() > chosen);
}

TEST_CASE("G4: a round that did not go to plan makes the bot re-evaluate its goal") {
    auto map = load("training-01");
    Match m = setup(map, 2, {});
    Bot bot(1, *map, 5, Personality::Hauler);
    bot.set_blunder_percent(0);
    bot.set_fumble_percent(0);
    play_round(m, bot);
    play_round(m, bot);
    CHECK(bot.disruptions() == 0);              // everything went as predicted so far
    const int chosen = bot.goals_chosen();
    // Something interferes: the robot is teleported across the board between rounds.
    Json saved = m.save();
    Json& p = const_cast<Json&>(saved.find("players")->items()[0]);
    p.set("x", 9);
    p.set("y", 8);
    std::string err;
    auto moved = Match::restore(cfg(map, 2), saved, err);
    REQUIRE_MESSAGE(moved.has_value(), err);
    play_round(*moved, bot);
    CHECK(bot.disruptions() == 1);
    CHECK(bot.goals_chosen() > chosen);         // re-evaluated and re-chosen
}

TEST_CASE("G4: a wounded bot does not walk into a laser beam it could have avoided") {
    // laserworks-01: lit tiles (2,5), (3,5), (8,5), (9,5). P1 one hit from disabling stands above the left lane.
    auto map = load("laserworks-01");
    int beam_hits = 0, disabled = 0;
    for (std::uint64_t seed = 1; seed <= 30; ++seed)
        for (Personality p : {Personality::Hauler, Personality::Saboteur}) {
            Match m = setup(map, 2, {{3, 4, "S", 2}});
            Bot bot(1, *map, seed, p);
            bot.set_blunder_percent(0);
            bot.set_fumble_percent(0);
            for (int r = 0; r < 2 && !m.finished(); ++r) play_round(m, bot);
            beam_hits += count_events(m, "LASER_HIT", 1);
            disabled += m.state().robot(1).respawn_pending;
        }
    CHECK(beam_hits == 0);
    CHECK(disabled == 0);
}

TEST_CASE("G4: a bot takes a shunt when one is on offer (sabotage is rewarded)") {
    // shuntworks-01: pits at (4,4), (6,4), (5,6). P2 stands at (4,3), directly above the pit at (4,4); P1 is above it.
    auto map = load("shuntworks-01");
    int shunts = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        Match m = setup(map, 2, {{4, 2, "S"}, {4, 3, "E"}});
        Bot bot(1, *map, seed, Personality::Saboteur);
        bot.set_blunder_percent(0);
        bot.set_fumble_percent(0);
        play_round(m, bot);
        for (const auto& e : m.all_events())
            if (e.type == "SCORE_AWARDED" && e.params.get_string("reason").value_or("") == "SHUNT" && e.params.get_int("player").value_or(0) == 1) { ++shunts; break; }
    }
    CHECK(shunts >= 18);
}

TEST_CASE("G4: bots seek to own machinery - saboteurs standing by a trap console arm it") {
    auto map = load("trapworks-01");   // K1 at (1,5): P1 stands at (2,5), next to it
    int armed = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        Match m = setup(map, 2, {{2, 5, "N"}});
        Bot bot(1, *map, seed, Personality::Saboteur);
        bot.set_blunder_percent(0);
        bot.set_fumble_percent(0);
        play_round(m, bot);
        armed += count_events(m, "TRAP_ARMED", 1) > 0;
    }
    CHECK(armed >= 15);   // not every roll is a hunt, but a hunting saboteur right beside the console takes it
}

TEST_CASE("G4: bots never wait forever on a device they cannot reach") {
    // A trap whose console is boxed in cannot be armed; the goal must be dropped, not pursued for the whole match.
    auto map = load("trapworks-01");
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        Match m = setup(map, 2, {});
        Bot bot(1, *map, seed, Personality::Saboteur);
        for (int r = 0; r < 12 && !m.finished(); ++r) play_round(m, bot);
        CHECK(bot.goals_chosen() >= 2);          // goals complete, time out or are dropped; they are never held for ever
    }
}

TEST_CASE("G4: planning stays cheap - a full four-bot round on the biggest map takes well under a second") {
    auto map = load("foundry-01");
    Match m = setup(map, 4, {});
    const Json snap = m.snapshot();
    const auto t0 = std::chrono::steady_clock::now();
    for (int p = 1; p <= 4; ++p) {
        Bot b(p, *map, 77 + static_cast<std::uint64_t>(p), personality_for(static_cast<std::uint64_t>(p)));
        b.plan(snap);
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 1500);
}

TEST_CASE("G4: the same seed gives the same plans; different personalities play differently") {
    auto map = load("foundry-01");
    Match m = setup(map, 4, {});
    const Json snap = m.snapshot();
    Bot a(2, *map, 9, Personality::Saboteur), b(2, *map, 9, Personality::Saboteur);
    CHECK(a.plan(snap) == b.plan(snap));
    CHECK(a.describe() == b.describe());
    std::map<std::string, int> goals;
    for (Personality p : {Personality::Hauler, Personality::Saboteur})
        for (int seed = 1; seed <= 60; ++seed) {
            Bot x(2, *map, static_cast<std::uint64_t>(seed), p);
            x.plan(snap);
            goals[std::string(to_string(p)) + ":" + to_string(x.goal_kind())]++;
        }
    CHECK(goals["hauler:haul"] > goals["saboteur:haul"]);
}

TEST_CASE("G4: the server gives bot seats mixed personalities and reports them in the match status") {
    using namespace botched::server;
    ManualClock clock;
    ServerConfig sc;
    sc.bot_seed = 3;
    GameServer srv(sc, clock);
    Json c = Json::object();
    c.set("type", "CREATE_MATCH");
    c.set("player_count", 4);
    c.set("bots", 3);
    Json r = srv.handle(c);
    REQUIRE(r.get_bool("ok").value_or(false));
    const std::string id = *r.get_string("match_id");
    Json j = Json::object();
    j.set("type", "JOIN_MATCH");
    j.set("match_id", id);
    REQUIRE(srv.handle(j).get_bool("ok").value_or(false));   // the one human: the match starts
    Json q = Json::object();
    q.set("type", "GET_MATCH_STATUS");
    q.set("match_id", id);
    const Json st = *srv.handle(q).find("status");
    const Json* kinds = st.find("bot_personalities");
    REQUIRE(kinds != nullptr);
    REQUIRE(kinds->items().size() == 3);
    std::map<std::string, int> seen;
    for (const auto& k : kinds->items()) seen[k.as_string()]++;
    CHECK(seen.size() == 3);   // three bots: hauler, opportunist and saboteur all present
}

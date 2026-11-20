// M0-H3: the approved request-id contract (docs/request-id-contract.md).
#include <map>
#include <memory>
#include <string>

#include "bot.hpp"
#include "botched/match.hpp"
#include "botched/serialize.hpp"
#include "doctest.h"
#include "server.hpp"
#include "sim_runner.hpp"

using namespace botched;
using namespace botched::server;

namespace {

std::unique_ptr<Match> make(int players = 2, int rounds = kDefaultRounds, int budget = kDefaultRequestBudget) {
    MatchConfig c;
    c.match_id = "m";
    c.map = std::make_shared<const Map>(training01());
    c.player_count = players;
    c.rounds = rounds;
    c.request_budget_per_round = budget;
    return std::make_unique<Match>(c);
}

using Orders = std::vector<std::string>;
const Orders kA = {"FORWARD_1", "WAIT", "WAIT"};
const Orders kB = {"BACK_1", "WAIT", "WAIT"};
const Orders kWait = {"WAIT", "WAIT", "WAIT"};

std::string fmt(const SubmitResult& r) {
    return std::string(r.ok ? "ok" : "no") + "|" + r.code + "|" + r.message + "|" + std::to_string(r.round) + "|" +
           std::to_string(r.revision) + "|" + std::to_string(r.state_version);
}

// A "junk" request that is rejected by the rules (wrong round) so it never changes the game.
SubmitResult junk(Match& m, int player, const std::string& id) {
    return m.submit_program(player, 99, 1, kWait, false, id);
}

}  // namespace

// ------------------------------------------------------------------ replay fidelity

TEST_CASE("H3: a retry replays the original response byte for byte, for every outcome") {
    auto m = make(2);
    struct Probe { std::string id; int player, round, revision; Orders orders; bool ready; std::string expect_code; };
    std::vector<Probe> probes = {
        {"accepted", 1, 1, 1, kA, false, "OK"},
        {"unchanged", 1, 1, 1, kA, false, "OK"},                       // identical content, new id
        {"conflict", 1, 1, 1, kB, false, "REVISION_CONFLICT"},
        {"stale-seed", 1, 1, 3, kB, false, "OK"},                       // moves the accepted revision to 3
        {"stale", 1, 1, 2, kB, false, "STALE_REVISION"},
        {"badrev", 2, 1, 0, kA, false, "INVALID_REVISION"},
        {"badtoken", 2, 1, 1, {"FORWARD_1", "JUMP", "WAIT"}, false, "INVALID_ORDERS"},
        {"badcount", 2, 1, 1, {"WAIT", "WAIT"}, false, "INVALID_ORDERS"},
        {"wronground", 2, 7, 1, kA, false, "WRONG_ROUND"},
        {"lock", 2, 1, 1, kA, true, "OK"},
        {"locked", 2, 1, 2, kB, false, "PROGRAM_LOCKED"},
    };
    std::map<std::string, SubmitResult> first;
    for (const auto& p : probes) {
        SubmitResult r = m->submit_program(p.player, p.round, p.revision, p.orders, p.ready, p.id);
        INFO(p.id);
        CHECK(r.code == p.expect_code);
        first[p.id] = r;
    }
    m->close_planning();
    SubmitResult closed = m->submit_program(1, 1, 9, kA, false, "closed");
    CHECK(closed.code == "PLANNING_CLOSED");
    first["closed"] = closed;
    const int version = m->state_version();
    const std::size_t records = m->request_record_count();
    const std::string hash = m->state_hash();

    m->run_round();  // round advances: every retry below happens in another round
    for (const auto& p : probes) {
        INFO(p.id);
        SubmitResult again = m->submit_program(p.player, p.round, p.revision, p.orders, p.ready, p.id);
        CHECK(fmt(again) == fmt(first[p.id]));
    }
    CHECK(fmt(m->submit_program(1, 1, 9, kA, false, "closed")) == fmt(closed));
    CHECK(m->request_record_count() == records);  // replays add nothing
    (void)version; (void)hash;
}

TEST_CASE("H3: replays have no effect: no state change, no events, no version bump, no budget") {
    auto m = make(2);
    CHECK(m->submit_program(1, 1, 1, kA, true, "x").ok);
    const int v = m->state_version();
    const std::string hash = m->state_hash();
    const std::size_t events = m->all_events().size();
    for (int i = 0; i < 500; ++i) CHECK(m->submit_program(1, 1, 1, kA, true, "x").ok);
    CHECK(m->state_version() == v);
    CHECK(m->state_hash() == hash);
    CHECK(m->all_events().size() == events);
    CHECK(m->request_record_count() == 1);
}

// ------------------------------------------------------------------ late retries (S3-S5 of the proposal)

TEST_CASE("H3: late retries - after planning closes, after the round advances, after the match ends") {
    auto m = make(2, 2);
    SubmitResult a = m->submit_program(1, 1, 1, kA, true, "x");
    REQUIRE(a.ok);
    m->close_planning();
    CHECK(fmt(m->submit_program(1, 1, 1, kA, true, "x")) == fmt(a));          // D3: replay, not PLANNING_CLOSED
    CHECK(m->submit_program(1, 1, 1, kA, true, "fresh").code == "PLANNING_CLOSED");  // a NEW id is rejected
    m->run_round();
    CHECK(fmt(m->submit_program(1, 1, 1, kA, true, "x")) == fmt(a));
    m->play_to_end();
    REQUIRE(m->finished());
    CHECK(fmt(m->submit_program(1, 1, 1, kA, true, "x")) == fmt(a));
    CHECK(m->submit_program(1, 2, 1, kA, true, "late-new").code == "PLANNING_CLOSED");
}

TEST_CASE("H3: eviction can no longer happen - the S5 scenario is inverted") {
    auto m = make(2);
    SubmitResult a = m->submit_program(1, 1, 1, kA, true, "x");
    REQUIRE(a.ok);
    for (int i = 0; i < 20000; ++i) junk(*m, 1, "junk" + std::to_string(i));  // far beyond any cache size
    CHECK(fmt(m->submit_program(1, 1, 1, kA, true, "x")) == fmt(a));         // still the original, not PROGRAM_LOCKED
}

TEST_CASE("H3: a rejection is replayed too; a stale 'too early' rejection needs a fresh id (D4)") {
    auto m = make(2);
    SubmitResult early = m->submit_program(1, 2, 1, kA, true, "e");
    CHECK(early.code == "WRONG_ROUND");
    m->run_round();  // round 2 is now open
    CHECK(fmt(m->submit_program(1, 2, 1, kA, true, "e")) == fmt(early));      // same id: original rejection
    CHECK(m->submit_program(1, 2, 1, kA, true, "e2").ok);                    // fresh id: processed
}

// ------------------------------------------------------------------ conflicting payloads (D2)

TEST_CASE("H3: reusing an id with different contents is refused, not replayed and not applied") {
    struct Variant { const char* what; int round, revision; Orders orders; bool ready; };
    const std::vector<Variant> variants = {
        {"round", 2, 1, kA, false},
        {"revision", 1, 2, kA, false},
        {"order slot 0", 1, 1, {"BACK_1", "WAIT", "WAIT"}, false},
        {"order slot 1", 1, 1, {"FORWARD_1", "TOGGLE", "WAIT"}, false},
        {"order slot 2", 1, 1, {"FORWARD_1", "WAIT", "DROP"}, false},
        {"ready", 1, 1, kA, true},
        {"count", 1, 1, {"FORWARD_1", "WAIT"}, false},
        {"invalid token", 1, 1, {"FORWARD_1", "JUMP", "WAIT"}, false},
    };
    for (const auto& v : variants) {
        auto m = make(2);
        SubmitResult first = m->submit_program(1, 1, 1, kA, false, "x");
        REQUIRE(first.ok);
        const std::string hash = m->state_hash();
        const int version = m->state_version();
        const std::size_t records = m->request_record_count();
        INFO(v.what);
        SubmitResult r = m->submit_program(1, v.round, v.revision, v.orders, v.ready, "x");
        CHECK_FALSE(r.ok);
        CHECK(r.code == "REQUEST_ID_REUSED");
        CHECK(m->state_hash() == hash);          // nothing applied
        CHECK(m->state_version() == version);
        CHECK(m->request_record_count() == records);  // not recorded
        CHECK(fmt(m->submit_program(1, 1, 1, kA, false, "x")) == fmt(first));  // original untouched, still replays
        // The conflicting request consumed no budget: all N fresh ids are still available.
        int processed = 0;
        for (int i = 0; i < m->request_budget() - 1; ++i)
            if (junk(*m, 1, "f" + std::to_string(i)).code != "REQUEST_LIMIT") ++processed;
        CHECK(processed == m->request_budget() - 1);
    }
}

TEST_CASE("H3: ids are exact: spelling variants are distinct ids, never conflated") {
    auto m = make(2);
    for (const char* id : {"abc", "ABC", "abc.", "abc-", "abc_", "abc:", "abd", "ab", "abcc", "0", "00"}) {
        SubmitResult r = junk(*m, 1, id);
        CHECK(r.code == "WRONG_ROUND");  // each processed as a first sight
    }
    CHECK(m->request_record_count() == 11);
}

TEST_CASE("H3: payloads that the rules reject identically are equivalent for idempotency (and only those)") {
    auto m = make(2);
    SubmitResult first = m->submit_program(1, 1, 1, {"FORWARD_1", "JUMP", "WAIT"}, false, "x");
    CHECK(first.code == "INVALID_ORDERS");
    // A different unrecognised token: same payload class, same rejection, no effect -> replay.
    CHECK(fmt(m->submit_program(1, 1, 1, {"FORWARD_1", "NOPE", "WAIT"}, false, "x")) == fmt(first));
    // A valid payload under the same id is a different request.
    CHECK(m->submit_program(1, 1, 1, kA, false, "x").code == "REQUEST_ID_REUSED");
}

// ------------------------------------------------------------------ budget (D5, D7)

TEST_CASE("H3: the budget rations NEW ids per player per round and never forgets old ones") {
    auto m = make(2, 12, 8);
    REQUIRE(m->request_budget() == 8);
    for (int i = 0; i < 8; ++i) CHECK(junk(*m, 1, "id" + std::to_string(i)).code == "WRONG_ROUND");
    CHECK(m->request_record_count() == 8);
    SubmitResult limited = junk(*m, 1, "id8");
    CHECK_FALSE(limited.ok);
    CHECK(limited.code == "REQUEST_LIMIT");
    CHECK(m->request_record_count() == 8);                    // refusal is not recorded
    CHECK(junk(*m, 1, "id8").code == "REQUEST_LIMIT");        // ... so it is refused again, still not recorded
    CHECK(junk(*m, 1, "id0").code == "WRONG_ROUND");          // replays still work at the limit
    CHECK(m->request_record_count() == 8);
    // Another player has an independent budget.
    for (int i = 0; i < 8; ++i) CHECK(junk(*m, 2, "id" + std::to_string(i)).code == "WRONG_ROUND");
    CHECK(junk(*m, 2, "id8").code == "REQUEST_LIMIT");
    // The budget refills only when the round advances, and the refused id is then processed as new.
    m->run_round();
    SubmitResult now = m->submit_program(1, 2, 1, kA, false, "id8");
    CHECK(now.ok);
    CHECK(now.code == "OK");
    CHECK(m->request_record_count() == 8 + 8 + 1);
    CHECK(junk(*m, 1, "id0").code == "WRONG_ROUND");  // the old record from round 1 is still there
}

TEST_CASE("H3: the budget counts rejected and accepted ids alike; unkeyed requests are never recorded or counted") {
    auto m = make(2, 12, 8);
    for (int i = 0; i < 8; ++i) {
        // Alternate accepted revisions and rejections: all are charged.
        SubmitResult r = (i % 2) ? junk(*m, 1, "k" + std::to_string(i))
                                 : m->submit_program(1, 1, i + 1, kA, false, "k" + std::to_string(i));
        CHECK(r.code != "REQUEST_LIMIT");
    }
    CHECK(junk(*m, 1, "overflow").code == "REQUEST_LIMIT");
    const std::size_t records = m->request_record_count();
    for (int i = 0; i < 5000; ++i) CHECK(junk(*m, 1, "").code == "WRONG_ROUND");  // unkeyed
    CHECK(m->request_record_count() == records);
    // Unknown players are never recorded or charged.
    for (int i = 0; i < 100; ++i) CHECK(junk(*m, 9, "p" + std::to_string(i)).code == "UNKNOWN_PLAYER");
    CHECK(junk(*m, 0, "z").code == "UNKNOWN_PLAYER");
    CHECK(junk(*m, -3, "z").code == "UNKNOWN_PLAYER");
    CHECK(m->request_record_count() == records);
}

TEST_CASE("H3: the budget is configurable within 8-128 and clamped outside it") {
    CHECK(make(2, 12, 1)->request_budget() == kMinRequestBudget);
    CHECK(make(2, 12, 0)->request_budget() == kMinRequestBudget);
    CHECK(make(2, 12, -5)->request_budget() == kMinRequestBudget);
    CHECK(make(2, 12, 10000)->request_budget() == kMaxRequestBudget);
    CHECK(make(2, 12, 64)->request_budget() == 64);
    CHECK(make(2)->request_budget() == 32);
    auto m = make(2, 12, 128);
    for (int i = 0; i < 128; ++i) CHECK(junk(*m, 1, "i" + std::to_string(i)).code == "WRONG_ROUND");
    CHECK(junk(*m, 1, "i128").code == "REQUEST_LIMIT");
}

// ------------------------------------------------------------------ memory bounds

TEST_CASE("H3: memory is bounded by players x rounds x budget, whatever is sent") {
    const int players = 4, rounds = 12, budget = 32;
    auto m = make(players, rounds, budget);
    const std::string long_id(64, 'x');
    while (!m->finished()) {
        for (int p = 1; p <= players; ++p)
            for (int i = 0; i < 400; ++i)  // 12x more than the budget, with maximum-length ids
                junk(*m, p, long_id.substr(0, 40) + std::to_string(m->state().round) + "-" + std::to_string(i) + "-" +
                                std::string(10, 'y'));
        m->run_round();
    }
    const std::size_t bound = static_cast<std::size_t>(players) * rounds * budget;
    CHECK(m->request_record_count() == bound);       // exactly full: the budget, nothing more
    const std::size_t per_record_worst = 56 + 96;     // record + one heap block for a 64-char id
    CHECK(m->request_store_bytes() <= bound * per_record_worst);
    // After the match ends nothing more is ever stored.
    const std::size_t bytes = m->request_store_bytes();
    for (int i = 0; i < 100000; ++i) junk(*m, 1 + i % players, "post" + std::to_string(i));
    CHECK(m->request_record_count() == bound);
    CHECK(m->request_store_bytes() == bytes);
}

TEST_CASE("H3: a conforming client uses a small fraction of the budget and memory") {
    auto m = make(4);
    for (int round = 1; round <= 12; ++round) {
        for (int p = 1; p <= 4; ++p) {
            m->submit_program(p, round, 1, kWait, false, "c" + std::to_string(round) + "a");
            m->submit_program(p, round, 2, kWait, true, "c" + std::to_string(round) + "b");
            m->submit_program(p, round, 2, kWait, true, "c" + std::to_string(round) + "b");  // retry
        }
    }
    CHECK(m->request_record_count() == 4 * 12 * 2);
    CHECK(m->request_store_bytes() < 16 * 1024);
}

// ------------------------------------------------------------------ determinism

TEST_CASE("H3: simulator state/event hashes are unchanged from before the idempotency work") {
    sim::SimOptions o;  // players 4, seed 1, 12 rounds: the hashes recorded before M0-H1/H2/H3
    sim::SimResult r = sim::run_simulation(o);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.state_hash == "e846c1c016b36358");
    CHECK(r.event_hash == "5d9ebce915bed1f8");
}

TEST_CASE("H3: retries, conflicts and refusals interleaved anywhere never change the game") {
    // Reference run: only the first-sight commands. Noisy runs add replays, conflicting reuses and
    // refused (over-budget) ids at random points. Final state and every first-sight reply must match.
    struct Cmd { int player; int round_offset; int revision; Orders orders; bool ready; std::string id; };
    sim::Rng script_rng(99);
    static const char* all[] = {"FORWARD_1", "FORWARD_2", "BACK_1", "TURN_LEFT", "TURN_RIGHT", "TURN_AROUND", "DROP", "TOGGLE", "WAIT"};
    auto run = [&](std::uint64_t noise_seed) {
        auto m = make(4);
        sim::Rng script(99);
        sim::Rng noise(noise_seed);
        std::vector<std::string> replies;
        std::vector<std::pair<Cmd, std::string>> history;
        int serial = 0;
        while (!m->finished()) {
            const int round = m->state().round;
            for (int p = 1; p <= 4; ++p) {
                Cmd c{p, 0, 1, {all[script.below(9)], all[script.below(9)], all[script.below(9)]}, script.below(2) == 0,
                      "r" + std::to_string(round) + "p" + std::to_string(p) + "n" + std::to_string(serial++)};
                SubmitResult r = m->submit_program(c.player, round, c.revision, c.orders, c.ready, c.id);
                replies.push_back(fmt(r));
                history.push_back({c, fmt(r)});
                if (noise_seed != 0) {
                    for (int k = noise.below(4); k > 0; --k) {
                        auto& h = history[static_cast<std::size_t>(noise.below(static_cast<int>(history.size())))];
                        switch (noise.below(3)) {
                            case 0:  // replay an old command
                                CHECK(fmt(m->submit_program(h.first.player, h.first.round_offset + std::stoi(h.first.id.substr(1, h.first.id.find('p') - 1)),
                                                            h.first.revision, h.first.orders, h.first.ready, h.first.id)) == h.second);
                                break;
                            case 1:  // conflicting reuse of an old id
                                CHECK(m->submit_program(h.first.player, 1, 77, kB, false, h.first.id).code == "REQUEST_ID_REUSED");
                                break;
                            default:  // malformed-at-Match-level: unknown player, never recorded
                                CHECK(junk(*m, 9, "zzz").code == "UNKNOWN_PLAYER");
                        }
                    }
                }
            }
            m->run_round();
        }
        return std::make_pair(m->state_hash(), replies);
    };
    auto base = run(0);
    for (std::uint64_t seed : {1ULL, 2ULL, 3ULL, 4ULL}) {
        auto noisy = run(seed);
        CHECK(noisy.first == base.first);
        CHECK(noisy.second == base.second);
    }
    (void)script_rng;
}

TEST_CASE("H3: arrival order across players does not change results or budgets") {
    auto run = [](bool reversed) {
        auto m = make(4, 12, 8);
        std::vector<int> order = {1, 2, 3, 4};
        if (reversed) order = {4, 3, 2, 1};
        std::vector<std::string> replies(4 * 20);
        for (int round = 1; round <= 2; ++round) {
            for (int i = 0; i < 20; ++i)  // 20 fresh ids per player per round: 12 are refused (budget 8)
                for (int p : order)
                {
                    // state_version legitimately depends on how different players' accepted submissions
                    // interleave, so compare the answer, not the version.
                    SubmitResult r = m->submit_program(p, round, i + 1, kWait, false, "r" + std::to_string(round) + "-" + std::to_string(i));
                    replies[static_cast<std::size_t>((p - 1) * 20 + i)] += r.code + "|" + r.message + ";";
                }
            m->run_round();
        }
        return std::make_pair(m->state_hash(), replies);
    };
    auto a = run(false), b = run(true);
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
}

// ------------------------------------------------------------------ model-based fuzz

TEST_CASE("H3: random command streams always agree with a simple idempotency model") {
    for (std::uint64_t seed : {11ULL, 12ULL, 13ULL}) {
        auto m = make(3, 12, 8);
        sim::Rng rng(seed);
        struct Expect { int round, revision; Orders orders; bool ready; std::string reply; };
        std::map<std::pair<int, std::string>, Expect> seen;
        std::map<std::pair<int, int>, int> used;  // (player, budget round) -> new ids charged
        static const char* all[] = {"FORWARD_1", "FORWARD_2", "BACK_1", "TURN_LEFT", "TURN_RIGHT", "TURN_AROUND", "DROP", "TOGGLE", "WAIT"};
        std::vector<std::pair<int, std::string>> ids;
        for (int op = 0; op < 6000 && !m->finished(); ++op) {
            int player = 1 + rng.below(3);
            const int budget_round = m->state().round;
            std::string id;
            Expect probe;
            probe.round = rng.below(4) == 0 ? budget_round + 1 : budget_round;
            probe.revision = 1 + rng.below(3);
            probe.orders = {all[rng.below(9)], rng.below(6) == 0 ? "JUMP" : all[rng.below(9)], all[rng.below(9)]};
            probe.ready = rng.below(3) == 0;
            const bool fresh_id = rng.below(2) == 0 || ids.empty();
            if (fresh_id) {
                id = "id" + std::to_string(op);
            } else {
                auto pick = ids[static_cast<std::size_t>(rng.below(static_cast<int>(ids.size())))];
                player = pick.first;
                id = pick.second;
                if (rng.below(2) == 0) {  // retry exactly
                    const Expect& e = seen[{player, id}];
                    probe.round = e.round; probe.revision = e.revision; probe.orders = e.orders; probe.ready = e.ready;
                }
            }
            SubmitResult r = m->submit_program(player, probe.round, probe.revision, probe.orders, probe.ready, id);
            auto it = seen.find({player, id});
            if (it != seen.end()) {
                const Expect& e = it->second;
                bool same = e.round == probe.round && e.revision == probe.revision && e.ready == probe.ready;
                for (int i = 0; i < 3; ++i) {
                    bool ea = std::string(e.orders[static_cast<std::size_t>(i)]) == "JUMP";
                    bool pa = std::string(probe.orders[static_cast<std::size_t>(i)]) == "JUMP";
                    same = same && (ea == pa) && (ea || e.orders[static_cast<std::size_t>(i)] == probe.orders[static_cast<std::size_t>(i)]);
                }
                if (same) CHECK(fmt(r) == e.reply);
                else CHECK(r.code == "REQUEST_ID_REUSED");
            } else if (used[{player, budget_round}] >= 8) {
                CHECK(r.code == "REQUEST_LIMIT");
            } else {
                CHECK(r.code != "REQUEST_LIMIT");
                CHECK(r.code != "REQUEST_ID_REUSED");
                probe.reply = fmt(r);
                seen[{player, id}] = probe;
                ids.push_back({player, id});
                ++used[{player, budget_round}];
            }
            if (rng.below(40) == 0) m->run_round();
        }
        CHECK(m->request_record_count() == seen.size());
    }
}

// ------------------------------------------------------------------ restore (D8)

TEST_CASE("H3: a restored match starts with an empty idempotency store and fresh budgets (D8)") {
    auto m = make(2, 12, 8);
    for (int i = 0; i < 8; ++i) junk(*m, 1, "id" + std::to_string(i));
    CHECK(junk(*m, 1, "overflow").code == "REQUEST_LIMIT");
    Json saved = m->save();
    std::string err;
    auto r = Match::restore(m->config(), saved, err);
    REQUIRE_MESSAGE(r.has_value(), err);
    CHECK(r->request_record_count() == 0);
    CHECK(r->request_budget() == 8);
    for (int i = 0; i < 8; ++i) CHECK(junk(*r, 1, "new" + std::to_string(i)).code == "WRONG_ROUND");
    CHECK(junk(*r, 1, "new8").code == "REQUEST_LIMIT");
    // Documented limitation: an old id is processed as a fresh request after a restore.
    auto r2 = Match::restore(m->config(), saved, err);
    REQUIRE(r2.has_value());
    CHECK(junk(*r2, 1, "id0").code == "WRONG_ROUND");
}

// ------------------------------------------------------------------ server level (JSON)

namespace {
Json req(const char* type, const std::string& match = "") {
    Json j = Json::object();
    j.set("type", type);
    if (!match.empty()) j.set("match_id", match);
    return j;
}
struct Lobby {
    ManualClock clock;
    GameServer srv;
    std::string match;
    std::vector<std::string> session;
    explicit Lobby(ServerConfig cfg = {}) : srv(cfg, clock) {
        Json c = req("CREATE_MATCH");
        c.set("player_count", 2);
        match = *srv.handle(c).get_string("match_id");
        for (int i = 0; i < 2; ++i) session.push_back(*srv.handle(req("JOIN_MATCH", match)).get_string("session"));
    }
    Json submit(int p, int round, int rev, const Orders& orders, bool ready, const std::string& id) {
        Json s = req("SUBMIT_PROGRAM", match);
        s.set("player_id", p);
        s.set("session", session[static_cast<std::size_t>(p - 1)]);
        s.set("round", round);
        s.set("revision", rev);
        Json a = Json::array();
        for (const auto& o : orders) a.push(o);
        s.set("orders", a);
        s.set("ready", ready);
        if (!id.empty()) s.set("request_id", id);
        return srv.handle(s);
    }
};
std::string code(const Json& j) { return j.get_string("code").value_or(""); }
}  // namespace

TEST_CASE("H3 server: replay is byte-identical JSON; conflicts and limits use the new codes") {
    ServerConfig cfg;
    cfg.request_budget_per_round = 8;
    Lobby l(cfg);
    Json first = l.submit(1, 1, 1, kA, true, "c-1");
    CHECK(first.get_bool("ok").value());
    CHECK(l.submit(1, 1, 1, kA, true, "c-1").dump() == first.dump());
    Json reused = l.submit(1, 1, 2, kB, false, "c-1");
    CHECK(code(reused) == "REQUEST_ID_REUSED");
    CHECK_FALSE(reused.get_bool("ok").value());
    CHECK(reused.get_string("request_id") == "c-1");
    for (int i = 0; i < 7; ++i) CHECK(code(l.submit(2, 5, 1, kA, false, "q" + std::to_string(i))) == "WRONG_ROUND");
    Json limited = l.submit(2, 5, 1, kA, false, "q7-too-many");
    CHECK(code(limited) == "WRONG_ROUND");  // the 8th new id for player 2 is still processed
    Json over = l.submit(2, 5, 1, kA, false, "q8");
    CHECK(code(over) == "REQUEST_LIMIT");
    CHECK(over.get_string("request_id") == "q8");
    // Player 2 has used its whole budget: even a legitimate new id is refused for the rest of the round.
    CHECK(code(l.submit(2, 1, 1, kA, true, "after")) == "REQUEST_LIMIT");
    // The round advances only through the game (here: the planning timer); then budgets refill.
    l.clock.advance_ms(61'000);
    l.srv.poll();
    CHECK(l.srv.find_match(l.match)->state().round == 2);
    CHECK(l.submit(2, 2, 1, kA, false, "q8").get_bool("ok").value());   // the previously refused id, now new
    CHECK(l.submit(1, 1, 1, kA, true, "c-1").dump() == first.dump());  // and round-1 history still replays
}

TEST_CASE("H3 server: malformed and unauthenticated traffic cannot consume or refill a budget") {
    ServerConfig cfg;
    cfg.request_budget_per_round = 8;
    Lobby l(cfg);
    auto base = [&] { return l.submit(1, 5, 1, kA, false, "").dump(); };  // (unkeyed helper only used below)
    (void)base;
    // 2000 requests that must never reach the idempotency store.
    for (int i = 0; i < 500; ++i) {
        Json s = Json::object();
        s.set("type", "SUBMIT_PROGRAM");
        s.set("match_id", l.match);
        s.set("player_id", 1);
        s.set("session", i % 2 ? "bad-session" : std::string(64, 'f'));  // unauthenticated
        s.set("round", 1);
        s.set("revision", 1);
        Json a = Json::array();
        for (int k = 0; k < 3; ++k) a.push("WAIT");
        s.set("orders", a);
        s.set("request_id", "m" + std::to_string(i));
        CHECK(code(l.srv.handle(s)) == "UNAUTHORIZED");
        Json t = s;  // authenticated slot but malformed fields
        t.set("session", l.session[0]);
        t.set("revision", "1");
        CHECK(code(l.srv.handle(t)) == "BAD_REQUEST");
        Json u = t;
        u.set("revision", 1);
        Json four = Json::array();
        for (int k = 0; k < 4; ++k) four.push("WAIT");
        u.set("orders", four);
        CHECK(code(l.srv.handle(u)) == "INVALID_ORDERS");
        Json w = u;
        w.set("orders", a);
        w.set("request_id", std::string(65, 'x'));
        CHECK(code(l.srv.handle(w)) == "BAD_REQUEST");
        CHECK(code(l.srv.handle_text("{\"type\":\"SUBMIT_PROGRAM\",")) == "BAD_REQUEST");
    }
    const Match* m = l.srv.find_match(l.match);
    CHECK(m->request_record_count() == 0);
    // The full budget is still there, and no more than that.
    for (int i = 0; i < 8; ++i) CHECK(code(l.submit(1, 5, 1, kA, false, "ok" + std::to_string(i))) == "WRONG_ROUND");
    CHECK(code(l.submit(1, 5, 1, kA, false, "ok8")) == "REQUEST_LIMIT");
}

TEST_CASE("H3 server: the configured budget is clamped to the documented range") {
    ServerConfig lo;
    lo.request_budget_per_round = 1;
    Lobby a(lo);
    CHECK(a.srv.find_match(a.match)->request_budget() == kMinRequestBudget);
    ServerConfig hi;
    hi.request_budget_per_round = 100000;
    Lobby b(hi);
    CHECK(b.srv.find_match(b.match)->request_budget() == kMaxRequestBudget);
    Lobby d;
    CHECK(d.srv.find_match(d.match)->request_budget() == kDefaultRequestBudget);
}

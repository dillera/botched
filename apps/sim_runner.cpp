#include "sim_runner.hpp"

#include <fstream>
#include <memory>
#include <ostream>
#include <sstream>

#include "ascii_render.hpp"
#include "bot.hpp"
#include "botched/serialize.hpp"
#include "server.hpp"

namespace botched::sim {

namespace {

using server::GameServer;

Json req(const char* type) {
    Json j = Json::object();
    j.set("type", type);
    return j;
}

SimResult fail(SimResult r, std::string msg) {
    r.ok = false;
    r.error = std::move(msg);
    return r;
}

}  // namespace

SimResult run_simulation(const SimOptions& opt) {
    SimResult res;
    server::ManualClock clock;
    server::ServerConfig cfg;
    cfg.default_planning_seconds = 60;
    cfg.maps_dir = opt.maps_dir;
    GameServer srv(cfg, clock);

    std::string map_id = opt.map_id;
    if (!opt.map_file.empty()) {
        std::ifstream f(opt.map_file);
        if (!f) return fail(res, "cannot open map file " + opt.map_file);
        std::stringstream ss;
        ss << f.rdbuf();
        ValidationOptions vo;
        vo.player_count = opt.players;
        MapLoad ml = load_map("custom", ss.str(), vo);
        if (!ml.ok()) return fail(res, "map invalid: " + (ml.errors.empty() ? std::string("parse error") : ml.errors[0]));
        srv.register_map(std::make_shared<const Map>(std::move(*ml.map)));  // id "custom"
        map_id = "custom";
    }

    Json create = req("CREATE_MATCH");
    create.set("map_id", map_id);
    create.set("player_count", opt.players);
    create.set("rounds", opt.rounds);
    if (opt.target_score > 0) create.set("target_score", opt.target_score);
    Json created = srv.handle(create);
    if (!created.get_bool("ok").value_or(false)) return fail(res, "create failed: " + created.dump());
    const std::string match_id = *created.get_string("match_id");

    std::vector<std::string> sessions;
    for (int p = 1; p <= opt.players; ++p) {
        Json j = req("JOIN_MATCH");
        j.set("match_id", match_id);
        Json joined = srv.handle(j);
        if (!joined.get_bool("ok").value_or(false)) return fail(res, "join failed: " + joined.dump());
        sessions.push_back(*joined.get_string("session"));
    }

    // Client side: fetch the map definition like any client would.
    Json gm = req("GET_MAP");
    gm.set("match_id", match_id);
    Json mapresp = srv.handle(gm);
    MapLoad ml = map_from_json(*mapresp.find("map"));
    if (!ml.map) return fail(res, "client could not parse GET_MAP");
    const Map& map = *ml.map;

    std::vector<Bot> bots;
    for (int p = 1; p <= opt.players; ++p)
        bots.emplace_back(p, map, opt.seed * 1000003ULL + static_cast<std::uint64_t>(p),
                          personality_for(opt.seed + static_cast<std::uint64_t>(p)));   // mixed personalities
    Rng chaos(opt.seed ^ 0xabcdef12345ULL);

    int since = 0;
    std::string event_text;
    auto drain_events = [&]() -> bool {
        Json j = req("GET_EVENTS");
        j.set("match_id", match_id);
        j.set("since_version", since);
        Json r = srv.handle(j);
        if (!r.get_bool("ok").value_or(false)) return false;
        for (const auto& e : r.find("events")->items()) {
            std::string line = e.dump();
            event_text += line;
            event_text += '\n';
            ++res.events;
            if (e.get_string("type").value_or("") == "PLANNING_CLOSED") {
                if (const Json* p = e.find("params"))
                    if (const Json* m = p->find("missing")) res.timeouts += static_cast<int>(m->items().size());
            }
            if (opt.out && opt.print_events) *opt.out << line << '\n';
        }
        since = static_cast<int>(*r.get_int("state_version"));
        return true;
    };

    auto status = [&]() {
        Json j = req("GET_MATCH_STATUS");
        j.set("match_id", match_id);
        return srv.handle(j).find("status")->get_string("phase").value_or("?");
    };
    auto snapshot = [&]() {
        Json j = req("GET_SNAPSHOT");
        j.set("match_id", match_id);
        return *srv.handle(j).find("snapshot");
    };

    if (!drain_events()) return fail(res, "initial event fetch failed");
    int guard = 0;
    while (status() != "FINISHED") {
        if (++guard > opt.rounds + 2) return fail(res, "match did not progress");
        Json snap = snapshot();
        const int round = static_cast<int>(*snap.get_int("round"));
        for (int p = 1; p <= opt.players; ++p) {
            if (chaos.below(100) < opt.flaky_percent) continue;  // this client never answers this round
            auto orders = bots[static_cast<std::size_t>(p - 1)].plan(snap);
            const bool two_step = chaos.below(4) == 0;  // sometimes revise before locking in
            for (int rev = two_step ? 1 : 2; rev <= 2; ++rev) {
                Json s = req("SUBMIT_PROGRAM");
                s.set("match_id", match_id);
                s.set("player_id", p);
                s.set("session", sessions[static_cast<std::size_t>(p - 1)]);
                s.set("round", round);
                s.set("revision", rev);
                Json arr = Json::array();
                for (const auto& o : orders) arr.push(rev == 1 && two_step ? "WAIT" : o);
                s.set("orders", std::move(arr));
                s.set("ready", rev == 2);
                s.set("request_id", "bot" + std::to_string(p) + "-r" + std::to_string(round) + "-v" + std::to_string(rev));
                Json a = srv.handle(s);
                Json b = srv.handle(s);  // network retry: must be answered identically
                if (a.dump() != b.dump()) return fail(res, "retry was not idempotent: " + a.dump() + " vs " + b.dump());
                if (!a.get_bool("ok").value_or(false) && a.get_string("code").value_or("") != "PLANNING_CLOSED")
                    return fail(res, "submit rejected: " + a.dump());
            }
        }
        if (status() == "FINISHED") { drain_events(); ++res.rounds_played; break; }
        Json after = snapshot();
        if (*after.get_int("round") == round) {
            // Someone did not lock in: let the planning timer run out.
            clock.advance_ms(61'000);
            srv.poll();
        }
        drain_events();
        ++res.rounds_played;
        if (opt.out && opt.verbose) {
            Json now = snapshot();
            *opt.out << "--- end of round " << round << " ---\n"
                     << render_board(map, now) << render_scores(now);
            for (std::size_t i = 0; i < bots.size(); ++i) *opt.out << "  P" << i + 1 << " " << bots[i].describe() << '\n';
            *opt.out << '\n';
        }
    }

    const Match* m = srv.find_match(match_id);
    res.final_snapshot = snapshot();
    res.state_hash = m->state_hash();
    res.event_hash = hex64(fnv1a64(event_text));
    res.winners = compute_winners(m->state());
    res.ok = true;
    return res;
}

}  // namespace botched::sim

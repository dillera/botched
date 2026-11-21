#include "botched/validate.hpp"

#include <set>

namespace botched {

namespace {
bool fail(std::string& e, const std::string& m) { e = m; return false; }
}  // namespace

bool validate_world(const GameState& s, const Map& map, int player_count, int rounds, int target_score, std::string& error) {
    if (s.player_count() != player_count)
        return fail(error, "player count " + std::to_string(s.player_count()) + " does not match configuration " +
                               std::to_string(player_count));
    if (s.rounds_total != rounds)
        return fail(error, "rounds_total " + std::to_string(s.rounds_total) + " does not match configured rounds " +
                               std::to_string(rounds));

    if (s.target_score != target_score)
        return fail(error, "target_score " + std::to_string(s.target_score) + " does not match configured target " +
                               std::to_string(target_score));

    // ---- phase / round / tick (what Match can actually be in) ----------------------------
    if (s.round < 1 || s.round > s.rounds_total) return fail(error, "round must be within 1..rounds_total");
    switch (s.phase) {
        case Phase::Planning:
            if (s.tick != 0) return fail(error, "PLANNING requires tick 0");
            break;
        case Phase::Executing:
            if (s.tick < 0 || s.tick >= kRegisters) return fail(error, "EXECUTING requires tick 0..2");
            break;
        case Phase::Finished:
            if (s.tick != kRegisters) return fail(error, "FINISHED requires tick 3");
            {
                bool hit = false;
                for (const auto& r : s.robots) hit = hit || (s.target_score > 0 && r.score >= s.target_score);
                if (s.round != s.rounds_total && !hit)
                    return fail(error, "FINISHED requires the last round or a player at the target score");
            }
            break;
    }

    // ---- groups equal the map's, in order ----------------------------------------------------
    if (s.groups.size() != map.groups.size()) return fail(error, "groups do not match the map");
    for (std::size_t i = 0; i < s.groups.size(); ++i)
        if (s.groups[i].id != map.groups[i]) return fail(error, "groups do not match the map");

    // ---- crates: identity and placement ------------------------------------------------------
    std::set<int> loose_tiles;
    int prev_id = 0;
    for (const Crate& c : s.crates) {
        const std::string ctx = "crate " + std::to_string(c.id);
        if (c.id <= prev_id) return fail(error, ctx + ": crate ids must be unique and ascending");
        prev_id = c.id;
        if (c.id >= s.next_crate_id) return fail(error, ctx + ": id is not below next_crate_id");
        if (c.carried) {
            if (c.carrier < 1 || c.carrier > player_count) return fail(error, ctx + ": carrier out of range");
            const Robot& r = s.robot(c.carrier);
            if (r.respawn_pending || r.carrying != c.id)
                return fail(error, ctx + ": carrier does not hold this crate");
        } else {
            if (!map.in_bounds(c.pos)) return fail(error, ctx + ": position out of bounds");
            if (map.blocked(c.pos)) return fail(error, ctx + ": loose crate on blocked terrain");
            if (map.tile(c.pos).terrain == Terrain::Pit) return fail(error, ctx + ": loose crate in a pit");
            if (!loose_tiles.insert(c.pos.y * map.width + c.pos.x).second)
                return fail(error, ctx + ": two loose crates share a tile");
        }
    }

    // ---- robots ------------------------------------------------------------------------------------
    std::set<int> occupied;
    int total_deliveries = 0;
    for (const Robot& r : s.robots) {
        const std::string ctx = "player " + std::to_string(r.id);
        if (r.score != kDeliveryScore * r.deliveries)
            return fail(error, ctx + ": score must equal 5 x deliveries (botched/1 scoring)");
        if (r.damage > r.damage_taken) return fail(error, ctx + ": damage exceeds cumulative damage_taken");
        total_deliveries += r.deliveries;
        if (r.respawn_pending) {
            if (r.carrying != 0) return fail(error, ctx + ": a disabled robot cannot carry a crate");
            if (r.damage == kMaxHealth && r.damage_taken < kMaxHealth)
                return fail(error, ctx + ": damage 3 without three recorded hits");
        } else {
            if (r.damage >= kMaxHealth) return fail(error, ctx + ": an active robot has damage 0..2");
            if (!map.in_bounds(r.pos)) return fail(error, ctx + ": position out of bounds");
            if (map.blocked(r.pos)) return fail(error, ctx + ": position is on blocked terrain");
            if (map.tile(r.pos).terrain == Terrain::Pit) return fail(error, ctx + ": an active robot cannot stand in a pit");
            if (!occupied.insert(r.pos.y * map.width + r.pos.x).second)
                return fail(error, ctx + ": tile occupancy conflict (two robots on one tile)");
        }
        if (r.carrying != 0) {
            const Crate* c = s.crate(r.carrying);
            if (!c || !c->carried || c->carrier != r.id)
                return fail(error, ctx + ": carrying crate " + std::to_string(r.carrying) +
                                       " which is not a crate carried by this player");
        }
    }
    // Every delivered crate was spawned once; ids are never reused.
    if (total_deliveries > s.next_crate_id - 1)
        return fail(error, "deliveries exceed the number of crates ever spawned");
    return true;
}

}  // namespace botched

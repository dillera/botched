# Botched! — Gameplay Evolution Milestones (G1–G4)

**Status:** Proposed roadmap for approval and incremental playtesting  
**Date:** 21 November 2026  
**Baseline:** Playable SDL client, server-managed bots, file-based maps, deterministic `botched/1` engine  
**Related:** `docs/botched-playtest-backlog.md`, especially B-08  
**Working rule:** Implement *one small task at a time*. This document does not authorise all four milestones.

## Goal

Evolve Botched! from a twelve-round crate-delivery race into a factory competition where **delivery, route control, and deliberate environmental sabotage** are viable paths to victory. Keep the three-order programming loop, rotating tick priority, server authority and entertaining event playback. Precision robot pushing remains an emergent interaction, not the primary sabotage strategy.

## Existing behaviour vs new proposals

- **Already implemented:** pits (`O`) disable robots on entry; conveyors and consoles (`C>A`, `TA`); three-hit durability and respawns; stationary directional ray emitters (`L>`, `L<`, `L^`, `Lv`) that fire each tick. Current playtest maps rarely exercise laser damage.
- **Not implemented:** bot personalities. (Score-target victory, player-attributed traps, the proximity burst, the charging/targeting laser turret, tar, pit-push scoring (the SHUNT) and sabotage points are done; see the status blocks below.)
- **Compatibility:** New scoring, traps and end conditions change normative gameplay. Introduce an explicitly versioned rules contract (proposed `botched/2`) when approved; keep `botched/1` fixtures and semantics intact where practical. **Never reinterpret an existing `L>` ray emitter as the new targeting turret.**

## G1 — Score-target victory

**Problem:** The game ends after 12 rounds just as players begin interfering with each other.

**Bounded implementation:**

- Configure a target score (suggested test default **30**) and maximum-round safety cap (suggested **100**, with an optional cap as high as 200). Expose both in match creation and SDL status.
- Complete **all three ticks and round-end effects** before choosing a winner; a player reaching the target first within a round does not win prematurely because of priority.
- If nobody reaches the target by the cap, compare scores and use existing delivery/damage tie-breakers.
- Preserve +5 points per crate delivery. Retain a fixed-round mode for baseline comparisons/tests if straightforward.
- Do not introduce sabotage, hazards or map changes during G1.

**Acceptance:** A real human can play a score-target Crossroads match against bots, with correct target/cap endings and deterministic results. Stop for a playtest before G2.

### G1 — implementation status: **done, awaiting playtest** (2026-11-21)

**What shipped** (additive to `botched/1`; fixed-round matches are byte-identical):

- `CREATE_MATCH` gains `target_score` (0-1000, default 0 = fixed-round mode). `rounds` is the match length without a target and the **maximum-round safety cap** with one (default 100, now allowed up to 200).
- `Engine::end_round` is the only place victory is judged: after all three ticks and `process_respawns`, so rotating tick priority never decides who "got there first". If several players are over the line in the same round the usual ordering applies (score, then deliveries, then least damage taken; full ties share the win). The cap ends a match nobody finished, highest score wins.
- `GameState.target_score` is in snapshots only when set (so no fixed-round hash moves), in `GET_MATCH_STATUS` (lobby and running), validated on checkpoint restore (target must match the configuration; `FINISHED` now needs the last round **or** a player at the target). `MATCH_ENDED` carries `reason: "target"|"cap"` in target mode only.
- SDL client: `--target-score N` / `--rounds N` (the cap); the lobby shows the rules; the title shows `Round 7 (cap 100)` plus a one-line "First to 20 points wins" in the title bar; the scoreboard is titled `FIRST TO N` and every card has a progress bar (gold at the target); the result overlay and log say why the match ended. The terminal client prints the same header. `botched-sim --target-score` for headless runs.
- Scoring (+5 per delivery), maps, hazards and bots are untouched. Nothing from G2-G4 was started.

**Evidence**

| Check | Result |
|---|---|
| Whole suite (normal and ASan/UBSan) | 201/201 test cases, 31 350 assertions, both builds |
| Fixed-round regression | sim hashes still `8b9b82fb718c2751` / `415308a3742c5db6`; `test_idempotency` pins and the training-01 map hashes unchanged |
| Target judged only after the round | `test_score_target.cpp`: leaders already at the target stay unfinished after ticks 1 and 2, finish only after tick 3; events end with ticks 1-3 then `MATCH_ENDED` |
| Same-round multiple finishers | score, then damage taken, then shared victory, each asserted |
| Target boundary / cap | target = score ends after round 1, target + 1 does not; unreachable target ends exactly at the cap with `reason: cap` and the top score wins |
| Protocol | defaults (12 fixed, 100 with a target), range errors (`rounds` 201, `target_score` 1001/-1), `MATCH_CREATED` echo, lobby status |
| Checkpoints | restore with a different target is refused; an impossible `FINISHED` is refused |
| Determinism | `botched-sim --check` identical re-run on `crossroads-01` (3 players, target 30: ended round 20, hashes `47629cb74d7242dd`/`72496e88e4225bb3`) and `training-01` (ended round 14); tests repeat this on `training-01` and `factory-01` |
| Live SDL | server + `botched-client --ui` on `crossroads-01`, 1 human seat plus 2 server bots, target 20: ended in round 19 with P1 and P2 both delivering their 4th crate in that round (20 pts each): shared victory, "Target of 20 points reached", progress bars and headers captured headlessly |

**Notes for the playtest**

- Bot-only simulations on Crossroads need about 20 rounds to reach 30 points with three bots and about 35 rounds to reach 40 with four; 30 is a reasonable first target for three players. The target is a per-match setting, so tune it freely.
- `--rounds` now defaults to "server decides" in the clients (12 fixed, 100 with a target).
- The planning-time per round is unchanged; a 30-point match is long, so use `--planning-seconds` while playtesting.

**Stop gate:** playtest a score-target Crossroads match, then decide on G2. G2-G4 are not started.

## G2 — Environmental hazards and sabotage

**Problem:** Players optimising crate deliveries consistently outperform players attempting to disrupt opponents. Sabotage should be a viable scoring strategy based on controlling dangerous machinery, anticipating routes and creating ambushes — not perfectly predicting robot-to-robot collisions and rotating execution priority.

**Design principle:** Individual hazards should be simple and predictable. Interesting gameplay emerges from their placement, combinations and interactions with conveyors, crates and other robots.

**Sequence: implement and playtest one subtask at a time.**

- **G2-A — Owned proximity traps and sabotage scoring.** Implement multi-target proximity damage, arming, ownership attribution, scoring and SDL animations. Validate this mechanic before adding other hazards.
- **G2-B — Laser hazards and capturable turrets.** First exercise the existing continuous directional laser (`L>`, etc.) in a test map and verify its SDL presentation. Then introduce a separate charging laser turret with capturable ownership, autonomous targeting and damage attribution.
- **G2-C — Tar/slowing hazard.** Introduce a simple movement restriction affecting robots near a tar generator.
- **G2-D — Pit-push scoring.** Award a bonus when one robot directly pushes another into a pit. Preserve existing pit, crate-drop and respawn mechanics.

### Hazard catalogue

| Hazard | Behaviour |
|---|---|
| **Proximity burst** | Covers the eight immediately neighbouring tiles, including diagonals. Starts armed and neutral. During the machinery phase, if one or more robots occupy its affected area, it damages **all robots in range simultaneously** for 1 HP each, then disarms. An adjacent `T` command rearms it and assigns ownership to the activating player. |
| **Charging laser turret** | Charges for two ticks and is then READY (2/2). While READY, on each machinery phase it targets the nearest eligible robot within two tiles along the cardinal directions, fires once for 1 damage, then resets to 0/2 and recharges. With no target it stays READY. Starts neutral; players can capture ownership by entering its associated control tile. |
| **Continuous directional laser** | Existing `L>`, `L<`, `L^`, `Lv` emitters remain unchanged. They fire along a fixed ray every tick and cause 1 damage. They remain neutral environmental hazards and do not award sabotage points. |
| **Pit** | Permanent open hole. Entering immediately disables a robot. Carried cargo drops on the last safe tile if possible. Existing behaviour remains unchanged. |
| **Tar generator** | Affects the eight neighbouring tiles around the generator. Robots within its area have reduced voluntary movement: `F2` advances at most one tile, while `F` and `B` cannot move. Turning, dropping, toggling and waiting remain unaffected. Tar causes no damage. |

### G2-A — Proximity trap rules

**Activation and timing**

1. At the start of a match, map-defined proximity traps are armed but have no player owner.
2. A player on or adjacent to the trap's associated control console may execute `T` to arm or rearm it, gaining ownership (`T` activates every console in range, see *Multi-console TOGGLE* below). Rearming an already-armed trap transfers ownership without triggering it.
3. Proximity traps are evaluated during the machinery phase of each tick, **after all player instructions and conveyor movement**, so robots moved voluntarily, pushed or transported by conveyors are evaluated at their resulting positions.
4. If at least one robot occupies any of the eight neighbouring tiles, the trap activates once and deals 1 damage to every robot in the affected area, including its owner if present.
5. The trap then becomes disarmed and remains inactive until rearmed.
6. If the area is unoccupied, the trap remains armed.
7. A trap's damage is attributed to its current owner for scoring. Neutral traps cause damage but award nobody points.

A player may deliberately wait outside the affected area, attempt a different route, or enter knowing the trap will activate. Multiple robots entering the area during the same tick may all be caught.

**Scoring must be based on actual damage and disables, not inferred intent.**

### G2-B — Charging laser turret rules

**Ownership**

- Each turret has an associated control tile.
- Turrets begin neutral.
- A robot entering the control tile automatically claims the turret.
- Another player may subsequently capture the turret by entering the same tile.
- Ownership changes do not reset or pause the charging cycle.
- Turrets can damage their owner, but self-inflicted damage never awards points.

**Charging and firing**

1. Tick 1: charge (0/2 -> 1/2).
2. Tick 2: charge (1/2 -> 2/2). The turret is now **READY**.
3. At every subsequent machinery phase a READY turret looks for an eligible target. If none exists it stays READY, indefinitely and across round boundaries. When a target is found it fires once and resets to 0/2, starting another two-tick recharge. (Approved after playtesting; the original fixed three-tick cycle always fired on tick 3 of a round.)

When a READY turret checks for a target:

- Select the nearest eligible robot at distance 1 or 2 in a cardinal direction.
- Walls and other blocked terrain obstruct line of sight.
- If multiple targets are equally near, resolve ties using a fixed, documented order (proposed: N, E, S, W, then player ID).
- Fire at most once per cycle, dealing 1 damage.
- If no target is available, fire nothing and **stay READY** (no reset).
- The turret's owner receives any applicable sabotage points when another player is damaged.

The turret's charging state, ownership and firing action must be visible in the SDL client.

The charging turret is **not** a replacement for existing continuous directional lasers. Its map token and control tile must be distinct from the existing `L>` syntax.

### G2-C — Tar rules

Tar is a persistent movement hazard affecting the **eight neighbouring tiles** around a tar generator. It restricts voluntary movement immediately, not during the machinery phase.

- `FORWARD_2` moves at most one tile while in tar.
- `FORWARD_1` and `BACK_1` cannot move while in tar.
- Entering tar during `FORWARD_2` immediately cancels the remaining movement substep.
- Rotations, `DROP`, `TOGGLE` and `WAIT` are unaffected.
- Pushing remains possible through an allowed `FORWARD_2` movement.
- Being pushed or moved by conveyors is unaffected by tar.

Tar is checked after each movement substep, so its effects apply immediately upon entering the affected area, including when entering from outside.

Tar causes no damage and awards no points. No fractional speed, timing counters or additional machinery phase is required.

### G2-D — Pit-push scoring

Reward a player who directly pushes another robot into a pit. This is a difficult tactical action, so it earns **+8 sabotage points**.

- Award points only when a `ROBOT_PUSHED` movement causes an opponent to enter a pit and become disabled.
- Credit the player responsible for the push, including pushes involving a chain of robots.
- Conveyor-driven pushes may also qualify if the pushing robot can be identified.
- No points for walking into a pit, self-disabling, or an unattributable environmental movement.
- Award the bonus once per disabled opponent, even if multiple robots are pushed into the same pit during one tick.
- Existing pit behaviour remains unchanged: immediate disable, crate dropped on the last safe tile if possible, and respawn at round end.
- Include the responsible player and awarded points in authoritative events so SDL can animate and display the bonus.

Push attribution persists until the end of the current round. If a pushed robot subsequently enters a pit through its own orders, conveyor movement or further displacement, the credited pusher receives the +8 bonus. A later push by another player transfers attribution to that player. Attribution is cleared at round end and on disable, so no credit carries into subsequent rounds.

Pit-push points count towards score-target victory and existing tie-breaks.

This mechanic should be named the SHUNT, (e.g. instead of PIT_PUSH_BONUS) with "SHUNT!" being the animated wording.

**Acceptance:** Demonstrate direct and chain pushes into pits, correct attribution and scoring, unchanged crate-drop/respawn behaviour, and deterministic SDL playback. No additional pit mechanics or changes to ordinary pushing.

### Sabotage scoring

| Action | Points |
|---|---:|
| Deliver a crate | **+5** |
| Damage an opponent with an owned proximity trap | **+1 per damaged opponent** |
| Damage an opponent with an owned charging laser turret | **+1 per hit** |
| Disable an opponent using owned damaging machinery | **+3 additional bonus** |
| Directly push an opponent into a pit | **+8** |
| Damage yourself with owned machinery | **0** |
| Neutral environmental damage | **0** |
| Toggle, rearm or capture machinery without causing damage | **0** |
| Slow an opponent using tar | **0** |

A successfully attributed disabling hit earns both the ordinary damage point and the disable bonus.

Examples:

- Owned proximity trap damages two opponents: **+2**.
- Owned proximity trap damages two opponents and disables one: **+5**.
- Owned turret hits and disables one opponent: **+4**.
- Directly push an opponent into a pit: **+8**.

A single disable cannot award multiple disable bonuses. Damage and scoring events must identify their source and responsible player unambiguously.

No points are awarded for ordinary robot pushes, except a direct push that causes a pit disable.

### Deterministic resolution and attribution

- Player orders retain existing rotating tick priority.
- Conveyor movement is resolved according to the existing rules.
- Proximity traps activate after conveyor movement during machinery resolution.
- Each trap applies its entire multi-robot activation as one deterministic event group before disarming.
- Charging turret firing and targeting must use deterministic state and tie-breaking rules.
- If multiple hazards affect the same robot during one tick, the engine must preserve a fixed resolution order and prevent duplicate disable bonuses.
- Direct pit-push credit requires evidence from the engine's actual push-chain resolution. Walking or voluntarily driving into a pit awards nobody a push bonus.
- Damage, ownership changes, arming, activation, firing, scoring, disables and crate drops must be represented by authoritative events that SDL can replay without recalculating gameplay.

**Before implementing G2-A**, settle and document the precise ordering between proximity traps, existing continuous lasers and future charging turrets. Keep this narrow: a clear phase order and a small set of edge-case tests are sufficient.

### G2-A — implementation status: **done, awaiting playtest** (2026-11-21)

**Resolution order (settled before implementing, as the roadmap required).** Each tick: 1 orders (rotating priority) → 2 conveyors → 3 continuous lasers (unchanged) → **3b proximity traps (ascending trap id)** → 4 disable check. Traps see final positions and every source's damage is judged once. Edge rules: a robot already at 3 damage after the lasers takes no trap damage (so a disable has exactly one source and one bonus); a robot in two trap areas is hit by both; arming happens in phase 1 and the same tick's trap phase judges occupancy, so arming a trap whose area is occupied fires it at once. Full list: `docs/rules-interpretations.md` #25-30.

**What shipped**

- Map tokens `X<n>` (trap device, solid) and `K<n>` (control console), validated (every trap needs a console and vice versa, ids unique). New map: `resources/maps/trapworks-01.map` (12x10, 2-4 players, one trap in the middle with a dispenser inside its area and a `K1` console on each side wall).
- Rules: trap state in the world (`traps: [{id, armed, owner}]`, per-player `sabotage`; both appear only on maps with traps), `TOGGLE` next to a `K` console arms/re-arms and claims, activation/damage/disarm, attributed scoring (+1 per damaged opponent, +3 for a disabling hit, 0 for self or neutral), `score = 5 x deliveries + sabotage`, so sabotage also counts for score targets and tie-breaks.
- Events (all authoritative, nothing recomputed by clients): `TRAP_ARMED`, `TRAP_TRIGGERED` (lists everyone caught), `TRAP_HIT`, `SCORE_AWARDED` (who, why, victim, trap, new score), and `ROBOT_DISABLED` with `cause: "TRAP"`, `trap`, `by`.
- Versioning: kept `botched/1`. Everything is gated on map tokens, so every existing map plays, serialises and hashes exactly as before; the roadmap's proposed `botched/2` was **not** introduced. Say if you would rather bump the rules version when G2 completes.
- SDL: trap and console sprites (armed = owner-coloured spikes, off = grey), the eight tiles tinted in the owner colour while armed, replay steps for arming ("ARMED" ring), the burst (area flash + shockwave), `-1` on each victim and a gold `+N` for the owner, the scoreboard's `sabotage +N`, a `Traps:` line, tile hover text, narration lines in both clients, and the terminal board (`X1!` armed, `x1` off, `K1`).

**Evidence**

| Check | Result |
|---|---|
| Whole suite, normal and ASan/UBSan | 216/216 test cases, 31 505 assertions, both builds (15 new in `tests/test_traps.cpp`) |
| Existing behaviour | `training-01` sim hashes still `8b9b82fb718c2751` / `415308a3742c5db6`; training-01 map hashes and every older pin unchanged; snapshots of trap-less maps carry no `traps`/`sabotage` (tested) |
| Map | parse/serialise round-trip (text and `GET_MAP` JSON), validation errors for a missing console, an orphan console, a duplicate id and bad tokens; the device blocks movement |
| Rules | unoccupied trap stays armed and distance-2 robots are safe; neutral trap hurts once and scores 0 (disarmed for ticks 2-3); owned trap hits 3 robots at once, owner scores +1 per opponent and 0 for itself; disabling hit pays +1 and +3 exactly once; self-disable and neutral disable pay 0; laser-felled robot gets no trap damage or bonus; `T` arms and claims and the same tick's burst scores for the new owner; re-arm transfers an armed trap without firing; `T` away from consoles is still `NO_CONSOLE` |
| Persistence / determinism | checkpoint restore round-trips traps and sabotage and rejects a score that does not add up; same scenario twice gives the same state hash; sabotage reaches a score target |
| Playback | the replay of arm, burst, hits, scores and a trap disable ends on the authoritative board (field-by-field) |
| Maps in simulation | `trapworks-01` with 2, 3 and 4 bot players finishes and re-runs identically (`--check`) |
| Live SDL | server + two scripted `--ui` clients on `trapworks-01`: P1 walks to the console and `T`-claims the trap in round 2, P2 walks into its area in round 3; captured headlessly: replay of "P1 takes over trap 1", the machinery-phase burst ("TRAP 1 (P1) fires: catches P2"), then `+1 for P1`, scoreboard `1 pts / sabotage +1`, P2 damage pip, trap shown "off (P1)" |

**Notes for the playtest**

- Server bots ignore traps for now (they never press `T`), so a human needs to claim the trap; neutral traps still hurt bots that wander in. Bot personalities and deliberate sabotage are G4.
- `T` next to a belt console still toggles the belt group; a `K` console is only used when no belt console is adjacent. Keep the two kinds of console apart on maps.
- Not started: charging turrets, tar, pit-push scoring, new hazards beyond the proximity trap, other maps, bot changes.

**Playtest findings (live SDL, accepted).** G2-A is accepted.

- A persistent trap and multi-tick arming both worked: a claim made in one round stayed in force and fired in a later one.
- P2 escaped the blast area on a single tick (the burst only judges where robots stand when the machinery phase runs).
- Friendly fire behaved correctly: the owner is damaged like anyone else and earns nothing for it.
- A mixed delivery/sabotage strategy was enjoyable.

*Observations for later balancing (not blockers):* server bots repeatedly walk into armed traps, and delivery scoring still dominates sabotage. Both belong to G4 (bot behaviour) and G3 (map design and scoring balance).

### G2-B step 1 — existing continuous lasers on one small map: **done, awaiting review** (2026-11-21)

Scope was deliberately narrow: **no rules, scoring or mechanics changed** (the engine, state, events and clients are untouched). The roadmap asks to exercise the existing `L>`/`L<`/`L^`/`Lv` emitters before building anything new, so this step adds one map and the tests that pin current behaviour.

- **Map:** `resources/maps/laserworks-01.map` (12x10, 2-4 players). `L>` at (1,5) and `L<` at (10,5) fire along row 5 and stop at the walls (4,5) and (7,5), so (2,5), (3,5), (8,5), (9,5) are lit and (5,5), (6,5) never are. Crossing north/south is a gamble through a lane or the longer safe way through the gap. The lasers stay neutral (no points).
- **Verified (`tests/test_lasers.cpp`, 8 cases, all passing):**
  - the map validates for 2, 3 and 4 players and has exactly two beams of two lit tiles each;
  - a robot in a lit lane takes 1 damage per tick (`LASER_HIT` with the emitter position), is disabled on the third hit (`ROBOT_DISABLED`, cause `LASER`), and robots in the gap or beside a stopping wall take nothing; no `SCORE_AWARDED`, scores stay 0;
  - the east lane works the same way;
  - a disabled robot carrying a crate drops it on the tile where it fell (`CRATE_DROPPED`), the loose crate is not harmed by later ticks, and if a loose crate already lies there the carried crate is destroyed (`CRATE_DESTROYED`, `DROP_BLOCKED`);
  - the robot respawns at round end on its spawn with damage 0 and `damage_taken` 3;
  - SDL playback of hits, the disable, the crate drop and the respawn replays to exactly the authoritative board (two `Laser` steps, one `Disable`, one `Drop`, one `Respawn`);
  - bots finish 12 rounds with 2-4 players, with identical state and event hashes on re-run (they avoid the lit lanes, so the map shows no laser damage in bot play: a human is needed to test the gamble).
- **Live SDL:** server + two scripted `--ui` clients: P1 walks to (3,5) and waits; captured the beam lines from both emitters, the machinery phase, the explosion at (3,5) on the third hit with two damage pips, "P1 hit by a laser (damage 3)", "P1 DISABLED (LASER)" and the respawn at (1,1).
- **Whole suite:** 224/224 test cases, 31 605 assertions; ASan/UBSan run also passes; `training-01` hashes unchanged.

Not started: charging or capturable turrets, any new mechanic, scoring or rule change, bots using lasers deliberately.

**Stop gate:** review `laserworks-01` (and playtest it with humans if desired) before deciding whether to proceed to the charging turret.

### G2-B charging turret — implementation: **done, awaiting playtest** (2026-11-22)

Continuous lasers are accepted and unchanged (neutral, no points). This step adds the separate charging turret and nothing else: no tar, pit-push, G3/G4 work or bot changes.

**What shipped**

- Map tokens `Y<n>` (turret device, blocked) and `P<n>` (control pad, walkable), validated like trap/console pairs. New map `resources/maps/turretworks-01.map` (12x10, 2-4 players): `Y1` at (5,4), dispenser G1 one tile north of it, a wall at (5,6) shortening the southern shot, and a `P1` pad on each side at (2,4) and (9,4), both out of range.
- Rules (revised after playtest, see "READY revision" below): neutral at the start; two charging ticks, then READY until a shot is actually fired (the charge is world state, so READY persists across rounds and ownership changes never touch it); target = nearest robot at distance 1-2 along N/E/S/W with no blocked tile between, ties N, E, S, W; one shot, 1 damage, or no shot when nothing is in range; the owner can be hit; capture by entering a pad; sabotage scoring identical to traps (+1 per damaged opponent, +3 more for the disabling hit, 0 for self or neutral).
- Settled ordering (`docs/rules-interpretations.md` #31-36): 1 orders, 2 conveyors, 3 lasers, 3b traps, **3c turrets (ascending id: capture, cycle, fire)**, 4 disables. A robot already felled by a laser or trap this tick is not a turret target, so each disable has one source and one bonus. "Entering" means arriving on a pad this tick; standing on one claims nothing; with two entrants the owner keeps it, otherwise pad scan order decides.
- Events (authoritative, nothing recomputed by clients): `TURRET_CAPTURED`, `TURRET_CHARGING` (1 or 2), `TURRET_FIRED` (victim, owner, direction, distance, damage, points), `SCORE_AWARDED` with reasons `TURRET_DAMAGE` / `TURRET_DISABLE` (carrying `turret`), `ROBOT_DISABLED` with `cause: "TURRET"`, `turret`, `by`. Snapshots and checkpoints gain `turrets: [{id, charge, owner}]` only on turret maps; `sabotage` now appears on trap or turret maps.
- Versioning: still `botched/1`, additive and gated on the tokens, as with traps.
- SDL: turret with a barrel on each side and two charge pips (glows at 2), owner colour, a faint cross over the tiles in range, pad with an owner-coloured ring, replay steps for charging (inward ring), capture ("CAPTURED") and firing (beam, impact, `-1`), the gold `+N`, a `Turrets:` line in the robot panel, scoreboard `sabotage +N`, tile hover text, narration in both clients and the terminal board (`Y1` plus charge mark, `P1`).

**Evidence**

| Check | Result |
|---|---|
| Whole suite, normal and ASan/UBSan | 239/239 test cases, 31 826 assertions, both builds (15 new in `tests/test_turrets.cpp`) |
| Existing behaviour | `training-01` sim hashes still `8b9b82fb718c2751` / `415308a3742c5db6`; all earlier pins and maps unchanged; training-01 snapshots carry no `turrets` or `sabotage` (tested) |
| Map | validates for 2-4 players, text round-trip, orphan pad / orphan turret / duplicate id / bad id rejected, the device blocks and the pad does not, no `L` emitters |
| Cycle | (superseded by the READY revision below) |
| Targeting | nearest wins; N2 loses to E1; ties N > E > S > W; distance 3 and diagonals never hit; W2 is in range; a wall at distance 1 hides a robot at distance 2 |
| Scoring and attribution | owned hit +1; disabling hit +1 and +3 once (`by`, `turret` on the event); crate drops where the robot fell; owner self-hit and self-disable score 0; neutral hit and disable score 0; a laser-felled robot is not targeted and its disable stays a laser disable with no bonus |
| Ownership | entering a pad claims, a second player entering the other pad takes it (previous owner reported), charge unaffected, the capture on a firing tick owns that shot; standing on a pad from the start claims nothing; simultaneous entrants resolve by pad order |
| Persistence / determinism | checkpoint restore round-trips `turrets`, rejects charge 3, owner 3 and a wrong id; `turretworks-01` with 2, 3 and 4 bots finishes 12 rounds and re-runs identically (`--check`) |
| Playback | capture, two charges, fire, both scores, disable and crate drop replay to the authoritative board (field-by-field, including turret charge and owner) |
| Live SDL | server + two scripted `--ui` clients on `turretworks-01`: P1 walks onto the pad (round 2), P2 walks into range; captured headlessly: replay "TURRET 1 (P1) fires W and hits P2 (damage 2)", the beam and `-1` on P2, owner-coloured turret and range cross, `+1 for P1`, then in round 4 "P2 DISABLED (TURRET)" and `+3` (score 6); P1 also shot by its own turret later with no points |

**Notes for the playtest**

- (Superseded: the fixed tick-3 firing was replaced by the READY behaviour below.)
- Server bots do not claim turrets deliberately (G4). They plan on the real rules engine, so they see the damage but not the ownership opportunity.
- The next roadmap items (G2-C tar, G2-D pit-push) are not started.

**READY revision (approved after live playtest, 2026-11-22).** Playtesting showed the fixed three-tick cycle wasted shots whenever nobody happened to be in range on tick 3. The turret now keeps full charge until a shot is actually fired: two charging ticks, then READY (2/2); each machinery phase while READY it checks for a target, fires once (resetting to 0/2) if there is one, and otherwise stays READY indefinitely, across round boundaries included. Targeting, ownership, damage, scoring and animations are unchanged. Consequences:

- The `TURRET_IDLE` event is gone (a READY turret with no target emits nothing); `TURRET_CHARGING` (1, 2) and `TURRET_FIRED` are unchanged, and `charge` in snapshots is still 0-2 (2 = READY). The playback step and narration for "no target" were removed with it.
- The tick that reaches 2/2 only charges; the earliest shot is the following tick (a fresh turret therefore first fires on tick 3).
- A target that stays in range is hit once per recharge, i.e. every third tick.
- READY is shown everywhere: a pulsing "READY" label and glowing barrels on the SDL turret, a brighter range cross, `Turrets: Y1 READY (P1)` in the robot panel, and `READY` in the terminal board, legend and tile description.
- Evidence: 242/242 test cases, 31 858 assertions, normal and ASan/UBSan; `training-01` hashes unchanged; `turretworks-01` with 2-4 bots deterministic. New/changed tests: READY held across a round boundary and for three further empty rounds (never recharges, never fires); a missed opportunity then a first shot when a robot steps into range (fires on that tick, recharges ticks 2-3 and is READY again); a target left in range is hit every third tick; wall-hidden robot leaves the turret READY; playback of a READY turret across a round boundary equals the authoritative board.

**Stop gate:** playtest `turretworks-01` with humans before deciding anything further.

### G2-C tar — implementation: **done, awaiting playtest** (2026-11-22)

Implements the approved "G2-C — Tar rules" section as written. No damage, no points, no new world state, no bot changes; G2-D is not started.

**What shipped**

- Map token `A` (no id): a tar generator, solid like a wall or turret. The eight tiles around it (Chebyshev distance 1, walls do not shield, the generator's own tile is not tar) are tar, via `Map::in_tar`. Tar is a property of the map, so snapshots, checkpoints, hashes and every existing map are unchanged.
- Rules, in the order the section lists them:
  - `FORWARD_1` and `BACK_1` cannot move a robot that starts in tar: `ORDER_NO_OP` with `reason: "TAR"` (the robot stays put and pushes nothing). Entering tar from outside with them is fine.
  - `FORWARD_2` makes its first one-tile substep as usual. A robot that **started in tar** is limited to that one substep even if it steps out of the tar (the allowance is fixed at instruction start, so `FORWARD_2` is the way out). A robot that started outside has the second substep cancelled if the first tile put it in tar (`TAR_SLOWED` event); entering tar on the *second* substep leaves nothing to cancel.
  - Rotations, `DROP`, `TOGGLE`, `WAIT` are untouched (they keep their normal no-op reasons).
  - A robot in tar can push with an allowed `FORWARD_2`; pushed robots and conveyor movement ignore tar.
- Interpretation recorded in `docs/rules-interpretations.md` #37-41. (Fixed after the first playtest: a `FORWARD_2` that starts in tar is limited to one tile even when that tile leaves the tar; my first reading let it continue.)
- Map: `resources/maps/tarworks-01.map` (12x10, 2-4 players): generator at (5,4) with dispenser G1 inside the tar, so the central crate is a slow grab while the outer ring is fast.
- SDL: a tar generator sprite, the eight tar tiles tinted purple with slow bubbles (always visible), tile hover text and the robot panel's "On: tar: F2 moves 1 tile, F and B cannot move", a replay step with a purple pulse and "TAR!" for `TAR_SLOWED` and for a no-op in tar, plus narration ("stuck in tar, cannot move", "slowed by tar: FORWARD_2 stops after one tile"). The terminal board shows `~` on tar and `A`.

**Evidence**

| Check | Result |
|---|---|
| Whole suite, normal and ASan/UBSan | 252/252 test cases, 32 118 assertions, both builds (8 new in `tests/test_tar.cpp`) |
| Existing behaviour | `training-01` sim hashes still `8b9b82fb718c2751` / `415308a3742c5db6`; snapshots carry no tar, trap or turret keys on tar maps |
| Map | validates for 2-4 players; exactly the 8 neighbours are tar, the generator tile and tiles two away are not; text round-trip; `A1` rejected |
| Rules | `F`/`B` in tar: no move, `reason TAR`; `F` from outside into tar moves; `F2` from outside into tar stops after the first tile; entering tar only on the second substep ends there with no event; `F2` inside tar moves one tile; **`F2` that starts in tar moves one tile even when that tile leaves the tar (regression test), after which a normal `F2` moves two; repeated `F2` is the way out**; `F2` outside tar moves two; turns/`WAIT` unaffected and `DROP`/`TOGGLE` keep their usual no-op reasons |
| Pushing / belts | a robot in tar pushes with `FORWARD_2` (pushed robot not restricted), `FORWARD_1` in tar cannot push; a belt inside tar carries a robot out unaffected; no damage and no `SCORE_AWARDED` |
| Playback | slowed `F2` plus two stuck orders give 3 `TarSlow` steps and the replayed board equals the authoritative one |
| Determinism | `tarworks-01` with 2, 3 and 4 bots finishes and re-runs identically |
| Live SDL | server + two scripted clients on `tarworks-01`: tar tiles and generator drawn, P1 walks in, then `F2` from inside shows "P1 is slowed by tar: FORWARD_2 stops after one tile" and the "TAR!" pulse |

**Notes for the playtest**

- Server bots plan on the real rules engine, so they feel tar but do not seek or avoid it deliberately (G4).
- The central dispenser G1 sits inside the tar; a crate carried out of it is slowed on the way, as intended.

**Fix after playtest (2026-11-22).** `F2` starting in tar previously moved two tiles when the first tile left the tar; it now stops after one. Only the instruction-start allowance changed: entry from outside (immediate stop), stuck `F`/`B`, pushing and belts are as before. Evidence: 252/252 test cases, 32 123 assertions (normal and ASan/UBSan), `training-01` hashes unchanged, `tarworks-01` 2-4 bot runs deterministic.

**Stop gate:** playtest `tarworks-01` before G2-D.

### G2-D SHUNT (pit-push scoring) — implementation: **done, awaiting playtest** (2026-11-22)

Implements the approved "G2-D — Pit-push scoring" section, including the persistent push attribution and the SHUNT naming. Existing pit, crate-drop and respawn mechanics, ordinary pushing and every other rule are untouched. G3/G4 are not started.

**What shipped**

- Attribution: each robot carries `pushed_by` (player id, 0 = none). Whenever a movement pushes robots (`ROBOT_PUSHED`), the robot that issued the move (by order **or by belt**) becomes `pushed_by` of every robot in the chain; a later push by someone else overwrites it. It is cleared when the robot is disabled and for everyone at round end, so no credit crosses a round boundary.
- Scoring: when a robot enters a pit, if `pushed_by` names another player that player earns **+8** (`kShuntScore`): `SCORE_AWARDED` with `reason: "SHUNT"`, `victim`, `points`, new `score` (no `trap`/`turret` key), and `ROBOT_DISABLED` (cause `PIT`) now carries `by` for an attributed pit disable. A robot that walks in on its own, falls with no live attribution or whose attribution is itself earns nobody anything. A disable happens once per robot, so each disabled opponent pays once, even when two robots are shunted into the same pit in one tick. `score = 5 x deliveries + sabotage`, so SHUNT points count towards score targets and tie-breaks.
- Pit behaviour is exactly as before: immediate disable, crate dropped on the last safe tile (or destroyed on `DROP_BLOCKED`), respawn at round end, no damage.
- State: `pushed_by` is serialised in snapshots/checkpoints **only while non-zero** (a mid-round fact), so a mid-round save/restore keeps the credit; `sabotage` now appears in a snapshot once anyone has earned some even on maps without traps/turrets. Checkpoint validation rejects an out-of-range or self pusher, or one on a disabled robot. Still `botched/1`, additive.
- Fixed a latent bot quirk this exposed: the bot planner's look-ahead treated *any* score increase as a delivery (+100000), so it would have chased SHUNTs (and trap/turret points) as if they were deliveries. It now checks `deliveries` instead. On maps without sabotage this is identical, which is why the default sim hashes did not move.
- Map: `resources/maps/shuntworks-01.map` (12x10, 2-4 players): three pits in a trident, (4,4), (6,4) and (5,6), dispenser G1 at (5,3) between the upper two, walls for cover and push lines.
- SDL: the pit explosion's caption names the pusher ("pushed in by P1"), then a **"SHUNT!"** step with a shockwave, big "SHUNT!" wording and a gold `+8` over the pusher, the scoreboard's `sabotage +8`, and narration lines in both clients. The SHUNT step belongs to the orders phase (it does not start the machinery phase).

**Evidence**

| Check | Result |
|---|---|
| Whole suite, normal and ASan/UBSan | 264/264 test cases, 32 332 assertions (12 new in `tests/test_shunt.cpp`) |
| Existing behaviour | `training-01` sim hashes still `8b9b82fb718c2751` / `415308a3742c5db6` |
| Map | 3 pits, valid for 2-4 players |
| Direct and chain pushes | P1 pushes P2 into (4,4): +8 for P1, `ROBOT_DISABLED by 1`, crate dropped where P2 stood, respawn at round end, no damage; P1-P2-P3 in a row: P3 falls, **P1** is credited, the middle robot scores nothing |
| No-credit cases | walking into a pit: no `SCORE_AWARDED`, no `by` |
| Attribution lifetime | pushed on tick 1, walks into the pit on tick 2: +8 to the pusher; pushed by P1 then by P3: P3 is credited, P1 gets 0; pushed on tick 3 and walks into the pit next round: nothing (attribution cleared at round end) |
| Once per disabled opponent | two shunts into the same pit in one tick: +8 each to two different pushers, nobody scores twice |
| Belt pushes | a belt driving P1 into P2 and P2 into the pit credits P1 |
| Score target / persistence | +8 reaches a target of 8 and the match finishes after the round; mid-round save/restore keeps `pushed_by` and the credit; a self-pusher is refused |
| Playback | SHUNT step (orders phase), the disable and the crate drop replay to the authoritative board, `score`/`sabotage` 8 |
| Determinism | `shuntworks-01` with 2-4 bots finishes and re-runs identically |
| Live SDL | server + two scripted clients: P1 pushes P2 in round 4; captured "P1 pushes P2", the pit explosion with "P2 DISABLED (PIT), pushed in by P1", then "SHUNT! +8" and "SHUNT! +8 for P1: P2 pushed into a pit (score 8)" |

**Notes for the playtest**

- Server bots do not seek shunts deliberately (G4); they only avoid pits as before.
- "Opponent" is every other player; there are no teams.

**Stop gate:** playtest `shuntworks-01` before G3.

### G3 map — implementation: **done, awaiting playtest** (2026-11-22)

One new map, `resources/maps/foundry-01.map` ("The Foundry", 11x13, 2-4 players). **No engine, rules or bot change**: the map is built only from the G1-G2 tokens, and `training-01`, `crossroads-01`, `factory-01` and the G2 playtest maps are untouched. G4 is not started.

**Design**

- **Three dispensers for up to four players, none near a dock** (every dock-to-dispenser walk is at least 6 tiles, and the three journeys are different kinds of trouble):
  - **G3, centre pocket (5,6)** - the contested prize. Pits (4,6)/(6,6) and walls close it on both sides; mouths (5,5) and (5,7), equally far from the north and south seats. No machinery, but the four flank tiles beside the pits are SHUNT lines (+8), so everyone's nearest crate is also the most dangerous place to stand.
  - **G1, north dead end (5,1)** - top of a one-wide corridor (5,2)-(5,4) walled in by **two traps**, X1 (4,3) and X2 (6,3). Both rings cover the whole corridor: the trip up and the trip down are two passes through a murder passage. Armed from K1 (1,5) / K2 (9,5), far across the floor, so the owner has to guess when a carrier is inside.
  - **G2, south dead end (5,11)** - the mirror corridor (5,8)-(5,10), walled in by **two tar generators** A (4,9) / A (6,9), so every tile crawls (FORWARD_2 moves one tile). **Turrets** Y1 (3,8) / Y2 (7,8) cover the corridor mouth and the lower belts; two pads each claim them (P1 (2,7), (4,7); P2 (6,7), (8,7)).
- **Conveyor shortcuts home:** rows 3 and 9 each carry a short lane W/E straight onto the dock. Group A is the left pair, B the right; consoles TA (3,5) / TB (7,5) reverse their own group, so a rival can send your carrier back out, into the trap ring or the tar. Pits at (2,4)/(8,4) (above the upper lanes) and (2,8)/(8,8) (beside the lower) let someone standing next to a rider shove him in. The safe-but-slower alternative is walking the open floor beside the lanes.
- **Symmetry:** exactly left-right mirrored; top-bottom mirrored in route structure (traps <-> tar), so every seat gets the same game.

**Design iterations recorded (map-only; engine untouched)**

1. First draft (13x13, one trap ring across the only route to the centre and the north dispenser): the neutral trap starts armed and the planner treats damage as a wall, so all bots stalled (about 1 delivery per match). Fix: keep trap rings off the centre pocket.
2. A single-mouthed centre starved the south seats (P3/P4 near 0 points); a second mouth plus a south corridor mirroring the north one fixed it.
3. The board was tightened to 11x13 for cycle length. 2-3 rounds per delivery is a guideline, not a rule.

**Evidence**

| Check | Result |
|---|---|
| Whole suite | 275/275 test cases, 32 744 assertions (11 new in `tests/test_foundry.cpp`) |
| Existing behaviour | `training-01` sim hashes still `8b9b82fb718c2751` / `415308a3742c5db6`; the three reference maps still load |
| Map | validates for 2, 3 and 4 players; 3 dispensers, 2 traps + 2 consoles, 2 turrets + 4 pads, 2 tar generators, 6 pits, both belt groups |
| Layout | left-right terrain identical; walking distance from each seat to each dispenser matches its mirror; every dock >= 6 tiles from every dispenser |
| Hazards | north corridor tiles inside both trap rings (dispenser outside); south corridor tiles tar (dispenser not); turrets cover the mouth and lower belts but not the pocket or either dispenser; the pocket touches no ring, tar or turret line |
| Engine | one step into the north corridor: 2 `TRAP_TRIGGERED`, 2 hits; south corridor: `FORWARD_1` no-op x3, `FORWARD_2` moves one tile; a robot waiting at the mouth is shot once per round by the neutral turret; a push from above on a flank tile is a SHUNT (+8) |
| Bots | 2-4 players finish and re-run identically; 6 seeds x 4 bots: at least 10 deliveries, sabotage points scored, **all four seats deliver** |
| Bot balance (16 seeds, 4 bots, total score by seat) | P1 97, P2 109, P3 88, P4 126, with SHUNTs, trap and turret hits and crate drops happening; bots mostly fight over the centre and do not farm a corner |
| Live SDL | server + 3 bots, headless screenshot: both trap rings, tar corridor, turrets and pit flanks all read clearly; spawns, docks and belts are as designed |

**Notes for the playtest**

- Bots do not use consoles or pads on purpose (G4): the traps and turrets they meet are the neutral, armed-at-start ones, so human players are the ones who can really exploit the north and south corridors.
- Tune the map file, not the engine: candidate knobs are pit and wall placement near the pocket, lane length, and the pad positions.

**SDL layout fixes (after the first playtest of `foundry-01`, UI only):** the hover/replay caption now wraps onto two lines to the right of the "FACTORY FLOOR" label instead of running over it; the right column (scoreboard / your robot / your orders) is sized to its content (scoreboard by player count, robot panel by its status lines, `compute_layout(w, h, players, robot_lines)`) and always ends on the board panel's bottom edge, so the log sits cleanly underneath; the score-target progress bar no longer touches the deliveries line. New `--hover x,y` client flag places the mouse on a board tile for headless screenshots. Covered by a layout test (276/276 test cases); no rules change.

**Stop gate:** playtest `foundry-01` before G4.

### Multi-console TOGGLE (after the `foundry-01` playtest) — **done, awaiting playtest** (2026-11-22)

`TOGGLE` now activates **every** console (belt `T` or trap `K`) on the robot's own tile or any of the four orthogonally adjacent tiles, each exactly once per instruction, in row-major order (y, then x). Each console emits its normal event (`GROUP_TOGGLED` / `TRAP_ARMED`) after the single `ORDER_EXECUTED`; with no console in range the order is still `ORDER_NO_OP` `NO_CONSOLE`. It replaces the old "one console wins" rule (belt console first, then lowest id). Motivation: on `foundry-01`, standing at (2,5) between K1 and TA used to toggle only the belts; now it does both, which is a map-design matter rather than a rule one. Two belt consoles of the same group in range cancel out (per-console rule). Still `botched/1`; no snapshot or event shape changes. Details: `rules-interpretations.md` #47.

Evidence: 282/282 test cases (new `tests/test_toggle.cpp`: K1+TA at (2,5) in row-major order, own-tile console, row-then-column ordering, once-per-console incl. same-group cancel, NO_CONSOLE, lone consoles unchanged; the old "smallest id" test in `test_machinery.cpp` now asserts both toggle); `training-01` hashes unchanged (bots never TOGGLE).

### Presentation requirements

Players should be able to understand each hazard's state by inspecting the map:

| Hazard | Visible information |
|---|---|
| Proximity trap | Armed/disarmed, affected area and owner |
| Charging turret | Charge progress, owner, range and firing effect |
| Continuous laser | Fixed direction and beam |
| Pit | Always visible as a permanent hazard |
| Tar | Clearly marked slowing area |

A client should explain why a robot took damage, which device caused it and whether another player earned points.

Smooth animations are encouraged in SDL, but all effects must remain representable as discrete state changes on constrained retro clients.

### Implementation boundaries

- Preserve the existing three-instruction programming system.
- Do not introduce weapons, inventory, ammunition, energy management or a general trap-scripting engine.
- Reuse existing consoles, terrain parsing, deterministic events and SDL playback patterns where practical.
- New normative mechanics require the agreed rules-version transition; do not silently change `botched/1`.
- Keep existing maps and deterministic fixtures functional where practical.
- Implement one hazard type at a time, with focused tests and a playable demonstration.
- Stop after each subtask for human playtesting and approval.

**G2 acceptance:** A human can deliberately control environmental machinery, damage or disable another player, receive the correct attributable score and understand the entire interaction through SDL playback.

The goal is not to make every factory dangerous. It is to give map designers a small collection of understandable machinery that can create contested routes, ambushes, shortcuts and spectacular failures.

## G3 — Maps designed for interactions

**Problem:** Short safe dispenser-to-dock loops let delivery-specialist bots avoid the rest of the factory.

**Bounded implementation:**

- Retain all the test maps like `training-01`, `crossroads-01` and `factory-01` as reference/playtest resources. Don't silently replace them.
- Design **one** new external `.map` around contested pickups, short routes, meaningful conveyor shortcuts, trap-controlled pinch points / “murder passages,” and a safe but less efficient alternative.  Utilize all the hazard elements created so far. Be creative, and don't just plonk dispensers next to delivery points like the test maps had. Have hazards be meaningful, imagine the journey a bot has to take to get its reward, and how an opponent could thwart it. This is the game design challenge. Interesting, fun, reward game play.
- Have fewer dispensers than players, at locations difficult to get to, to encourage varied game play.
- Test with 2–4 players including imperfect server bots. Avoid universal Manhattan-distance rules, procedural generation, map editor or hot-loading implementation.

**Acceptance:** Human playtests show contested routes and meaningful machinery usage; bots do not merely farm isolated corners. Adjust the map resource, not the engine, to improve layout balance.

## G4 — Bot personalities and persistent goals

**Goal:** Bots should be recognisable opponents with coherent tasks, not identical delivery optimisers or wholly random movers.

Each bot has a **personality** and a **current plan**. Choose the next goal probabilistically from the personality, then retain it until it completes, becomes impossible, or a significant event forces replanning. Continue to calculate just **three orders per round**, from public information, using the existing planner.

**Starting weights for selecting the next goal (illustrative, tune after playtesting):**

| Personality | Haul / deliver | Mixed positioning / opportunism | Hunt / arm traps |
|---|---:|---:|---:|
| **Passive — Cargo hauler** | 80% | 15% | 5% |
| **Normal — Opportunist** | 50% | 30% | 20% |
| **Aggressive — Saboteur** | 20% | 25% | 55% |

Goal examples: collect an accessible crate, return home with cargo, move to and arm an important trap, pressure a rival by occupying or controlling their likely route. Hunting should prefer **environmental disruption** to exact-tick pushes. If the chosen goal is unavailable or nonsensical, resample or fall back to useful delivery movement.

Keep seeded deterministic variation, limited search and occasional imperfect choices. Bots must obey identical rules, cannot see unpublished human orders and must not become stuck waiting for unavailable traps. Expose a personality label in the scoreboard only if it is cheap.

**Acceptance:** One human can play mixed-personality matches; visible differences emerge in goals and behaviour; bots still deliver crates and play reproducibly under fixed seeds. No new AI framework, learning system or difficulty hierarchy.

## Parked — later, not part of G1–G4

- **Positive pickups:** occasional temporary map bonus, e.g. *full health*, to enable clutch deliveries. Define spawn and fairness semantics only after damage/traps work.
- Teleporters; fog of war and personal visibility radius; irregular board silhouettes; additional trap types; sound cues; replay history; live map registration with administrator authentication.
- Retro binary transport, Amiga and constrained-platform clients: separate platform milestones, not additional requirements for G1–G4.

## Scope control / stop gates

1. Implement **G1 only** next, verify, commit, update this file and stop for playtest.
2. Approve **G2-A** trigger/ownership/scoring semantics before coding. G2-B and G2-C are **separate, optional follow-on tasks**.
3. Build one G3 map after seeing which hazards create worthwhile encounters.
4. Implement G4 when useful sabotage goals exist; don't build personalities against imagined traps.
5. No new trackers or broad refactoring. Report status, commit and a short playtest observation in this document.
6. Game engine remains authoritative; clients only animate semantic events at their chosen speeds.

**Next agent instruction: G1 only. Documenting G2–G4 is not permission to implement them now.**

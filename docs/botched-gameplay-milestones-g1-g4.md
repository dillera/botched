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
- **Not implemented:** score-target victory, player-attributed traps, a single-use proximity burst, charging/targeting laser turret, slowing tar, sabotage points or bot personalities.
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
| **Charging laser turret** | Automatically cycles through two charging ticks and one firing tick. On its firing tick, it targets the nearest eligible robot within two tiles along the cardinal directions, fires once for 1 damage, then restarts charging. Starts neutral; players can capture ownership by entering its associated control tile. |
| **Continuous directional laser** | Existing `L>`, `L<`, `L^`, `Lv` emitters remain unchanged. They fire along a fixed ray every tick and cause 1 damage. They remain neutral environmental hazards and do not award sabotage points. |
| **Pit** | Permanent open hole. Entering immediately disables a robot. Carried cargo drops on the last safe tile if possible. Existing behaviour remains unchanged. |
| **Tar generator** | Affects the eight neighbouring tiles around the generator. Robots within its area have reduced voluntary movement: `F2` advances at most one tile, `F` remains one tile, and `B` does not translate. Turning, dropping, toggling and waiting remain unaffected. Tar causes no damage. |

### G2-A — Proximity trap rules

**Activation and timing**

1. At the start of a match, map-defined proximity traps are armed but have no player owner.
2. A player adjacent to the trap's associated control console may execute `T` to arm or rearm it, gaining ownership. Rearming an already-armed trap transfers ownership without triggering it.
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

1. Tick 1: charge.
2. Tick 2: charge.
3. Tick 3: fire one shot, then restart the cycle.

On the firing tick:

- Select the nearest eligible robot at distance 1 or 2 in a cardinal direction.
- Walls and other blocked terrain obstruct line of sight.
- If multiple targets are equally near, resolve ties using a fixed, documented order (proposed: N, E, S, W, then player ID).
- Fire at most once per cycle, dealing 1 damage.
- If no target is available, fire nothing and restart charging.
- The turret's owner receives any applicable sabotage points when another player is damaged.

The turret's charging state, ownership and firing action must be visible in the SDL client.

The charging turret is **not** a replacement for existing continuous directional lasers. Its map token and control tile must be distinct from the existing `L>` syntax.

### G2-C — Tar rules

Tar is a persistent movement hazard, not a damage source.

When a robot begins a voluntary movement instruction inside the eight-tile area surrounding a tar generator:

- `FORWARD_2` becomes a one-tile forward attempt.
- `FORWARD_1` remains unchanged.
- `BACK_1` does not move.
- Rotations, `DROP`, `TOGGLE` and `WAIT` behave normally.
- Pushing remains possible through an allowed one-tile movement attempt.
- Conveyor-induced movement and being pushed by another robot are not slowed.

No fractional speed, alternate-tick counters or real-time timing mechanics are required.

Tar awards no points. It is intended to make movement planning less predictable and create opportunities for traps and interception.

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

- Retain `training-01`, `crossroads-01` and `factory-01` as reference/playtest resources. Don't silently replace them.
- Design **one** new external `.map` around contested pickups, short routes, meaningful conveyor shortcuts, trap-controlled pinch points / “murder passages,” and a safe but less efficient alternative.
- Experiment with **N−1 dispensers** (three for four players), but do not require it universally.
- Aim for an uncontested delivery cycle of around **2–3 rounds** so journeys don't consume half a match; favour choices and encounters over raw distance.
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

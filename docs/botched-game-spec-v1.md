# BOTCHED! — Multiplayer Factory Frenzy

**Game design and implementation specification — v1.0 (20 November 2026)**  
**Status:** Proposed rules, ready for implementation. This is a new design inspired by programmable-robot board games, not a specification of an existing game.  
**Targets:** FujiNet-NIO-connected Amiga, Atari 8-bit, BBC Micro, and other constrained clients.  
**Players:** 2–4 humans (unoccupied slots disabled; optional bots are future work).  
**Architecture:** Server-authoritative deterministic, asynchronous turn submission; no real-time inputs required.

## 1. Elevator pitch and design goals

Each player controls a hapless warehouse robot. Robots simultaneously programme three instructions, then a central server executes each instruction slot in order. Conveyor belts, doors, pushable robots, lasers and other players' programming interfere with every plan. Collect **neutral crates** from dispensers and deliver them to **your own loading dock** to score. The player with the highest score after 12 rounds wins.

The game must support very different client capabilities: ASCII/character-grid clients are first-class, smooth animation is optional, and authoritative calculations happen only on the server. The protocol carries **semantic state and events**, not graphics or timed keypresses.

### Normative vocabulary

**MUST** denotes a required gameplay or protocol rule; **SHOULD** is recommended; **MAY** is optional. Coordinates are zero-based; `x` increases to the right, `y` increases downward. Directions are `N`, `E`, `S`, `W` with clockwise order. All identifiers are ASCII. A *tick* is one programmed instruction slot plus subsequent machinery resolution. A *round* contains exactly three ticks.

## 2. Match setup and victory

| Parameter | Rule |
|---|---|
| Players | 2–4, assigned player IDs 1–4 |
| Board | 12 columns × 10 rows in the supplied example; protocol allows 8–20 columns and 8–16 rows |
| Round count | 12 |
| Registers | Each player submits exactly 3 orders each round |
| Timer | Configurable; suggested 60 seconds per planning phase; server applies `WAIT,WAIT,WAIT` to missing submissions |
| Score | +5 per crate delivered to your own dock; +1 for each completed delivery in a round during which you have zero damage (optional rule **OFF** in v1) |
| Win | Highest score after tick 36 wins; tie-break by number of deliveries, then least damage taken, then shared victory |
| Elimination | None; damaged robots respawn and remain in the game |
| Visibility | Entire board, crates, robot positions and scores public; each player's unexecuted programme secret until resolution |

**v1 scoring is delivery-only** (+5); any optional bonus is excluded from the MVP and must not be implemented without a rules version change.

## 3. Grid, coordinates, and occupancy

The board consists of a **terrain layer** and an **entity layer**. Terrain is one immutable or configurable tile per coordinate. Entities are robots and loose crates. The server tracks occupancy; a robot and a loose crate may share a tile only transiently while the crate is automatically collected, otherwise a loose crate is considered an item on the floor, not an obstacle. At most one robot occupies a tile. At most one loose crate occupies a tile. A robot may carry at most one crate.

**Walkable terrain:** `FLOOR`, `DOCK`, `SPAWN`, `DISPENSER`, `CONVEYOR`, `CONSOLE`, `OPEN_DOOR`.  
**Blocked terrain:** `WALL`, `CLOSED_DOOR`, `LASER_EMITTER`.  
**Hazard terrain:** `PIT` is enterable but causes a robot to be disabled and respawned; loose crates entering pits are destroyed.

Moving or being pushed off the map is blocked as though by a wall. Robots do not move diagonally. Loose crates do not block normal movement. Each robot has position, heading, damage, carried crate (or none), deliveries, and respawn flag.

### 3.1 Terrain and objects

| ID | Purpose and capabilities | Interaction |
|---|---|---|
| `FLOOR` | Empty traversable tile | None |
| `WALL` | Immovable obstruction | Blocks robots, pushes and laser rays |
| `SPAWN(owner)` | Initial and respawn location for owner | Walkable to all; cannot be a crate dispenser |
| `DOCK(owner)` | Delivery destination for owner | Robot carrying any neutral crate delivers automatically when **ending a movement substep** on its own dock; +5, crate removed |
| `DISPENSER(id)` | Fixed source of neutral crates | At **start of each round**, spawn one crate if no loose crate and no robot is on dispenser; otherwise skip. Each dispenser has a stable ID |
| `CONVEYOR(direction, group)` | Pushes robots and loose crates one cell in indicated direction each tick | Activates in machinery phase; direction may be reversed by its group's console |
| `CONSOLE(group)` | Controls conveyor group | `TOGGLE` by an adjacent robot toggles group `normal/reversed`; effect begins in the **same tick's machinery phase** |
| `DOOR(group, initial_open)` | Gate under console control, optional map feature | `TOGGLE` affects group doors as well as conveyors; attempted closing while occupied stays open until next tick where vacant |
| `LASER_EMITTER(direction)` | Shoots a straight line until a wall, closed door or map edge | In laser phase, each robot intersected takes 1 damage; rays do not stop on robots or crates |
| `PIT` | Hazard | Entering robot is disabled; loose crate destroyed |

For v1 **required terrain**: floor, wall, spawn, dock, dispenser, conveyor, console, laser emitter, pit. Doors are **v1.1 optional** and must not appear on v1 maps. Teleporters, rotators, switches, shields, weapons, batteries and inventory upgrades are out of scope.

### 3.2 Entity objects

- **Robot:** `id`, `x`, `y`, `heading`, `damage` (0–2), `carrying` (crate ID or null), `deliveries`, `respawn_pending`. Robots block one another but may push chains under the rules below.
- **Crate:** `id`, `x`, `y` when loose, or `carrier_player_id` when held. All crates are neutral; no colours or types in v1. Crates are collected automatically when a robot enters their tile while empty-handed. A robot carrying a crate cannot collect another.
- **Conveyor group:** `id`, `reversed` boolean. Consoles with the same ID operate that group. A toggle flips the boolean exactly once for each successful `TOGGLE` order, in resolved order.

## 4. Robot programming and legal orders

Each round every active player submits an ordered array of **exactly three** orders. The client MAY offer shortcuts or preview pathing but MUST submit canonical order tokens. Each order executes once in its corresponding tick; there is no conditional programming in v1.

| Order token | Parameters | Semantics |
|---|---|---|
| `FORWARD_1` | None | Attempt one cell forward |
| `FORWARD_2` | None | Attempt up to two consecutive forward substeps; stop if a substep fails or robot is disabled |
| `BACK_1` | None | Attempt one cell backwards without changing heading |
| `TURN_LEFT` | None | Rotate heading 90° counter-clockwise; no translation |
| `TURN_RIGHT` | None | Rotate heading 90° clockwise; no translation |
| `TURN_AROUND` | None | Rotate heading 180°; no translation |
| `DROP` | None | If carrying, place crate on robot's current tile **if there is no loose crate already there**; otherwise do nothing. Does not score |
| `TOGGLE` | None | If currently orthogonally adjacent to one or more consoles, toggle the group of the console with the lexicographically smallest console ID; otherwise do nothing |
| `WAIT` | None | No voluntary movement or action; machinery still affects robot |

Robots do **not** need a separate pick-up command. Collection and delivery are automatic. No energy economy, attack command, movement cards, upgrades, or randomness in player orders in v1. Invalid order tokens cause submission rejection; a syntactically valid order that cannot act becomes a logged no-op.

## 5. Exact resolution algorithm

Everything below is **normative**. The server MUST not resolve movement based on arrival order of network packets. For each tick, determine priority by **rotating player ID**:

`priority_start = ((absolute_tick_index - 1) mod player_count) + 1`, where tick index begins at 1. Resolve players cyclically from that ID. Example with four players: tick 1 `1,2,3,4`; tick 2 `2,3,4,1`; tick 3 `3,4,1,2`; tick 4 `4,1,2,3`.

All orders are committed before tick 1. Different players' orders are executed sequentially within the same tick by priority, **not physically simultaneous**. This deliberate deterministic arbitration is the fairness rule; narration may call it a simultaneous *programming* game.

### 5.1 Tick phases (strict order)

1. **Robot orders:** Resolve the current register's order for each robot in priority order. Perform auto-pickup/delivery and pit checks after each movement substep, including pushed robots. A disabled robot cannot execute its remaining registers that round.
2. **Conveyor machinery:** Snapshot all robots and loose crates currently on active conveyors; each can be proposed to move at most one cell in this phase. Process robot proposals first in the same rotating priority order, and loose-crate proposals afterwards in ascending crate ID order. Failed moves do not retry. A robot pushed onto another conveyor by an earlier proposal does not gain an additional move this phase.
3. **Laser machinery:** For every laser emitter in ascending `(y,x)` order, cast a ray; each robot on any ray is hit at most **once per tick** even if beams overlap. A hit gives +1 damage.
4. **Disable and respawn marking:** Robots reaching damage 3 become disabled; any carried crate is dropped on their last safe tile if that tile has no loose crate, otherwise destroyed. The robot is removed from the board and marked `respawn_pending`. A robot entering a pit is disabled immediately during its movement substep, without taking additional laser damage.
5. **Tick end:** Record state and event log. At end of tick 3 of a round, process respawns and start next planning phase (or finish game).

### 5.2 Movement, pushing and collision

To move robot A into target cell T:

1. If T is outside board or blocked terrain, movement fails (`MOVE_BLOCKED`).
2. If T contains robot B, recursively attempt to push B **one cell in A's movement direction**. If a chain ends in an empty walkable cell, all pushed robots move one cell and then A moves into T. If any push step is blocked, entire attempted movement fails; no partial pushes. A chain cannot cycle because direction is fixed and straight.
3. A robot is pushed without changing heading. Pushed robots may auto-collect or deliver crates and may fall in pits.
4. Once movement succeeds, run pickup/delivery/pit resolution for each affected robot, **from farthest pushed robot back to active robot**.
5. `FORWARD_2` is two separate attempts; a successful first substep is retained if the second fails.

**Important:** Pushing a robot into a pit is legal: the robot is disabled immediately, vacating the pit cell, and the remaining chain movement completes. For implementation, determine the whole chain and legality first, then atomically apply new positions and resolve tile effects. A robot currently disabled has no tile occupancy.

### 5.3 Crate pickup, delivery and destruction

- At the end of any robot movement substep (including conveyor-induced moves and being pushed), if robot is empty-handed and standing on a loose crate, pick it up. Emit `CRATE_PICKED_UP`.
- If it then stands on its own dock holding a crate, deliver it immediately: remove crate from game, add +5 score, increment deliveries, emit `CRATE_DELIVERED`.
- When a robot starts a tick **already on its own dock holding a crate**, it delivers at start of that robot's order. This covers `DROP`/spawn anomalies and state restoration.
- `DROP` puts a loose crate on the current traversable tile; if the tile is a pit, crate is destroyed instead. A crate dropped on a conveyor is eligible for conveyor motion later that tick.
- A loose crate pushed by a conveyor onto a robot's tile is auto-collected if the robot is empty-handed, and delivered immediately if on that robot's dock.
- A loose crate pushed into a wall, closed door, out of bounds or another loose crate remains where it was. A loose crate pushed into a pit is destroyed. Loose crates never push robots.

### 5.4 Conveyor ordering and ambiguity avoidance

Each conveyor proposal uses the conveyor's direction (reversed if group state is true) **at phase start**. Process the original occupant list once, never newly arrived occupants. Robot conveyor moves use the same robot push-chain routine as voluntary movement. Crate conveyor moves check terrain and destination crate occupancy as they execute. If two crates target the same tile, ascending crate ID wins; the other stays. A conveyor has no effect on a robot **standing on a crate** other than ordinary robot motion. An occupied dispenser does not spawn a crate that round.

### 5.5 Damage, disables and respawns

- Robots have 3 durability (damage counter 0, 1, 2; disable at 3).
- Laser hits cause +1 damage. Pits cause immediate disable. Wall collisions and failed pushes do not deal damage.
- Disabled robots' remaining orders for the current round become `WAIT` with `ORDER_SKIPPED_DISABLED` events.
- At **end of round**, respawn pending players at their designated spawn tiles, with damage reset to 0 and heading reset to the spawn's defined heading; their personal score and delivery count persist.
- If spawn occupied, find first walkable, robot-free tile using increasing Manhattan distance from spawn, then increasing `y`, then `x`; never use pits, emit `RESPAWN_REDIRECTED`. If no legal tile exists, leave robot pending and retry next round.
- When disabling a robot on a pit, treat the last safe cell it occupied before entering the pit as its crate-drop candidate. Otherwise use its current tile. Crates not droppable are destroyed.

## 6. Round lifecycle

1. **Start round:** Replenish one neutral crate at each unobstructed dispenser. Respawn any pending robots that were not successfully placed in previous rounds. Publish `ROUND_STARTED` and a snapshot.
2. **Planning:** Accept at most one current programme per player; allow replacements until locked or deadline. Programmes have a `round` and monotonically increasing `revision`. `ready=true` locks that player's latest programme. Clients may not view opponents' programmes before resolution.
3. **Close planning:** When all players are ready, or deadline expires, replace missing programmes with three `WAIT` orders; retain submitted but unready latest programmes. Lock all programmes and publish `PLANNING_CLOSED`.
4. **Execute:** Run exactly three ticks with the algorithm above; publish state versions and ordered events after each tick. Clients may animate at their own speed and must never block the server.
5. **Round end:** Process respawns; publish scores and new snapshot. Advance round or calculate winner after round 12.

A disconnected client remains a participant; its robot uses `WAIT` when it fails to submit. Reconnection uses session authorization, not a new player slot. Spectators may see current resolved state but not pending programmes.

## 7. Example playable 12×10 map

Tokenized row format (one token per coordinate; spaces are separators). This **illustrative layout is not certified balanced**. A production implementation MUST validate it using the checks in section 8 and adjust strategic balance in playtests.

Legend: `#` wall; `.` floor; `S1`–`S4` owner spawn; `D1`–`D4` owner dock; `C>`/`C<` conveyors (group `A`); `T` console (group `A`); `G1`–`G4` dispensers; `L>` laser emitter (ray East); `O` pit.

```text
#  #  #  #  #  #  #  #  #  #  #  #
# S1 D1  .  . G1  .  .  . D2 S2  #
#  .  .  #  .  .  .  #  .  .  .  #
#  .  .  . C> C> C>  .  . G2  .  #
#  .  #  .  .  O  .  .  #  .  .  #
#  .  . G3  .  T  .  .  .  .  .  #
#  .  #  . C< C< C<  .  #  .  .  #
#  .  .  .  .  .  .  .  . G4  .  #
# S3 D3  .  .  .  .  .  . D4 S4  #
#  #  #  #  #  #  #  #  #  #  #  #
```

The MVP example intentionally does **not** include a laser emitter: `LASER_EMITTER` is defined above and can be used in alternate maps, but this learning map has no forced laser damage. The map features one `A` group whose three east and three west conveyors reverse upon `TOGGLE`.

Initial robot headings: P1 `E`, P2 `W`, P3 `E`, P4 `W`. For 2–3 player matches, remove unused robots and keep all four docks/dispenser positions; empty docks are harmless. A real map may add lasers after initial tests.

## 8. Map validation requirements

A map validator MUST reject maps unless all of the following hold:

- Dimensions are within bounds; exactly one terrain tile exists at each coordinate.
- Exactly one spawn and one matching dock exist for **each available player slot**. Spawns and docks must be walkable, distinct, and not overlapping.
- At least two dispensers exist; dispenser IDs unique; initial crate IDs unique.
- Every console group references at least one conveyor; every conveyor group has at least one console. Each entity's start coordinate is legal.
- All four configured spawn-to-own-dock pairs are connected through walkable cells ignoring robots and doors. Each dock is reachable from at least one dispenser by walkable cells ignoring robots and doors.
- There are no blocked-terrain objects or conveyors facing an out-of-bounds cell in the supplied first-party maps (general maps MAY permit conveyors aimed at walls, provided movement fails normally).
- All tokens, group IDs and owner IDs parse successfully; no duplicate robot or crate IDs.

Map balance (spawn symmetry, dispenser proximity, access to console) is a **playtest criterion**, not a requirement of graph validation.

## 9. Server state and wire contract

The exact serialization may be compact binary or JSON, but semantic types and fields are normative. For an initial POSIX reference implementation use JSON; constrained clients may use a binary adapter. Transport framing, authentication and retry policy belong to NIO infrastructure, not the game engine.

### 9.1 Authoritative state

```json
{
  "protocol": "botched/1",
  "match_id": "m123",
  "state_version": 27,
  "phase": "PLANNING",
  "round": 4,
  "tick": 0,
  "board": { "width": 12, "height": 10, "map_id": "training-01" },
  "players": [
    { "id": 1, "x": 2, "y": 4, "heading": "N", "damage": 1,
      "carrying": null, "score": 10, "deliveries": 2, "respawn_pending": false }
  ],
  "crates": [{ "id": 19, "x": 7, "y": 4, "carrier_player_id": null }],
  "groups": [{ "id": "A", "reversed": false }],
  "submitted_players": [1, 3],
  "ready_players": [1],
  "planning_deadline_utc": "2026-11-20T20:40:00Z"
}
```

A full snapshot MUST include immutable map terrain or a stable map ID plus a separately retrievable map definition. It MUST NOT include other players' pending orders during planning. `state_version` increases monotonically on every published authoritative state transition.

### 9.2 Client commands

```json
{ "type": "SUBMIT_PROGRAM", "match_id": "m123", "player_id": 1,
  "round": 4, "revision": 2,
  "orders": ["FORWARD_2", "TURN_RIGHT", "TOGGLE"], "ready": true,
  "request_id": "client-unique-104" }
```

`SUBMIT_PROGRAM` is accepted only for the currently connected, authenticated player's slot, while planning that round, unless `ready` is already true. Repeated identical `request_id` MUST return the original response idempotently. Revisions lower than an accepted revision MUST be rejected; equal revision with different contents MUST be rejected. Commands received after planning closes MUST be rejected without changing results. Requests to fetch `GET_SNAPSHOT`, `GET_EVENTS(since_version)`, `GET_MAP` and `GET_MATCH_STATUS` MUST be supported; these are logical API operations, not necessarily HTTP endpoints.

### 9.3 Events

Each event MUST include `match_id`, `state_version`, `round`, `tick`, `sequence`, `type`, and deterministic parameters. Types include:

`ROUND_STARTED`, `PLANNING_CLOSED`, `ORDER_EXECUTED`, `ORDER_NO_OP`, `ORDER_SKIPPED_DISABLED`, `ROBOT_MOVED`, `ROBOT_PUSHED`, `MOVE_BLOCKED`, `ROBOT_ROTATED`, `GROUP_TOGGLED`, `CRATE_SPAWNED`, `CRATE_PICKED_UP`, `CRATE_DROPPED`, `CRATE_MOVED`, `CRATE_DESTROYED`, `CRATE_DELIVERED`, `LASER_HIT`, `ROBOT_DISABLED`, `ROBOT_RESPAWNED`, `RESPAWN_REDIRECTED`, `ROUND_ENDED`, `MATCH_ENDED`.

Event sequence is monotonic **within one tick**, starting at 1. Events must describe accepted effects, not proposed or rolled-back changes. A polling client may skip events and request a full snapshot instead. If event retention has expired, return `SNAPSHOT_REQUIRED` rather than silently providing an incomplete sequence.

## 10. Client minimum requirements

- Render board coordinates and terrain symbols, every robot and loose crate, own heading, damage, held-crate state, score, round, and readiness status.
- Allow selection of the nine order tokens for three slots, editing up to ready/timeout, and submit/retry safely.
- Handle a full snapshot at any time, including after reconnect, without replaying all earlier events.
- Treat events as animations/UI hints; never infer game authority from client physics.
- Support character-grid displays and no animation. Sound, sprites, smooth motion and high resolution are optional.
- Do not require a frame rate, a persistent connection, or synchronized clocks.

## 11. Determinism and acceptance tests

A headless simulation library MUST accept `(map, initial_state, submitted_programmes, rules_version)` and produce byte-for-byte stable canonical state plus a stable event stream. The implementation MUST include tests for at least:

1. Heading rotation, forward/back movement, wall and border blocking.
2. `FORWARD_2` first step succeeding and second failing.
3. Two- and three-robot push chains, blocked chains, pushes into pit.
4. Rotating resolution priority for all four players over four ticks.
5. Pickup from dispenser, delivery on own dock, no delivery on rival dock, carrying capacity.
6. Dropping onto empty vs crate-occupied tile; conveyor crate movement and contention.
7. Conveyor snapshot: newly arrived entity does not move twice.
8. Console toggled before conveyor phase; two toggles of same group cancel.
9. Lasers blocked by wall and hitting several robots; overlapping beams never double-hit in tick.
10. Disable at third damage, pit disable, crate-drop/destroy, skip remaining orders, respawn tie-break.
11. Missing programme → three `WAIT` orders; duplicate request idempotency; stale revisions rejection.
12. Identical engine results for same inputs independent of network arrival order and client rendering speed.
13. Round-12 end conditions, equal scores, delivery and damage tie-breaks.
14. Reconnect snapshot accurately restores carried crates, group states and pending phase.

## 12. Implementation milestones

**M0 — Pure rules engine:** map parser/validator, robots, orders, pushing, pickup/delivery, turn priority, snapshot and deterministic replay. Unit tests; no network or rendering.

**M1 — Networked MVP:** server rooms, programme submission and timeout, event stream/polling, reconnect, ASCII reference client. Use `training-01`, no lasers if preferred for first end-to-end match. All v1 core rules still implemented.

**M2 — Retro ports:** compact transport binding, platform-specific render/input, four-platform interoperability tests, lag/reconnect resilience.

**M3 — Balance and polish:** alternative maps, configurable turn timer, animation, sounds and optional AI bots. Any added mechanics require `botched/2` or explicit feature negotiation.

## 13. Non-goals and intellectual-property direction

Do not reproduce Robo Rally's proprietary board layouts, artwork or verbatim rule text. Botched! uses its own scoring, object selection, factory rules, naming and presentation. No card deck, card drafting, dexterity checks, real-time steering, microtransactions or persistent player progression.

## 14. Open implementation decisions deliberately left configurable

These settings affect presentation or deployment, **not** core simulation: planning duration (default 60 s), animation pacing, optional private matches and lobby access controls, which supported client protocol encoding is used, and maximum number of simultaneously hosted matches.

**Rules freeze:** Any change to movement, push order, tile activation sequence, scoring or win conditions MUST bump the game rules version and require new deterministic fixtures.

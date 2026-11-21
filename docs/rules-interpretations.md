# Rules interpretations (botched/1)

The specification is authoritative. This file lists every place where the text was
silent or ambiguous and an implementation choice had to be made. None of them change a
documented rule; each is the narrowest reading consistent with the text. Changing any
of them is a rules change (spec section 14: bump the rules version, new fixtures).

Each row names the test that pins the behaviour.

| # | Topic | Spec says | Implemented as | Pinned by |
|---|-------|-----------|----------------|-----------|
| 1 | Player ids | "assigned player IDs 1-4"; priority uses `mod player_count` | A match with N players uses ids 1..N, contiguous. Unoccupied slots do not exist (not "disabled robots"). | server `START_MATCH` test |
| 2 | Console id order | "lexicographically smallest console ID" | Consoles get ids `T001`, `T002`... in `(y,x)` scan order. Lexicographic on zero-padded ids equals scan order. | `8. ... smallest console id` |
| 3 | Map tokens beyond section 7 | Only `C>`/`C<`, `T`, `L>` shown | `C<d>[group]`, `T[group]`, `L<d>`, `S<n>[dir]`; arrows `> < ^ v`; group defaults to `A`; spawn heading defaults E for odd owners, W for even (the section 7 headings). | map tests |
| 4 | Laser ray termination | "until a wall, closed door or map edge" | A ray stops at any *blocked* terrain: wall, **another laser emitter**, or the edge. Robots and crates never stop it. | `9. a ray stops at another emitter` |
| 5 | Conveyor: robot displaced earlier in the phase | "never newly arrived occupants"; "at most one cell in this phase" | A snapshotted robot that was already moved (pushed) earlier in the conveyor phase has used its one move and is skipped. | `7. a robot pushed onto a second belt...` |
| 6 | Conveyor snapshot time | "Snapshot ... currently on active conveyors" | Taken when the conveyor phase starts, i.e. after robot orders. A robot that stepped onto a belt during orders rides once. | `7. conveyor snapshot` |
| 7 | Crate under a robot on a belt | "no effect on a robot standing on a crate other than ordinary robot motion" | The robot moves as a robot; the crate is processed independently in the crate pass (ascending id). | `6. robots move before crates` |
| 8 | Failed crate conveyor moves | "remains where it was" | No event is emitted (events describe accepted effects only). | - |
| 9 | Pickup is movement-only | "at the end of any robot movement substep" | Standing still, `DROP`, turning, respawning or a crate dropped under a robot never picks up. Start-of-order delivery (5.3) only delivers; it never picks up. | `5. standing still on a loose crate...` |
| 10 | Disable and damage value | damage "0-2"; "disable at 3" | A laser-disabled robot keeps `damage = 3` until it respawns (reset to 0 there). Pit disables leave damage unchanged. | respawn test |
| 11 | "Least damage taken" tie-break | no definition | Cumulative count of laser hits over the whole match (`damage_taken`, serialised). Pit disables count 0. | `13. tie-breaks` |
| 12 | Disable while carrying (laser) | "last safe tile ... Otherwise use its current tile" | Laser disable drops on the current tile; pit disable drops on the cell it occupied before the movement substep (for pushed robots: before the push). Occupied by a loose crate -> destroyed. | `10. ...` tests |
| 13 | Event order at round start | 6.1 lists crates, respawns, then "publish ROUND_STARTED" | `CRATE_SPAWNED`*, `RESPAWN_*`/`ROBOT_RESPAWNED`*, then `ROUND_STARTED`. | `server: GET_EVENTS` |
| 14 | Event order at round end | 5.1(5) + 6.5 | `ROBOT_RESPAWNED`*, `ROUND_ENDED`, and `MATCH_ENDED` after the last round. | `13. the match ends` |
| 15 | Sequence numbers | "monotonic within one tick, starting at 1" | Sequence restarts at 1 whenever `(round, tick)` changes. Planning-phase events (`ROUND_STARTED`, `PLANNING_CLOSED`) are tick 0. `(round, tick, sequence)` is unique; `state_version` is non-decreasing. | `12. event stream is ordered` |
| 16 | `state_version` | "increases on every published authoritative state transition" | +1 per accepted submission that changes state, per `PLANNING_CLOSED`, per tick, and per round start. Tick 3 also carries `ROUND_ENDED`/respawns. Identical resubmissions do not bump it. | `11. duplicate request ids` |
| 17 | Request-id idempotency | "Repeated identical `request_id` MUST return the original response" | Approved contract (`docs/request-id-contract.md`): keyed by `(player, request_id)`; every outcome, accepted or rejected, is recorded exactly (id verbatim, payload exact) for the match lifetime and replayed byte for byte, also after planning closes or the round advances. A reused id with different contents -> `REQUEST_ID_REUSED`. New ids are rationed to 32 per player per round (`REQUEST_LIMIT`, configurable 8-128); nothing is ever evicted. Not persisted by `save()` (M1-02). | `tests/test_idempotency.cpp` |
| 18 | Revision rules (decided by the rules, after the idempotency check) | lower rejected; equal + different contents rejected | Equal revision with *identical* contents (including `ready`) is accepted as a no-op. `revision >= 1`. A programme already `ready` is locked (`PROGRAM_LOCKED`). | `11. revisions` |
| 19 | Respawn onto a crate | not stated | No pickup (respawn is not a movement substep). | - |
| 20 | Respawn redirect "walkable" | "walkable, robot-free ... never pits" | Any non-blocked, non-pit tile without a robot (loose crates are fine). Search order: Manhattan distance, then `y`, then `x`. | `10. respawn` |
| 21 | Map validation | section 8 | "Blocked-terrain objects/conveyors facing out of bounds" is implemented as *conveyors facing out of bounds* (strict mode, default on). Spawn/dock/path checks apply to slots 1..N only; extra docks and spawns are allowed. | validator tests |
| 22 | Doors | v1.1 optional | Not implemented; no door token exists, so v1 maps cannot contain them. | - |
| 23 | Optional +1 bonus | OFF in v1 | Not implemented. | - |
| 24 | Planning timer | 60 s suggested, configurable | A server concern (`planning_seconds`, 0 = none). The rules library has no clock. | `server: the planning deadline applies WAIT...` |
| 25 | G2-A proximity traps: tokens | G2-A in `docs/botched-gameplay-milestones-g1-g4.md` | `X<n>` (1-9) is the trap device, `K<n>` its control console; each `X<n>` needs at least one `K<n>` and vice versa. The device is **blocked terrain** (like a wall or emitter: robots, crates and laser rays stop at it); the console is walkable like a belt console. Additive: maps without these tokens behave exactly as before and their snapshots, events and hashes are unchanged. | `tests/test_traps.cpp` |
| 26 | G2-A tick order | "after all player instructions and conveyor movement"; ordering vs lasers to be settled | Phases per tick: 1 orders, 2 conveyors, 3 continuous lasers, **3b proximity traps**, 4 disables. Traps run after lasers so all positions are final, and before the disable check so one tick's damage from every source is judged once. | `laser felled` test |
| 27 | Arming | "T ... arms or rearms it, gaining ownership" | `TOGGLE` next to a belt console still toggles that belt group (a belt console wins if both are adjacent). Otherwise next to a `K` console (lowest trap id) it arms the trap: `armed = true`, `owner = the robot's player`, event `TRAP_ARMED` (with `was_armed`, `previous_owner`). Arming never fires the trap by itself; but occupancy is judged in the same tick's machinery phase, so arming a trap whose area is already occupied fires it immediately. | arming tests |
| 28 | Activation | rules 3-6 | Armed and at least one active robot on the eight surrounding tiles (Chebyshev distance 1; walls do not shield): `TRAP_TRIGGERED` (all robots in range listed), the trap disarms, then each robot in range (ascending id) takes 1 damage (`TRAP_HIT`), the owner included. A robot already at 3 damage this tick (felled by a laser first) takes none and its disable stays a laser disable. Traps fire in ascending id order and a robot in two areas is hit twice. Respawn-pending robots are not on the board. | trap tests |
| 29 | Sabotage scoring | table in the G2 section | Awarded for damage actually dealt: owner != victim and owner != neutral. `+1` per damaged opponent (`SCORE_AWARDED`, reason `TRAP_DAMAGE`); `+3` once more when that hit takes the victim to 3 damage and the robot is disabled (`TRAP_DISABLE`, the `ROBOT_DISABLED` event carries `cause: TRAP`, `trap`, `by`). Self-damage and neutral damage score 0. `score = 5 x deliveries + sabotage` (new per-player `sabotage`, serialised only on maps with traps); sabotage counts for score targets and tie-breaks. | trap tests |
| 30 | Trap state | not specified | `traps: [{id, armed, owner}]` in snapshots and checkpoints on maps with traps (`owner` null = neutral). Maps start armed and neutral. Restore validates ids against the map, owners against the player count and the score equation. | restore tests |

## Open question for the rules owner

Items 5, 7, 11 and 12 have the highest chance of surprising a player. If a different
reading is intended, say so before M1 clients are written against `botched/1`.

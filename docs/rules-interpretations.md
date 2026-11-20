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
| 17 | Request-id cache | "Repeated identical `request_id` MUST return the original response" | Keyed by `(player, request_id)`, kept for the match lifetime, includes rejections. Not persisted by `save()`. | `server: retried submissions` |
| 18 | Revision rules | lower rejected; equal + different contents rejected | Equal revision with *identical* contents (including `ready`) is accepted as a no-op. `revision >= 1`. A programme already `ready` is locked (`PROGRAM_LOCKED`). | `11. revisions` |
| 19 | Respawn onto a crate | not stated | No pickup (respawn is not a movement substep). | - |
| 20 | Respawn redirect "walkable" | "walkable, robot-free ... never pits" | Any non-blocked, non-pit tile without a robot (loose crates are fine). Search order: Manhattan distance, then `y`, then `x`. | `10. respawn` |
| 21 | Map validation | section 8 | "Blocked-terrain objects/conveyors facing out of bounds" is implemented as *conveyors facing out of bounds* (strict mode, default on). Spawn/dock/path checks apply to slots 1..N only; extra docks and spawns are allowed. | validator tests |
| 22 | Doors | v1.1 optional | Not implemented; no door token exists, so v1 maps cannot contain them. | - |
| 23 | Optional +1 bonus | OFF in v1 | Not implemented. | - |
| 24 | Planning timer | 60 s suggested, configurable | A server concern (`planning_seconds`, 0 = none). The rules library has no clock. | `server: deadline` |

## Open question for the rules owner

Items 5, 7, 11 and 12 have the highest chance of surprising a player. If a different
reading is intended, say so before M1 clients are written against `botched/1`.

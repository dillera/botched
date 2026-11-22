# B1 annotated byte examples

These are literal review fixtures, not outputs of an implemented codec. Machine-readable copies are in
[fixtures.json](fixtures.json). All numbers in byte strings are hexadecimal. Each request example uses
exchange 1 independently unless noted; do not concatenate them as a session. Record examples include
tag and body length. The field order and scalar widths in [wire.md](wire.md) annotate every byte.
Illustrative isolated records are not assertions that their combination forms a legal game checkpoint.
A codec test in B2 must supply the logical response context and assert decoded values, not merely lengths.

| Name | Kind / bytes | Decode | Hex |
|---|---|---|---|
| HELLO-new | payload / 30 | major 1..1, minor 0, P=240, K4, mandatory features, new session | `01 00 00 01 01 00 f0 00 02 3f 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |
| HELLO-resume | payload / 30 | Resume secret 16 bytes of 11; fixture-only secret | `01 00 00 01 01 00 f0 00 02 3f 00 00 00 01 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11` |
| HELLO_OK | payload / 48 | v1.0 P240 K4, epoch22*, secret11*, last=0, lease1800s | `81 00 00 01 00 f0 00 02 3f 00 00 00 22 22 22 22 22 22 22 22 22 22 22 22 22 22 22 22 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 00 00 08 07` |
| BIND-player | payload / 15 | role1; match m1; player1; credential abc (illustrative test credential) | `02 01 00 01 02 6d 31 01 00 00 00 03 61 62 63` |
| BIND-spectator-refused | payload / 12 | role2 reserved: expect UNSUPPORTED; no binding | `02 01 00 02 02 6d 31 00 00 00 00 00` |
| FETCH | payload / 11 | transfer1 offset16 (must have been a returned page boundary) | `03 01 00 01 00 00 00 10 00 00 00` |
| CANCEL | payload / 7 | transfer1 | `04 01 00 01 00 00 00` |
| CANCELLED | payload / 7 | transfer1 released | `83 01 00 01 00 00 00` |
| LIST_MAPS | payload / 3 | no fields | `10 01 00` |
| CREATE-defaults | payload / 14 | presence0, empty map and zero absent fixed slots | `11 01 00 00 00 00 00 00 00 00 00 00 00 00` |
| CREATE-all | payload / 24 | foundry-01, four players, three bots, cap200, target1000, planning86400 | `11 01 00 3f 0a 66 6f 75 6e 64 72 79 2d 30 31 04 03 c8 e8 03 80 51 01 00` |
| JOIN_MATCH | payload / 6 | match m1 | `12 01 00 02 6d 31` |
| START-bound | payload / 4 | bound match | `13 01 00 00` |
| START-explicit | payload / 7 | public scope m1, no seat authority added | `13 01 00 01 02 6d 31` |
| SUBMIT-numeric | payload / 21 | bound player, round3 revision1, F2/right/toggle, ready, numeric key7 -> b:7 | `14 01 00 00 03 00 00 00 01 00 00 00 01 04 07 01 01 07 00 00 00` |
| SUBMIT-unkeyed | payload / 17 | WAITx3, draft, no key | `14 01 00 00 03 00 00 00 01 00 00 00 08 08 08 00 00` |
| SUBMIT-ascii | payload / 21 | exact ASCII key c-1 | `14 01 00 00 03 00 00 00 01 00 00 00 08 08 08 00 02 03 63 2d 31` |
| SUBMIT-i32-boundaries | payload / 25 | explicit player -1, round INT32_MIN, revision INT32_MAX, defined invalid-order sentinel; numeric key UINT32_MAX; server UNKNOWN_PLAYER precedes order checking | `14 01 00 01 ff ff ff ff 00 00 00 80 ff ff ff 7f ff 08 08 01 01 ff ff ff ff` |
| GET_SNAPSHOT | payload / 4 | bound scope | `15 01 00 00` |
| GET_MAP | payload / 4 | bound scope | `17 01 00 00` |
| GET_MATCH_STATUS | payload / 4 | bound scope | `18 01 00 00` |
| GET_EVENTS | payload / 10 | since41, max16 versions | `16 01 00 00 29 00 00 00 10 00` |
| GET_EVENTS-negative | payload / 13 | explicit m1 since -1, one version; SNAPSHOT_REQUIRED | `16 01 00 01 02 6d 31 ff ff ff ff 01 00` |
| FAULT-UNSUPPORTED | payload / 4 | UNSUPPORTED | `ff 01 00 01` |
| FAULT-BAD_MESSAGE | payload / 4 | BAD_MESSAGE | `ff 01 00 02` |
| FAULT-BAD_BINDING | payload / 4 | BAD_BINDING | `ff 01 00 03` |
| FAULT-SESSION_EXPIRED | payload / 4 | SESSION_EXPIRED | `ff 01 00 04` |
| FAULT-EXCHANGE_REUSED | payload / 4 | EXCHANGE_REUSED | `ff 01 00 05` |
| FAULT-BAD_SEQUENCE | payload / 4 | BAD_SEQUENCE | `ff 01 00 06` |
| FAULT-BAD_CURSOR | payload / 4 | BAD_CURSOR | `ff 01 00 07` |
| FAULT-TRANSFER_EXPIRED | payload / 4 | TRANSFER_EXPIRED | `ff 01 00 08` |
| FAULT-LIMIT | payload / 4 | LIMIT | `ff 01 00 09` |
| FAULT-PROFILE_LIMIT | payload / 4 | PROFILE_LIMIT | `ff 01 00 0a` |
| CONTEXT | record / 5 | match_id=m1 | `01 03 02 6d 31` |
| CREATED | record / 7 | players4 bots3 rounds200 target1000 | `02 05 04 03 c8 e8 03` |
| JOINED | record / 8 | player1 started=true credential=abc | `03 06 01 01 03 61 62 63` |
| STARTED | record / 3 | four players | `04 01 04` |
| SUBMITTED-accepted | record / 26 | P1 round3 rev1 response version42, accepted, numeric b:7; append exact diagnostic TEXT | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 01 00 00 01 07 00 00 00` |
| SUBMITTED-unchanged | record / 26 | P1 round3 rev1 response version42, unchanged, numeric b:7; append exact diagnostic TEXT | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 01 00 01 01 07 00 00 00` |
| SUBMITTED-bad-count | record / 26 | P1 round3 rev1 response version42, bad-count, numeric b:7; append exact diagnostic TEXT | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 00 0e 02 01 07 00 00 00` |
| SUBMITTED-bad-token | record / 26 | P1 round3 rev1 response version42, bad-token, numeric b:7; append exact diagnostic TEXT | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 00 0e 03 01 07 00 00 00` |
| SUBMITTED-closed | record / 26 | P1 round3 rev1 response version42, closed, numeric b:7; append exact diagnostic TEXT | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 00 10 04 01 07 00 00 00` |
| ERROR-REQUEST_TOO_LARGE | record / 3 | REQUEST_TOO_LARGE; ERROR context, followed by ECHO and TEXT | `06 01 01` |
| ERROR-BAD_REQUEST | record / 3 | BAD_REQUEST; ERROR context, followed by ECHO and TEXT | `06 01 02` |
| ERROR-UNKNOWN_REQUEST | record / 3 | UNKNOWN_REQUEST; ERROR context, followed by ECHO and TEXT | `06 01 03` |
| ERROR-UNKNOWN_MATCH | record / 3 | UNKNOWN_MATCH; ERROR context, followed by ECHO and TEXT | `06 01 04` |
| ERROR-UNKNOWN_MAP | record / 3 | UNKNOWN_MAP; ERROR context, followed by ECHO and TEXT | `06 01 05` |
| ERROR-TOO_MANY_MATCHES | record / 3 | TOO_MANY_MATCHES; ERROR context, followed by ECHO and TEXT | `06 01 06` |
| ERROR-INVALID_MAP | record / 3 | INVALID_MAP; ERROR context, followed by ECHO and TEXT | `06 01 07` |
| ERROR-ALREADY_STARTED | record / 3 | ALREADY_STARTED; ERROR context, followed by ECHO and TEXT | `06 01 08` |
| ERROR-MATCH_FULL | record / 3 | MATCH_FULL; ERROR context, followed by ECHO and TEXT | `06 01 09` |
| ERROR-START_FAILED | record / 3 | START_FAILED; ERROR context, followed by ECHO and TEXT | `06 01 0a` |
| ERROR-NOT_STARTED | record / 3 | NOT_STARTED; ERROR context, followed by ECHO and TEXT | `06 01 0b` |
| ERROR-UNKNOWN_PLAYER | record / 3 | UNKNOWN_PLAYER; ERROR context, followed by ECHO and TEXT | `06 01 0c` |
| ERROR-UNAUTHORIZED | record / 3 | UNAUTHORIZED; ERROR context, followed by ECHO and TEXT | `06 01 0d` |
| ERROR-INVALID_ORDERS | record / 3 | INVALID_ORDERS; ERROR context, followed by ECHO and TEXT | `06 01 0e` |
| ERROR-SNAPSHOT_REQUIRED | record / 3 | SNAPSHOT_REQUIRED; ERROR context, followed by ECHO and TEXT | `06 01 0f` |
| ECHO-present | record / 22 | request_type SUBMIT_PROGRAM, request_id b:7 | `07 14 0e 53 55 42 4d 49 54 5f 50 52 4f 47 52 41 4d 01 07 00 00 00` |
| ECHO-absent | record / 4 | no echoed type/key | `07 02 00 00` |
| BOUND | record / 9 | role player, P1, next key8, not exhausted | `08 07 01 01 08 00 00 00 00` |
| BOUND-exhausted | record / 9 | numeric keys exhausted; next slot zero | `08 07 01 01 00 00 00 00 01` |
| SNAP_META-fixed | record / 33 | planning round3/12, next crate3, map1, two players/two crates/one group, P1 submitted, no deadline/target | `09 1f 00 03 00 0c 00 00 03 00 00 00 01 00 00 00 00 02 02 00 01 00 00 01 00 00 00 00 00 00 00 00 00` |
| SNAP_META-target-executing | record / 33 | executing tick2, cap200 target1000, next crate65537, four players, one trap/turret, deadline1700000000 | `09 1f 01 03 02 c8 e8 03 01 00 01 00 01 00 00 00 03 04 02 00 01 01 01 0f 0f 00 f1 53 65 00 00 00 00` |
| PLAYER-active | record / 29 | P1 at(2,3) east, damage0, no cargo/sabotage/push; omitted slots zero | `0a 1b 01 00 02 03 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |
| PLAYER-carry-sabotage-pushed | record / 29 | P2 carries crate65536, score13=delivery5+sabotage8, damage/taken2, pushed_by1; all optional values present | `0a 1b 02 0e 03 03 03 02 00 00 01 00 0d 00 00 00 01 00 00 00 02 00 00 00 08 00 00 00 01` |
| PLAYER-disabled | record / 29 | P1 pending respawn, null coordinates/carry, damage3 taken3 | `0a 1b 01 01 00 00 01 03 00 00 00 00 00 00 00 00 00 00 00 00 03 00 00 00 00 00 00 00 00` |
| CRATE-loose | record / 9 | crate65536 loose at2,3 | `0b 07 00 00 01 00 02 03 00` |
| CRATE-carried | record / 9 | crate65536 carried by P2; null coordinates | `0b 07 00 00 01 00 00 00 02` |
| GROUP | record / 4 | group Z reversed | `0c 02 19 01` |
| HAZARD-trap-neutral-armed | record / 6 | trap-neutral-armed | `0d 04 01 01 01 00` |
| HAZARD-trap-owned-disarmed | record / 6 | trap-owned-disarmed | `0d 04 01 01 00 02` |
| HAZARD-turret-neutral-ready | record / 6 | turret-neutral-ready | `0d 04 02 01 02 00` |
| HAZARD-turret-owned-empty | record / 6 | turret-owned-empty | `0d 04 02 01 00 02` |
| STATUS-lobby | record / 20 | lobby cap200 target1000 joined3 capacity4 bots2 | `0e 12 03 00 00 c8 e8 03 00 00 00 00 00 03 04 02 00 00 00 00` |
| STATUS-planning | record / 26 | P2 hauler P3 opportunist P4 saboteur, all submitted, bot seats ready | `0e 18 00 03 00 c8 e8 03 2a 00 00 00 04 00 00 03 0f 0e 00 03 02 00 03 01 04 02` |
| STATUS-finished-tie | record / 20 | finished round3, P1 and P2 winners | `0e 12 02 03 03 c8 e8 03 2a 00 00 00 02 00 00 00 03 03 03 00` |
| TEXT-name | record / 15 | field1, total3, offset0: 'Map' | `0f 0d 01 03 00 00 00 00 00 00 00 03 4d 61 70` |
| TEXT-description | record / 12 | field2, total0, offset0: '' | `0f 0a 02 00 00 00 00 00 00 00 00 00` |
| TEXT-message | record / 20 | field3, total8, offset0: 'accepted' | `0f 12 03 08 00 00 00 00 00 00 00 08 61 63 63 65 70 74 65 64` |
| TEXT-continuation | record / 68 | description total120, offset64, final56 bytes a; follows fragment0 of64 bytes | `0f 42 02 78 00 00 00 40 00 00 00 38 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61` |
| MAP_META | record / 24 | map1 training-01,12x10,120 tiles,19 attributes | `10 16 01 00 00 00 0b 74 72 61 69 6e 69 6e 67 2d 30 31 0c 0a 78 00 13 00` |
| TILES-all-kinds | record / 12 | first0,14 tiles: terrain codes0..13 low nibble first (illustrative segment) | `11 0a 00 00 0e 10 32 54 76 98 ba dc` |
| TILES-odd | record / 7 | three terrain codes0,1,2; final padding high nibble0 | `11 05 00 00 03 10 02` |
| ATTRIBUTE-spawn | record / 7 | tile terrain determines interpretation: S4W | `12 05 01 01 04 00 03` |
| ATTRIBUTE-dock | record / 7 | tile terrain determines interpretation: D4 | `12 05 02 01 04 00 00` |
| ATTRIBUTE-dispenser | record / 7 | tile terrain determines interpretation: G999 | `12 05 03 01 e7 03 00` |
| ATTRIBUTE-conveyor | record / 7 | tile terrain determines interpretation: CvZ | `12 05 04 01 19 00 02` |
| ATTRIBUTE-console | record / 7 | tile terrain determines interpretation: TZ | `12 05 05 01 19 00 00` |
| ATTRIBUTE-laser | record / 7 | tile terrain determines interpretation: L> | `12 05 06 01 00 00 01` |
| ATTRIBUTE-trap | record / 7 | tile terrain determines interpretation: X9 | `12 05 07 01 09 00 00` |
| ATTRIBUTE-trap-console | record / 7 | tile terrain determines interpretation: K9 | `12 05 08 01 09 00 00` |
| ATTRIBUTE-turret | record / 7 | tile terrain determines interpretation: Y9 | `12 05 09 01 09 00 00` |
| ATTRIBUTE-turret-pad | record / 7 | tile terrain determines interpretation: P9 | `12 05 0a 01 09 00 00` |
| MAP_LIST | record / 4 | one entry | `13 02 01 00` |
| MAP_ENTRY | record / 20 | map1 training-01 supports2..4 | `14 12 01 00 00 00 0b 74 72 61 69 6e 69 6e 67 2d 30 31 02 04` |
| EVENTS_META | record / 10 | since41; one event in this window | `16 08 29 00 00 00 01 00 00 00` |
| EVENT-PLANNING_CLOSED | record / 14 | PLANNING_CLOSED: version42 round3 tick0 sequence1; params bytes follow section7 field order | `15 0c 01 2a 00 00 00 03 00 01 00 00 00 02` |
| EVENT-ROUND_STARTED | record / 14 | ROUND_STARTED: version42 round3 tick0 sequence1; params bytes follow section7 field order | `15 0c 02 2a 00 00 00 03 00 01 00 00 00 03` |
| EVENT-ROUND_ENDED | record / 41 | ROUND_ENDED: version42 round3 tick3 sequence1; params bytes follow section7 field order | `15 27 03 2a 00 00 00 03 03 01 00 00 00 03 02 01 05 00 00 00 01 00 00 00 00 00 00 00 02 05 00 00 00 01 00 00 00 00 00 00 00` |
| EVENT-MATCH_ENDED | record / 42 | MATCH_ENDED: version42 round3 tick3 sequence1; params bytes follow section7 field order | `15 28 04 2a 00 00 00 03 03 01 00 00 00 03 00 02 01 05 00 00 00 01 00 00 00 00 00 00 00 02 05 00 00 00 01 00 00 00 00 00 00 00` |
| EVENT-ORDER_EXECUTED | record / 15 | ORDER_EXECUTED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 0d 05 2a 00 00 00 03 02 01 00 00 00 01 01` |
| EVENT-ORDER_NO_OP | record / 16 | ORDER_NO_OP: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 0e 06 2a 00 00 00 03 02 01 00 00 00 01 00 03` |
| EVENT-ORDER_SKIPPED_DISABLED | record / 15 | ORDER_SKIPPED_DISABLED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 0d 07 2a 00 00 00 03 02 01 00 00 00 01 08` |
| EVENT-ROBOT_MOVED | record / 19 | ROBOT_MOVED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 11 08 2a 00 00 00 03 02 01 00 00 00 01 02 03 03 03 00` |
| EVENT-ROBOT_PUSHED | record / 19 | ROBOT_PUSHED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 11 09 2a 00 00 00 03 02 01 00 00 00 02 01 03 03 04 03` |
| EVENT-MOVE_BLOCKED | record / 18 | MOVE_BLOCKED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 10 0a 2a 00 00 00 03 02 01 00 00 00 01 03 ff 03 00` |
| EVENT-ROBOT_ROTATED | record / 16 | ROBOT_ROTATED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 0e 0b 2a 00 00 00 03 02 01 00 00 00 01 00 01` |
| EVENT-CRATE_SPAWNED | record / 21 | CRATE_SPAWNED: version42 round3 tick0 sequence1; params bytes follow section7 field order | `15 13 0c 2a 00 00 00 03 00 01 00 00 00 00 00 01 00 e7 03 02 03` |
| EVENT-CRATE_PICKED_UP | record / 20 | CRATE_PICKED_UP: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 12 0d 2a 00 00 00 03 02 01 00 00 00 01 00 00 01 00 02 03` |
| EVENT-CRATE_DROPPED | record / 20 | CRATE_DROPPED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 12 0e 2a 00 00 00 03 02 01 00 00 00 01 00 00 01 00 02 03` |
| EVENT-CRATE_DELIVERED | record / 24 | CRATE_DELIVERED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 16 0f 2a 00 00 00 03 02 01 00 00 00 01 00 00 01 00 02 03 05 00 00 00` |
| EVENT-CRATE_MOVED | record / 21 | CRATE_MOVED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 13 10 2a 00 00 00 03 02 01 00 00 00 00 00 01 00 02 03 03 03` |
| EVENT-CRATE_DESTROYED | record / 20 | CRATE_DESTROYED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 12 11 2a 00 00 00 03 02 01 00 00 00 00 00 01 00 02 03 01` |
| EVENT-GROUP_TOGGLED | record / 18 | GROUP_TOGGLED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 10 12 2a 00 00 00 03 02 01 00 00 00 01 19 2c 01 01` |
| EVENT-LASER_HIT | record / 17 | LASER_HIT: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 0f 13 2a 00 00 00 03 02 01 00 00 00 01 02 03 02` |
| EVENT-ROBOT_DISABLED | record / 21 | ROBOT_DISABLED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 13 14 2a 00 00 00 03 02 01 00 00 00 01 00 02 03 00 00 00 00` |
| EVENT-RESPAWN_REDIRECTED | record / 18 | RESPAWN_REDIRECTED: version42 round3 tick3 sequence1; params bytes follow section7 field order | `15 10 15 2a 00 00 00 03 03 01 00 00 00 01 01 01 02 01` |
| EVENT-ROBOT_RESPAWNED | record / 17 | ROBOT_RESPAWNED: version42 round3 tick3 sequence1; params bytes follow section7 field order | `15 0f 16 2a 00 00 00 03 03 01 00 00 00 01 02 01 01` |
| EVENT-TRAP_ARMED | record / 20 | TRAP_ARMED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 12 17 2a 00 00 00 03 02 01 00 00 00 01 01 02 03 00 00 01` |
| EVENT-TRAP_TRIGGERED | record / 18 | TRAP_TRIGGERED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 10 18 2a 00 00 00 03 02 01 00 00 00 01 02 03 00 03` |
| EVENT-TRAP_HIT | record / 18 | TRAP_HIT: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 10 19 2a 00 00 00 03 02 01 00 00 00 01 02 02 01 01` |
| EVENT-TURRET_CAPTURED | record / 19 | TURRET_CAPTURED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 11 1a 2a 00 00 00 03 02 01 00 00 00 01 01 02 03 00 01` |
| EVENT-TURRET_CHARGING | record / 18 | TURRET_CHARGING: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 10 1b 2a 00 00 00 03 02 01 00 00 00 01 02 03 02 00` |
| EVENT-TURRET_FIRED | record / 22 | TURRET_FIRED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 14 1c 2a 00 00 00 03 02 01 00 00 00 01 02 03 01 02 01 02 03 01` |
| EVENT-TAR_SLOWED | record / 17 | TAR_SLOWED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 0f 1d 2a 00 00 00 03 02 01 00 00 00 01 01 02 03` |
| EVENT-SCORE_AWARDED | record / 23 | SCORE_AWARDED: version42 round3 tick2 sequence1; params bytes follow section7 field order | `15 15 1e 2a 00 00 00 03 02 01 00 00 00 01 08 04 02 0d 00 00 00 00 00` |
| EVENT-MATCH_ENDED-target | record / 42 | conditional variant target; common version42 round3 tick3 sequence1 | `15 28 04 2a 00 00 00 03 03 01 00 00 00 03 01 02 01 05 00 00 00 01 00 00 00 00 00 00 00 02 05 00 00 00 01 00 00 00 00 00 00 00` |
| EVENT-MATCH_ENDED-cap | record / 42 | conditional variant cap; common version42 round3 tick3 sequence1 | `15 28 04 2a 00 00 00 03 03 01 00 00 00 03 02 02 01 05 00 00 00 01 00 00 00 00 00 00 00 02 05 00 00 00 01 00 00 00 00 00 00 00` |
| EVENT-ROBOT_DISABLED-laser | record / 21 | conditional variant laser; common version42 round3 tick3 sequence1 | `15 13 14 2a 00 00 00 03 03 01 00 00 00 01 01 02 03 00 00 00 00` |
| EVENT-ROBOT_DISABLED-shunt | record / 21 | conditional variant shunt; common version42 round3 tick3 sequence1 | `15 13 14 2a 00 00 00 03 03 01 00 00 00 02 00 02 03 01 01 00 00` |
| EVENT-ROBOT_DISABLED-neutral-trap | record / 21 | conditional variant neutral-trap; common version42 round3 tick3 sequence1 | `15 13 14 2a 00 00 00 03 03 01 00 00 00 02 02 02 03 01 00 01 01` |
| EVENT-ROBOT_DISABLED-owned-turret | record / 21 | conditional variant owned-turret; common version42 round3 tick3 sequence1 | `15 13 14 2a 00 00 00 03 03 01 00 00 00 02 03 02 03 01 01 02 01` |
| EVENT-SCORE_AWARDED-trap-damage | record / 23 | conditional variant trap-damage; common version42 round3 tick3 sequence1 | `15 15 1e 2a 00 00 00 03 03 01 00 00 00 01 01 00 02 01 00 00 00 01 01` |
| EVENT-SCORE_AWARDED-trap-disable | record / 23 | conditional variant trap-disable; common version42 round3 tick3 sequence1 | `15 15 1e 2a 00 00 00 03 03 01 00 00 00 01 03 01 02 04 00 00 00 01 01` |
| EVENT-SCORE_AWARDED-turret-damage | record / 23 | conditional variant turret-damage; common version42 round3 tick3 sequence1 | `15 15 1e 2a 00 00 00 03 03 01 00 00 00 01 01 02 02 01 00 00 00 02 01` |
| EVENT-SCORE_AWARDED-turret-disable | record / 23 | conditional variant turret-disable; common version42 round3 tick3 sequence1 | `15 15 1e 2a 00 00 00 03 03 01 00 00 00 01 03 03 02 04 00 00 00 02 01` |
| EVENT-CRATE_DESTROYED-drop-blocked | record / 20 | conditional variant drop-blocked; common version42 round3 tick3 sequence1 | `15 12 11 2a 00 00 00 03 03 01 00 00 00 00 00 01 00 02 03 00` |
| EVENT-ORDER_NO_OP-reason0 | record / 16 | NOT_CARRYING/TILE_OCCUPIED use DROP; NO_CONSOLE uses TOGGLE | `15 0e 06 2a 00 00 00 03 02 01 00 00 00 01 06 00` |
| EVENT-ORDER_NO_OP-reason1 | record / 16 | NOT_CARRYING/TILE_OCCUPIED use DROP; NO_CONSOLE uses TOGGLE | `15 0e 06 2a 00 00 00 03 02 01 00 00 00 01 06 01` |
| EVENT-ORDER_NO_OP-reason2 | record / 16 | NOT_CARRYING/TILE_OCCUPIED use DROP; NO_CONSOLE uses TOGGLE | `15 0e 06 2a 00 00 00 03 02 01 00 00 00 01 07 02` |
| PAGE-created-final | payload / 34 | transfer1 offset0 total12 highwater0 origin CREATE final, CONTEXT+CREATED | `82 01 00 01 00 00 00 00 00 00 00 0c 00 00 00 00 00 00 00 11 01 0c 01 03 02 6d 31 02 05 04 03 c8 e8 03` |
| TCP-PAGE-created-final | frame / 36 | TCP length34 (22-byte page header plus12 record bytes) | `22 00 82 01 00 01 00 00 00 00 00 00 00 0c 00 00 00 00 00 00 00 11 01 0c 01 03 02 6d 31 02 05 04 03 c8 e8 03` |
| PAGE-created-first | invalid / 27 | Noncanonical short page: entire 12-byte stream fits P128; sender must emit final page. Receiver rejects non-greedy stream segmentation in fixture validation. | `82 01 00 01 00 00 00 00 00 00 00 0c 00 00 00 00 00 00 00 11 00 05 01 03 02 6d 31` |
| ECHO-max | record / 101 | 32-byte type and64-byte ASCII key: body99, record101, fits P128 | `07 63 20 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 41 02 40 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61` |
| TILES-max | record / 69 | 128 floor tiles, body67 | `11 43 00 00 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |
| TEXT-max | record / 76 | 64-byte description, body74 | `0f 4a 02 40 00 00 00 00 00 00 00 40 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61` |
| truncated-GET_EVENTS | invalid / 9 | missing max_versions high byte => BAD_MESSAGE | `16 01 00 00 29 00 00 00 10` |
| unknown-opcode | invalid / 3 | BAD_MESSAGE, no game dispatch | `7f 01 00` |
| oversize-prefix | invalid / 2 | payload241 exceeds max240: close, do not allocate | `f1 00` |
| PAGE-MAPS-P128-1 | payload / 127 | Canonical P128 page 1: transfer1 offset0 total249 H0 origin LIST_MAPS; 105 record bytes; final=0 | `82 01 00 01 00 00 00 00 00 00 00 f9 00 00 00 00 00 00 00 10 00 69 13 02 01 00 14 27 01 00 00 00 20 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 02 04 0f 3a 01 30 00 00 00 00 00 00 00 30 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e 6e` |
| PAGE-MAPS-P128-2 | payload / 98 | Canonical P128 page 2: transfer1 offset105 total249 H0 origin LIST_MAPS; 76 record bytes; final=0 | `82 02 00 01 00 00 00 69 00 00 00 f9 00 00 00 00 00 00 00 10 00 4c 0f 4a 02 78 00 00 00 00 00 00 00 40 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64` |
| FETCH-MAPS-P128-2 | payload / 11 | Fetch transfer1 offset105 | `03 02 00 01 00 00 00 69 00 00 00` |
| PAGE-MAPS-P128-3 | payload / 90 | Canonical P128 page 3: transfer1 offset181 total249 H0 origin LIST_MAPS; 68 record bytes; final=1 | `82 03 00 01 00 00 00 b5 00 00 00 f9 00 00 00 00 00 00 00 10 01 44 0f 42 02 78 00 00 00 40 00 00 00 38 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64 64` |
| FETCH-MAPS-P128-3 | payload / 11 | Fetch transfer1 offset181 | `03 03 00 01 00 00 00 b5 00 00 00` |
| SUBMITTED-PLANNING_CLOSED | record / 26 | PLANNING_CLOSED, ok=false detail4 response_version42, key b:7; append exact diagnostic | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 00 10 04 01 07 00 00 00` |
| SUBMITTED-WRONG_ROUND | record / 26 | WRONG_ROUND, ok=false detail4 response_version42, key b:7; append exact diagnostic | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 00 11 04 01 07 00 00 00` |
| SUBMITTED-PROGRAM_LOCKED | record / 26 | PROGRAM_LOCKED, ok=false detail4 response_version42, key b:7; append exact diagnostic | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 00 12 04 01 07 00 00 00` |
| SUBMITTED-INVALID_REVISION | record / 26 | INVALID_REVISION, ok=false detail4 response_version42, key b:7; append exact diagnostic | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 00 13 04 01 07 00 00 00` |
| SUBMITTED-STALE_REVISION | record / 26 | STALE_REVISION, ok=false detail4 response_version42, key b:7; append exact diagnostic | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 00 14 04 01 07 00 00 00` |
| SUBMITTED-REVISION_CONFLICT | record / 26 | REVISION_CONFLICT, ok=false detail4 response_version42, key b:7; append exact diagnostic | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 00 15 04 01 07 00 00 00` |
| SUBMITTED-REQUEST_ID_REUSED | record / 26 | REQUEST_ID_REUSED, ok=false detail4 response_version42, key b:7; append exact diagnostic | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 00 16 04 01 07 00 00 00` |
| SUBMITTED-REQUEST_LIMIT | record / 26 | REQUEST_LIMIT, ok=false detail4 response_version42, key b:7; append exact diagnostic | `05 18 01 00 00 00 03 00 00 00 01 00 00 00 2a 00 00 00 00 17 04 01 07 00 00 00` |
| SNAP_META-target-no-deadline | record / 33 | flags1: target present, deadline null; other fields as target-executing example | `09 1f 01 03 02 c8 e8 03 01 00 01 00 01 00 00 00 01 04 02 00 01 01 01 0f 0f 00 00 00 00 00 00 00 00` |
| SNAP_META-deadline-no-target | record / 33 | flags2: target absent, deadline present; other fields as target-executing example | `09 1f 01 03 02 c8 00 00 01 00 01 00 01 00 00 00 02 04 02 00 01 01 01 0f 0f 00 f1 53 65 00 00 00 00` |
| PAGE-EVENTS-same-version-1 | payload / 127 | Synthetic packing fixture P128: version42 spans all three pages; sequences1..20, cursor0, total315, final=False. Commit version42 only on final; not a reachable engine trace. | `82 01 00 01 00 00 00 00 00 00 00 3b 01 00 00 2a 00 00 00 16 00 69 01 03 02 6d 31 16 08 29 00 00 00 14 00 00 00 15 0d 05 2a 00 00 00 03 02 01 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 02 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 03 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 04 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 05 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 06 00 00 00 01 08` |
| PAGE-EVENTS-same-version-2 | payload / 127 | Synthetic packing fixture P128: version42 spans all three pages; sequences1..20, cursor105, total315, final=False. Commit version42 only on final; not a reachable engine trace. | `82 02 00 01 00 00 00 69 00 00 00 3b 01 00 00 2a 00 00 00 16 00 69 15 0d 05 2a 00 00 00 03 02 07 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 08 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 09 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 0a 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 0b 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 0c 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 0d 00 00 00 01 08` |
| PAGE-EVENTS-same-version-3 | payload / 127 | Synthetic packing fixture P128: version42 spans all three pages; sequences1..20, cursor210, total315, final=True. Commit version42 only on final; not a reachable engine trace. | `82 03 00 01 00 00 00 d2 00 00 00 3b 01 00 00 2a 00 00 00 16 01 69 15 0d 05 2a 00 00 00 03 02 0e 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 0f 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 10 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 11 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 12 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 13 00 00 00 01 08 15 0d 05 2a 00 00 00 03 02 14 00 00 00 01 08` |

## Multi-page and recovery sequences

The complete `PAGE-MAPS-P128-*` fixtures transfer one list entry with a 32-byte map ID,
48-byte name and 120-byte description. At P128, the first page contains MAP_LIST, MAP_ENTRY
and name TEXT (105 data bytes); the next pages carry description fragments of 76 and 68 record
bytes. Cursors are 0,105,181, final total249. FETCH fixtures give exact subsequent requests.
Retrying a prior returned cursor replays the same page. All fragments retain field,total,offset.
The `PAGE-created-first` negative fixture deliberately fails greedy packing: that entire response
fits one page. This is a producer conformance test, not permission to guess unknown later bytes
at runtime; a receiver can validate known data/offset/final consistency without lookahead.

Recovery trace: HELLO(new) -> HELLO_OK(secret S,last0); JOIN exchange1 commits but reply is lost;
HELLO(resume S) -> HELLO_OK(last1); exact JOIN exchange1 -> original PAGE/JOINED credential,
without another seat. If S expired, SESSION_EXPIRED ends recovery; do not send a new JOIN implicitly.

Submission trace: after binding P1, SUBMIT numeric7 succeeds at version42; the round advances;
retry with a fresh exchange but numeric7 and identical payload -> original response_version42.
Changing an order with numeric7 -> REQUEST_ID_REUSED. Switching to JSON request_id b:7 gives
the same H3 record. REQUEST_LIMIT is not recorded and numeric7 can be retried next round.

All finite enum members have their values in wire.md; field boundaries use the scalar definitions.
B2 adds exhaustive invalid enum/flag mutations and truncation at every byte. These examples provide
every operation, reply/control kind, record type, event type, error code, and conditional field shape.

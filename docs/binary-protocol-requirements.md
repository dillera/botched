# Botched! binary wire protocol — normative requirements for M1

**Status:** B1–B3 accepted; B4 implemented for review, 2026-11-23. B5 not started.
B2 evidence and explicit budget deviations: [codec review](binary-v1/b2-review.md).
B3 direct TCP / Linux terminal and SDL acceptance: [transport review](binary-v1/b3-review.md).
B4 portable NIO adapter and real POSIX NIO acceptance: [NIO review](binary-v1/b4-review.md).
**Authority:** current G1–G4 game implementation and the owner-confirmed decisions below.
**Rules version:** `botched/1`, the initial **unreleased** ruleset; no `botched/2` migration.
**Binary wire:** major 1, minor 0, independent of the rules label. Existing JSON remains unchanged.

The normative B1 deliverables are [wire layouts and state machines](binary-v1/wire.md),
[annotated byte examples](binary-v1/examples.md), [literal fixtures](binary-v1/fixtures.json),
and [resource budgets and map census](binary-v1/resources.md). These supersede the earlier
proposal details in sections 5 and 10 wherever noted. B1 freezes the design for this review;
B2 must implement it or propose an explicit correction, not silently choose different bytes.

**Keywords.** MUST, SHOULD and MAY are used as in the game spec. "Target" means a budget proposed here
that M1 must validate with measurements and may revise by an approved change to this document.

## Current baseline — November 2026

The binary protocol targets the completed G1–G4 gameplay, not the original M0 rules alone. Its purpose is to provide a second wire representation of the **same authoritative game**, with no changes to gameplay semantics.

The protocol MUST support:

- **G1:** Score-target matches, round caps, match results and tie-break information.
- **G2:** Proximity traps, ownership, multi-console TOGGLE, continuous lasers, charging/READY turrets, tar, SHUNT attribution and sabotage scoring.
- **G3:** All supported map terrain, devices, consoles, groups, IDs and metadata required for gameplay.
- **G4:** Server-managed bot seats, readiness and personality labels. Bot goals and diagnostic reasoning remain server-only.
- **Presentation:** All authoritative events and state needed for clients to reconstruct the board and independently animate round execution, including intermediate movement, machinery, scoring, disable and respawn.
- **Recovery:** Initial snapshots and resynchronisation, including published mid-round state needed for correct rendering. Binary event pagination and credential rebinding are new adapter requirements, not existing JSON operations. Durable reconnect after server restart remains outside this work.

### Platform and compatibility requirements

- JSON and binary clients MUST be able to participate in the same match.
- The binary message format MUST remain independent of transport. Support ordinary TCP clients as well as FujiNet/NIO transport adapters without introducing gameplay-specific framing into the codec.
- The format MUST be practical for Amiga C clients and constrained 6502-class clients, with bounded buffers and no required heap allocation.
- The existing size, memory and CPU budgets remain targets to measure and validate, not assumptions.
- Binary wire version and gameplay rules version are separate: binary 1.0 carries the initial unreleased `botched/1` ruleset, as confirmed by the owner.
- Preserve existing JSON behaviour, engine determinism and authoritative event ordering.

B1 has reconciled the message/event inventory and resolved O1–O13 below. Future read-only spectators have a reserved role but are not implemented. Implementation proceeds only through separately authorized milestones below; firmware changes remain excluded.

## 1. Principle

> **One authoritative game engine, multiple wire representations.**

```
  retro client (C / 6502 asm)          POSIX tools, tests, debugging
          |  binary                              |  JSON
   +--------------+                       +--------------+
   | binary codec |                       |  JSON codec  |   <- representation only
   +------+-------+                       +------+-------+
          |    typed commands / events / snapshots (semantic structs)
          +-----------------------+--------------+
                          GameServer (rooms, sessions, deadlines, idempotency)
                                  Match / Engine (rules; no I/O)
```

* The rules library and `GameServer` stay representation-agnostic. A codec converts bytes to the typed
  semantic values and back. **No gameplay logic in a codec.** Today `GameServer::handle()` accepts/returns
  `Json`, and `Event` contains a string type and `Json params`; there is no typed event enum or complete
  typed command layer. Initially bridge binary values to this existing API on the server. A shared
  schema/code table can prevent drift without a broad engine/API refactor. The diagram is the target seam.
* JSON stays: it is the reference/debug representation, the format of golden test logs and the
  baseline for equivalence tests (R-T1).
* Transport (FujiBus/SLIP/packet link), serialization and rules remain independent layers.

## 2. Why this is a first-class requirement (measured JSON baseline)

Historical JSON costs on the training map (4 players, reported 2026-11-20 from the M0 build;
not remeasured here and not a G1–G4 bound; current registry ID is `training-01`):

| Item | JSON today |
|---|---|
| `SUBMIT_PROGRAM` (64-char session, 17-char `request_id`) | 246 B |
| Programme response (accepted / rejected) | 172 B / 208 B |
| `GET_EVENTS` request / error response | 56 B / 134 B |
| Full snapshot | 1 047 B (largest observed 1 109 B) |
| Map definition | 597 B |
| Events per published version | avg 4.9 events / ~700 B; max 14 events observed over 20 random 4-player matches (1 642 B in the largest single version seen) |
| One event | 147 B average |

These need an `atoi()`-style integer parse per number, string compares for every name, and a JSON
tokeniser with dynamic memory. A 1 MHz 6502 with a few KB of free RAM cannot afford that per tick.
Section 5 retains the earlier size/CPU aspirations for comparison. No reduction factor is established
for the current game; full-hazard maps, long matches and metadata must be measured.

## 3. Transport baseline and binding

The existing [NetworkDevice TCP contract](../../../repos/fujinet-nio/docs/network_device_tcp.md)
and [NetworkDevice command layouts](../../../repos/fujinet-nio/docs/network_device_protocol.md)
provide Open/Read/Write/Info/Close over `tcp://host:port`. Use that existing service for the first
FujiNet/NIO client. A new Botched firmware device is unnecessary for this sequence (O1).

* Direct TCP and TCP reached through NIO are byte streams. Neither a socket read nor a FujiBus
  Read response is a game-message boundary. The transport adapter MUST assemble partial reads,
  retain partial writes, and handle several messages arriving together.
* Stream envelope: `u16 LE payload_length` followed by one binary game payload, length
  3..negotiated maximum. The codec receives only that payload. A future native message transport
  supplies its existing length instead; no second envelope is needed there.
* No game CRC, sync, escaping or link retransmission. FujiBus/SLIP/packet-link integrity remains
  below NetworkDevice. A stream length is necessary here because those frames do not delimit
  end-to-end game messages.
* NIO handles are u16 network resources, **not authenticated game seats**. Read/write cursors
  advance only by bytes actually transferred. Respect NotReady/DeviceBusy, EOF and the documented
  immediately-previous-operation replay rules; never blindly resend already accepted stream bytes.
* 240 bytes is a maximum **game payload**, not a guaranteed single FujiBus transaction. Account
  for NetworkDevice prefixes, parameters, cursor fields and link overhead; split reads/writes to
  fit the actual target buffer. A 256-byte client buffer must not be justified by `240 + 6` alone.
* Keep existing JSON-line TCP behavior. Use a separate configurable binary listener sharing
  the same `GameServer` and rooms, avoiding byte sniffing or changes to the JSON handshake.

## 4. Requirements

### 4.1 Numbers, identifiers and parsing

* **R-N1** All numeric fields MUST be binary with explicit widths. No ASCII integers, no `atoi`, `strtol`
  or decimal formatting on a client. Fields MUST be 8-bit or 16-bit unless a value cannot fit.
* **R-N2** Multi-byte fields MUST have one documented byte order for the whole protocol:
  **little-endian**, matching FujiBus and the 6502 (O4).
* **R-N3** Commands, orders, events, object kinds, terrain, headings, phases, outcome codes and error
  codes MUST be small numeric enumerations (one byte), defined in one normative table with stable values.
  The JSON string names (`FORWARD_2`, `ROBOT_MOVED`, `REQUEST_LIMIT`...) map 1:1 to those values.
  Numeric values MUST never be reused or renumbered within a major protocol version.
* **R-N4** Gameplay traffic MUST use compact numeric references after binding a match/seat.
  Do not renumber authoritative entity IDs: B1 selects u32 crate IDs (including restored state), u8 player/group/hazard IDs and
  u16 dispenser/console references. Coordinates need a signed representation for off-board
  `MOVE_BLOCKED` targets (section 10.4). Lobby strings and credential exchange remain bounded.
* **R-N5** Messages SHOULD consist of fixed-size, fixed-offset fields so a client decodes with byte loads and
  a table or jump on the opcode. Variable-length parts MUST carry an explicit count first.
* **R-N6** Bit-packing MAY be used selectively, only where it is plain shifts and masks on a single byte
  (for example heading in 2 bits plus flags, or two 4-bit order codes). It MUST NOT cross byte boundaries,
  use variable-length (varint) integer encodings, or need multiply/divide on the client.
* **R-N7** Every message MUST be decodable with bounded, non-allocating work over one packet
  buffer. Validate the complete packet before applying callbacks; a second bounded scan of that same
  buffer is allowed by B1 to preserve atomic rejection. Never read beyond the declared length; malformed input (truncated, over-long, unknown opcode, out-of-range value)
  MUST be rejected, not partially applied. Equivalent of the H2 input limits for JSON.

### 4.2 Content of the protocol

* **R-C1** The protocol carries **semantic state and events only**: no graphics, sprite or character
  data, animation scripts, timing/animation instructions or screen layouts (spec section 1, 10).
* **R-C2** Normal play uses **compact semantic event updates**: one event is an opcode plus its complete
  typed fields from section 10.2, including origins, causes and optional attribution. A published version
  carries its events in engine order with the same `(round, tick, sequence)` and `state_version` identity as JSON.
* **R-C3** **Full binary snapshots** are provided for initial synchronisation, reconnect, and recovery
  after `SNAPSHOT_REQUIRED`. A client MUST be able to resynchronise from a snapshot alone, with no replay
  of earlier events (spec section 10), given the matching static map. Status supplies bot labels
  and winners; no secret checkpoint data is implied.
* **R-C4** **Deltas** during normal play are the events themselves. A separate delta-snapshot format is
  not required (O8). Eventless readiness and deadlines are refreshed through existing status/snapshot semantics.
* **R-C5** **Static map data** (terrain, spawns, docks, dispensers, conveyor directions, consoles,
  lasers, traps/consoles, turrets/pads and tar generators) MUST be transferable once, identifiable by a compact map id/revision, and cacheable so reconnects
  do not resend it. Terrain SHOULD be coded in 4 bits per tile or one byte per tile (O6).
* **R-C6** Programme submission, readiness and the match/status queries of spec section 9.2 MUST each
  have a compact binary form with the same semantic fields (`round`, `revision`, 3 orders, `ready`,
  request correlation).
* **R-C7** Preserve absolute `state_version` and sequence as u32; no relative-version scheme
  or silent wrap (O8). Versions can advance without events; round/tick/sequence are not an ACK
  for an entire version until its final page has been consumed. Readiness/deadlines require
  status/snapshot reads as detailed in section 10.5.

### 4.3 Memory, size and processing

* **R-M1** **No dynamic allocation** is required to decode ordinary packets on a constrained client. A
  client MUST be implementable with static buffers and a fixed-size game-state mirror.
* **R-M2** **Explicit maximum packet sizes and bounded buffers.** Every message type has a stated maximum
  length; the protocol has a global maximum game payload (target: 240 B, with separate transport-overhead accounting, O3). Anything that could exceed it (events after a busy tick,
  map transfer, snapshots, status and lobby metadata) MUST be defined as chunked/continued, never as an oversize packet.
* **R-M3** The server MUST NOT send, and a client MUST NOT need to buffer, more than one maximum packet at
  a time for ordinary play (matches one outstanding FujiBus exchange).
* **R-M4** Budgets in section 5 are normative targets; M1 MUST report measured values against them.

### 4.4 Versioning and negotiation

* **R-V1** The protocol MUST be versioned and negotiated at session start with a capability exchange:
  protocol version(s), rules version (`botched/1`), supported encodings, the client's maximum packet size
  and optional features. The server MUST pick the highest common version, or refuse explicitly.
* **R-V2** Length delimiting permits safely discarding an unknown message but does not make
  an unknown state-changing event safe to ignore. Reject unsupported requests without dispatch;
  stop applying unsupported events and renegotiate or recover with a supported snapshot.
  In v1 reject unknown flags, fields and trailing bytes. Future negotiated minor extensions may
  specify skippable optional fields; never unconditionally ignore them. This resolves the old
  conflict with strict R-N7/R-T5.
* **R-V3** The encoding MUST be independent of the rules version: a new rules version bumps `botched/N`
  per the game spec's freeze rule; a new encoding version does not.

### 4.5 Layering with existing NIO transport

* **R-X1** The binary protocol is a **payload** carried by the existing FujiNet/NIO transport (FujiBus
  commands carrying TCP stream reads/writes, over SLIP or the packet link). The stream adapter
  supplies message lengths; the codec supplies no framing, escaping or integrity checks (section 3).
* **R-X2** Do not duplicate: length framing, CRC/checksum, retransmission of lost transport frames and
  link-level generation/handshake are the transport's. The game protocol handles only game-level
  correlation and idempotency (R-X3).
* **R-X3** **Request correlation and idempotency** follow the approved H3 contract
  (`docs/request-id-contract.md`): `(player, request id)` single-use, replay, `REQUEST_ID_REUSED`,
  per-round budget, `REQUEST_LIMIT`. A compact binary **per-player request key** (B1:
  u32) MAY be offered instead of arbitrary ASCII ids, provided it maps into the same keyspace and
  preserves exactly those semantics. H3 is **not** changed here to anticipate the binary format (O5).
* **R-X4** Session identification MUST NOT require a long ASCII token on every message. A token (or
  equivalent credential) MAY be exchanged once at join; afterwards a compact session/slot handle bound to
  the transport session identifies the player (O2). The security properties of H2/H1 (authenticated slot,
  constant-time check where a secret is compared) MUST be preserved.
* **R-X5** Transport, serialization and gameplay rules stay independent: a codec has no I/O, the
  transport has no game knowledge, `Match`/`Engine` have no wire knowledge.

### 4.6 Equivalence and testing

* **R-T1** **Identical semantics.** JSON and binary requests MUST yield identical authoritative game
  state, identical `state_version`s and identical event streams. Neither encoding may enable, disable
  or reorder anything.
* **R-T2** **Golden binary fixtures:** a checked-in, versioned set of byte-exact messages (hex plus
  an annotated decode) for every message type, including boundary values and each error code. Fixtures
  are the cross-language contract (C, Python tools, 6502).
* **R-T3** **Round-trip tests:** encode(decode(bytes)) == bytes for every fixture; decode(encode(x)) == x
  for generated values across the full legal domain.
* **R-T4** **Cross-encoding equivalence tests:** one command/event script is driven through the JSON and the
  binary adapters against separate `GameServer` instances; assert equal `state_hash`, equal canonical event
  streams (decoded), equal responses (semantically) and identical replay/idempotency behaviour, including
  full simulated matches (`botched-sim`) with missed submissions and retries.
* **R-T5** **Malformed-input tests:** truncation at every byte, every unknown opcode and flag, out-of-range
  values, over-long counts, and seeded mutation fuzz. The decoder never reads past the length, never
  allocates, and a refused message mutates nothing (the H2 invariant).
* **R-T6** **Size and budget tests:** assert the B1-approved maxima per message type and negotiated
  payload limit; report measurements against the historical section 5 aspirations separately.
* **R-T7** **No-allocation proof:** the reference decoder runs under an allocation-failing hook in tests.
* **R-T8** **Implementability:** a portable C reference decoder/encoder (C99, no libc allocation, no
  floating point, no `atoi`) ships with the fixtures and is tested in CI. The format MUST also be
  implementable in 6502 assembly (ca65), verifiable with the workspace `soft65c02` tester against the
  same fixtures (M2 retro ports).

## 5. Proposed budgets for 8-bit clients (targets to validate)

Historical estimates, derived from the M0 semantic model; not current measurements. M1 replaces them with measured numbers
via R-M4 and R-T6.

### 5.1 Historical size aspirations — superseded as hard limits

The table below is retained to make the budget change explicit, not as an implementable layout.
The hard per-message limit is 240 bytes, with pagination. In particular, revision is
not u8, complete events are not universally 2–6 bytes, full snapshots need not fit 240 bytes, and
terrain-only sizes omit all tile attributes and metadata. R-T6 must enforce measured, approved
layouts from step B1, not these historical estimates.

Bytes of game payload; all transport overhead is extra:

| Message | JSON today | Target binary | Basis |
|---|---|---|---|
| `SUBMIT_PROGRAM` | 246 | **<= 10** | opcode 1 + request seq 2 + round 1 + revision 1 + 3 orders (1 each, or 2 if packed) + flags 1 |
| Programme response | 172-208 | **<= 8** | status/outcome 1 + echoed seq 2 + revision 1 + `state_version` 4 |
| Full snapshot, 4 players | 1 047 | **<= 110 typical, <= 240 hard max** | header ~6; 4 players x ~7 (x,y,heading+flags,damage/carry,score,deliveries,damage_taken); up to ~16 crates x 3; group bitmask; chunk if larger |
| Map definition (12x10) | 597 | **<= 70 (4-bit tiles: 60 + header)**; 20x16 max <= 170 | transferred once, cached |
| One event | 147 (avg) | **2-6** | opcode + 1-4 fixed bytes |
| Events per published version (avg / observed max) | ~700 / 1 642 (4.9 / 14 events) | **<= 30 B avg, <= 64 B cap** (chunk beyond cap) | 14 events observed; the true engine worst case must be derived in M1 before the cap is frozen |
| `GET_EVENTS` request | 56 | **<= 6** | opcode + since-version |
| Error response | 134 | **<= 4** | code + echoed seq |

### 5.2 Client RAM targets (protocol and state mirror only, excludes UI and OS)

The earlier allocation below remains a **1 KB target**, not a proof for all valid maps. The engine
allows up to 320 board cells plus four carried crates, not 16 live crates; terrain attributes,
hazards, full score counters and paging state cost additional RAM. Do not cap the engine or
silently omit entities to meet this table. O3/O7/O10 require a measured constrained-client profile
and admission check before joining/starting a match; unrestricted clients must retain all legal
semantics. Derive worst-case live storage, not just initial crate count. The B1 census names every shipped map as outside K1 under the conservative complete-mirror
allocation; all fit K4. This explicit budget result is part of the current review.
No heap, bounded decoding over one packet and one receive packet remain mandatory even on larger clients.

Earlier allocation aspirations:

| Item | Target |
|---|---|
| Receive buffer | 256 B (one maximum packet) |
| Transmit buffer | 32 B for a `SUBMIT_PROGRAM` |
| Game-state mirror: 4 players, <= 16 crates, groups | <= ~120 B |
| Cached map, 12x10 / 20x16 at 4 bits per tile | 60 B / 160 B |
| Protocol bookkeeping (session handle, last sequence/version, round, tick, phase) | <= 32 B |
| **Total** | **<= ~700 B (12x10) / <= ~800 B (20x16); hard budget 1 KB** |

### 5.3 CPU (1 MHz 6502-class, cycle targets for decode)

| Operation | Target |
|---|---|
| Decode one event (opcode dispatch + field loads) | <= ~150 cycles |
| Decode a typical version of events (~5, cap 14+) | <= ~750 cycles typical, <= ~2 100 cycles at 14 events |
| Decode a full snapshot | <= ~3 000 cycles (~3 ms) |
| Encode `SUBMIT_PROGRAM` | <= ~400 cycles |
| Forbidden on the decode path | multiply, divide, decimal conversion, string compare, heap, recursion |

These are unverified aspirations for the old event shapes, not a demonstrated timing guarantee.
Measure full-hazard pages and chunked snapshots; clients decode while animating at their own speed
(spec section 10: clients must never block the server).

## 6. Relationship to M0 artefacts

* **JSON stays normative for reference tooling:** `botched-sim`, `botched-server`, golden JSON logs,
  `docs/protocol-limits.md`. Binary limits (R-N7, R-M2) mirror the H2 limits and must be documented
  alongside them.
* **Semantic structs are the contract between codecs and engine:** `Event`, `GameState`, programme
  records, `SubmitResult` codes. The implementation must establish one schema for binary and JSON
  type names to prevent drift; today event types are strings, not an enum.
* **Idempotency** is H3. **Checkpoint/recovery** (M1-02) is separate and unaffected by encoding.
* **No M0 code change** is needed for this document; M1 adds a codec layer beside the JSON one.

## 7. B1 decision register — O1–O13 resolved

No B1 technical choice remains deferred to codec implementation. Details and limits are in
[wire.md](binary-v1/wire.md); measurements that require code remain B2/B5 acceptance, not design gaps.

| # | Decision |
|---|---|
| **O1** | Existing NIO NetworkDevice TCP and direct TCP use identical u16-LE stream envelopes. No new device, command or firmware change. |
| **O2** | One bound player seat per adapter session; existing credentials authenticate BIND. Server-generated recovery secret and one-exchange replay cache recover lost JOIN/CREATE replies within a bounded lease. Future spectator role 2 is reserved and rejected; explicit public query scope preserves today's API. |
| **O3** | Payload negotiated 128..240, 256-byte receive buffer, pull pages; complete records <=101 bytes. Fixed K1/K4/K8 ceilings with immutable-map admission and explicit measured implementation gates. |
| **O4** | All multibyte integers little-endian. |
| **O5** | u32 keys map to canonical `b:N`; optional exact ASCII key mode and unkeyed mode. Same H3 keyspace, budget and outcomes across JSON/binary. Retain pending bytes/counter on reconnect; server supplies next-key hint, no wrap. Exchange IDs are separate bounded adapter replay. |
| **O6** | Row-major low-nibble-first terrain plus five-byte sparse attributes, complete map metadata and streamed text. Cache by server epoch/map handle; no RLE, collision-prone hash or ID renumbering. |
| **O7** | u32 crate IDs/next ID preserve restored values; u8 players/groups/hazards, u16 dispensers/consoles/counts, i8 coordinates, i32 request numeric domains, u32 scores/counters/version/sequence. No score truncation or crate recycling. |
| **O8** | Exact event records with absolute version/round/tick/sequence; no delta-snapshot format. Existing status/snapshot queries refresh eventless state. |
| **O9** | One frozen transfer/session, greedy whole-record pages, byte cursors, immutable high-water, explicit final/expiry. GET_EVENTS windows cover 1..16 complete versions; repeated windows reconstruct the JSON result. Fixed 4 MiB transfer/64 MiB global caps and 1800-second idle lease. |
| **O10** | Owner confirmed initial unreleased ruleset `botched/1`, wire 1.0; mandatory completed-G1–G4 capabilities. No retrospective rules-version bump or JSON hash changes. |
| **O11** | Frozen numeric tables and exact diagnostic TEXT records preserve all outcome/error distinctions. Adapter faults and NetworkDevice statuses are separate namespaces. |
| **O12** | All nine API operations have binary forms including full lobby creation/bots/target/cap/planning defaults. No JSON-only setup. |
| **O13** | Mixed JSON/binary connections share GameServer, rooms, clock and H3. Separate binary listener; representation never changes gameplay. |

## 8. M1 acceptance criteria for this requirement

1. A written protocol specification with the opcode and code tables (R-N3), message layouts, maxima
   (R-M2), negotiation (R-V1) and the resolved decisions O1-O13.
2. A binary codec beside the JSON codec, sharing the semantic structs, with no engine changes.
3. Golden fixtures, round-trip, equivalence, malformed-input, size and no-allocation tests (R-T2..T7) passing,
   including a full simulated 4-player match driven through the binary adapter with the same final
   state hash as JSON.
4. A C reference implementation and measured RAM/size/cycle figures against section 5.
5. Direct TCP and NIO NetworkDevice TCP adapters verified against section 3, including partial I/O,
   deadlines without incoming requests, pagination and mixed-client play. No new FujiBus device is required.

## 9. Non-goals

Implementing a codec in this documentation step; defining retro platform renderers or input (M2); changing `botched/1` gameplay;
encryption; replacing FujiBus, SLIP or the packet link.

## 10. Reconciled semantic inventory

Source of truth for this audit: [server API](../server/server.cpp),
[match lifecycle](../src/match.cpp), [engine event producers](../src/engine.cpp),
[world serialization](../src/serialize.cpp), [map parser/serializer](../src/map.cpp),
[vocabulary](../include/botched/types.hpp) and [event cursor](../src/state.cpp).
The older milestone document has stale “not implemented” and stop-gate text; the completed G1–G4
baseline and current code govern this design. No gameplay or historical document is rewritten here.

### 10.1 Requests, replies and errors

Every request has `type`. Except LIST_MAPS and CREATE_MATCH, requests carry `match_id` in JSON;
binary binding may supply it without repeating its string. Optional/defaulted fields must remain
optional in the semantic mapping. Responses carry `type` and `ok`; nested object names below matter.
This is the complete current public API (nine operations), not merely spec §9.2's five queries/actions.

| Request | Additional input | Successful response and content |
|---|---|---|
| LIST_MAPS | none | MAPS: `maps[]` in ascending ID order, each `map_id,name,description,min_players,max_players` |
| CREATE_MATCH | optional `map_id,player_count,bots,rounds,target_score,planning_seconds` | MATCH_CREATED: `match_id,player_count,bots,rounds,target_score` |
| JOIN_MATCH | none | JOINED: `match_id,player_id,session,started`; last human join may start the match |
| START_MATCH | none | MATCH_STARTED: `match_id,player_count`; current API does not authenticate this operation |
| SUBMIT_PROGRAM | `player_id,session,round,revision,orders[3]`; optional `ready,request_id` | SUBMIT_PROGRAM: `ok,code,message,match_id,player_id,round,revision,state_version`, optional `request_id`; rules refusals use this same response type |
| GET_SNAPSHOT | none | SNAPSHOT: `snapshot` object (10.3) |
| GET_EVENTS | `since_version` | EVENTS: `match_id,state_version,events[]`; currently returns all retained events newer than the cursor, without pagination |
| GET_MAP | none | MAP: `match_id,map` object; available in lobby as well as after start |
| GET_MATCH_STATUS | none | MATCH_STATUS: `status` object, with separate lobby/running shapes (10.3) |

CREATE defaults: map `training-01`, four players, zero bots, target 0, rounds 12 without a target
or 100 with one, planning time from server configuration. Limits: players 2–4, bots 0–3 and at
least one human seat, rounds 1–200, target 0–1000, planning seconds 0–86400. There is no separate
READY, LEAVE, REJOIN, GET_RESULTS or server-push message today. Ready is part of submission;
results are MATCH_ENDED events and finished status. Binary HELLO/BIND/continuation controls are
new adapter controls, not engine messages. Negotiation, binding and paging must never publish a
state version or consume a submission ID budget.

ERROR replies have `ok=false,code,message` and conditionally echo valid `request_type,request_id`.
Complete game-code inventory (B1 numeric assignments are in wire.md §5):

* Server errors: `REQUEST_TOO_LARGE`, `BAD_REQUEST`, `UNKNOWN_REQUEST`, `UNKNOWN_MATCH`,
  `UNKNOWN_MAP`, `TOO_MANY_MATCHES`, `INVALID_MAP`, `ALREADY_STARTED`, `MATCH_FULL`,
  `START_FAILED`, `NOT_STARTED`, `UNKNOWN_PLAYER`, `UNAUTHORIZED`, `INVALID_ORDERS`,
  `SNAPSHOT_REQUIRED`.
* Submission outcomes: `OK` (accepted **or** unchanged), `PLANNING_CLOSED`, `WRONG_ROUND`,
  `PROGRAM_LOCKED`, `INVALID_ORDERS`, `INVALID_REVISION`, `STALE_REVISION`,
  `REVISION_CONFLICT`, `REQUEST_ID_REUSED`, `REQUEST_LIMIT`.

Do not collapse ERROR into a rules-level submission refusal: validation order determines whether
an H3 record is created. Preserve echoed round/revision and the original response version on
replay, even after the server has advanced. The last ready submission can trigger a complete
round **after** its reply is constructed. A malformed request cannot apply its submitted action;
the existing server may still advance a previously expired deadline when handling it.
Adapter-only failures (unsupported version/profile, bad frame, expired transfer, invalid binding)
have a separate numeric namespace in wire.md. Diagnostic strings may be streamed; do not discard their
semantic distinctions to meet the obsolete four-byte error aspiration.

### 10.2 All authoritative events

Every event carries `match_id,state_version,round,tick,sequence,type,params`. Batch compression may
supply repeated context once but must restore these exact values. Sequence starts at 1 and resets
when round/tick changes, not per packet. The third tick and round-end effects share a sequence.
Fields below are the complete `params` keys; `?` marks conditional presence, not a zero default.
Positions in this table are pairs of independent fields, never inferred animation endpoints.

| Event | Params |
|---|---|
| PLANNING_CLOSED | `missing[]` player IDs |
| ROUND_STARTED | `round` |
| ROUND_ENDED | `round,scores[]` |
| MATCH_ENDED | `winners[],scores[],reason?` (`target`/`cap`, absent in fixed-round mode) |
| ORDER_EXECUTED | `player,order` |
| ORDER_NO_OP | `player,order,reason` |
| ORDER_SKIPPED_DISABLED | `player,order` |
| ROBOT_MOVED | `player,from_x,from_y,x,y,cause` |
| ROBOT_PUSHED | `player,by,from_x,from_y,x,y` |
| MOVE_BLOCKED | `player,dir,x,y,cause` (target may be outside board) |
| ROBOT_ROTATED | `player,from,to` headings |
| CRATE_SPAWNED | `crate,dispenser,x,y` |
| CRATE_PICKED_UP | `player,crate,x,y` |
| CRATE_DROPPED | `player,crate,x,y` |
| CRATE_DELIVERED | `player,crate,x,y,score` |
| CRATE_MOVED | `crate,from_x,from_y,x,y` |
| CRATE_DESTROYED | `crate,x,y,reason` |
| GROUP_TOGGLED | `player,group,console,reversed` |
| LASER_HIT | `player,emitter_x,emitter_y,damage` |
| ROBOT_DISABLED | `player,cause,x,y,by?,trap? or turret?` |
| RESPAWN_REDIRECTED | `player,spawn_x,spawn_y,x,y` |
| ROBOT_RESPAWNED | `player,x,y,heading` |
| TRAP_ARMED | `player,trap,x,y,was_armed,previous_owner,owner` |
| TRAP_TRIGGERED | `trap,x,y,owner,victims[]` |
| TRAP_HIT | `trap,player,damage,owner,points` |
| TURRET_CAPTURED | `player,turret,x,y,previous_owner,owner` |
| TURRET_CHARGING | `turret,x,y,charge,owner` |
| TURRET_FIRED | `turret,x,y,owner,player,dir,distance,damage,points` |
| TAR_SLOWED | `player,order,x,y` |
| SCORE_AWARDED | `player,points,reason,victim,score,trap? or turret?` |

`scores[]` records contain `player,score,deliveries,damage_taken`; winners preserve ties and
array order. Ranking is descending score, descending deliveries, ascending damage_taken.
No additional sabotage field is emitted in score rows. Hazard event owner values are integers
with 0 meaning neutral; snapshot owners instead use null. A neutral attributed disable has a
present `by=0`, while an ordinary laser/pit disable may omit `by`. SHUNT has `by` on disable and
no device key on SCORE_AWARDED. Preserve these differences in presence flags/tagged variants.

B1 numeric value tables cover these distinct vocabularies:

* Orders: `FORWARD_1,FORWARD_2,BACK_1,TURN_LEFT,TURN_RIGHT,TURN_AROUND,DROP,TOGGLE,WAIT`.
* Headings: `N,E,S,W`; phases: `PLANNING,EXECUTING,FINISHED`, plus server-only `LOBBY`.
* Movement cause: `ORDER,CONVEYOR`; disable cause: `PIT,LASER,TRAP,TURRET`.
* No-op reason: `NOT_CARRYING,TILE_OCCUPIED,NO_CONSOLE,TAR`; crate destruction: `DROP_BLOCKED,PIT`.
* Score reason: `TRAP_DAMAGE,TRAP_DISABLE,TURRET_DAMAGE,TURRET_DISABLE,SHUNT`.
* Match reason: `target,cap` (presence matters); bot personality: `hauler,opportunist,saboteur`.

Do not invent TICK_STARTED, machinery, laser-fired, tar-state or bot-goal events. Emit exactly the
engine's ordered list: orders, conveyors, continuous lasers, traps, turrets, disable handling,
then round-end effects on tick 3. Preserve multiple console events even when two toggles cancel.
Playback can animate from these events; it must not rerun game decisions or combine a hit and its
SCORE_AWARDED into two score increments. No secret future programmes are disclosed.

### 10.3 Snapshot, status and map content

A snapshot includes `protocol,match_id,rules_version,state_version`, board
`width,height,map_id`, `phase,round,tick,rounds_total,next_crate_id`, optional positive
`target_score`, `submitted_players[],ready_players[]`, nullable `planning_deadline_utc`, and:

| Collection | Exact public fields |
|---|---|
| players | `id,x,y,heading,damage,carrying,score,deliveries,damage_taken,respawn_pending,sabotage?,pushed_by?` |
| crates | `id,x,y,carrier_player_id` |
| groups | `id,reversed` (JSON group ID is a single A–Z letter) |
| traps (absent if none) | `id,armed,owner` |
| turrets (absent if none) | `id,charge,owner` |

Respawn-pending players have null x/y; empty carrying is null. Carried crates have null x/y and
a carrier; loose crates have null carrier. Owners are null when neutral. Sabotage appears for
all players when traps/turrets exist or anyone has sabotage; otherwise it is omitted. `pushed_by`
appears only while nonzero, including intermediate published ticks, and clears on disable and
round end. Charge is 0–2 (2 = READY), persists across rounds; no separate ready flag. Tar is
static map geometry. Snapshot alone restores the dynamic board given its matching static map;
it is not a checkpoint and cannot recover secret programmes, bot plans or H3 records.

Lobby status: `match_id,phase=LOBBY,joined,capacity,bots,rounds_total,target_score` (joined includes
bots). Running/finished status: `match_id,phase,round,rounds_total,target_score,tick,state_version,
player_count,submitted_players[],ready_players[],bots,bot_players[],bot_personalities[]`, plus
`winners[]` only when finished. Personality entries parallel bot player IDs. Neither status nor
snapshot exposes bot goals/reasoning. Snapshot does not carry bot labels, winners or a result
reason; status/events supply those. Finished snapshots still preserve the score tie-break data.

Map response: `protocol,map_id,width,height,rows[][]`. All 14 terrain kinds must round-trip:
Floor `.`, Wall `#`, Spawn `S<n>[heading]`, Dock `D<n>`, Dispenser `G<n>`, Conveyor `C<arrow>[group]`,
Console `T[group]`, LaserEmitter `L<arrow>`, Pit `O`, Trap `X<n>`, TarGenerator `A`, Turret `Y<n>`,
TurretPad `P<n>`, TrapConsole `K<n>`. In particular, `TA` is conveyor console group A; `A` is tar.

Nibble terrain alone is insufficient. Attribute records MUST retain player ownership, explicit
spawn heading, dispenser ID, conveyor direction and group, console group, laser direction, trap
ID and console linkage, turret ID and pad linkage. Belt console IDs `T001` etc. derive from
row-major console order; use the equivalent u16 ordinal without decimal parsing on the client.
Tar influence can be derived locally from generator positions without re-running movement rules.
Cache identity must include content revision or a server-instance-scoped immutable map handle:
textual `map_id` alone is unsafe across restarts. Name/description and player bounds come from
LIST_MAPS, not GET_MAP. Paginate list entries and long text without requiring full-list storage;
B1 fixes 65535-byte streamed text and transfer limits without changing JSON metadata behavior.

### 10.4 Widths and boundary cases

These semantic widths are finalized by the B1 wire tables, with u32 crate IDs replacing the old
u16 proposal and explicit presence bits and maxima. Never serialize a C/C++ struct by copying its padded memory.

| Value | B1 representation and reason |
|---|---|
| player, owner, hazard ID | u8; players 1–4, owner 0–4, trap/turret IDs 1–9 |
| group | u8 index 0–25 mapping exactly to A–Z |
| coordinates | i8 for event positions, including -1 off-board; board width <=20, height <=16. Snapshot null uses presence flags, not a coordinate collision |
| crate ID / next ID | u32, preserving both new-match and restored legal IDs; no recycling |
| dispenser ID / console ordinal | u16; parsed dispenser IDs can be 1–999, console count can exceed 255 |
| live crate count / tile and attribute counts | u16; board cells <=320, live crates conservatively <=324 |
| round / rounds_total / tick | u8 for valid state (rounds <=200, tick 0–3) |
| revision; submitted round/player; since_version | signed i32 in requests to preserve current rules-level rejection domain; do not turn a valid 32-bit negative value into a decoder range failure |
| state_version / sequence | u32 absolute, preserving current nonnegative int range; refuse exhaustion, never wrap; restored cursor/version permit up to 10^9 |
| score, sabotage, deliveries, damage_taken | u32 preserving current live and restored counters; target_score u16 (0–1000) |
| damage, turret charge, boolean / enums | u8 or explicitly documented single-byte masks |
| planning_seconds | u32 (86400 does not fit u16) |
| deadline | nullable signed i64 Unix seconds, lossless for server-produced UTC seconds; server does the text conversion. No decimal parse or 64-bit arithmetic required on retro client; optional numeric remaining-time hint must not replace the absolute deadline |
| request key | u32 plus explicit key-present flag, with frozen bounded ASCII compatibility mode under O5 |

A legal JSON revision can be INT32_MAX even in round 1. Requests without IDs can publish
arbitrarily many accepted revisions within the available integer range, so neither H3's budget
nor 200 rounds proves u16 versions sufficient. Binding supplies valid identity for the compact submission form; B1 provides an explicit-player form for the same signed numeric request domain; structurally invalid
JSON types/order counts are not tunneled through the fixed-slot binary request. Snapshot restore
limits differ from live request limits; do not use one as the other. Wire decoding must validate enum/length/count structure
before dispatch; a defined invalid-order value can map to the existing unknown-token refusal
without making arbitrary unknown opcodes legal. B1 assigns ff to this sentinel; all other unknown order bytes are malformed.

### 10.5 Pagination, consistency and recovery contract

* Capture a response's high-water version and immutable logical contents when a transfer begins.
  Event pages carry an identity for that transfer, the original since-version, page cursor,
  high-water version and final flag. Next cursor must distinguish multiple events of the same
  published version (for example an event ordinal within the captured result). Do not use only
  `since_version` as a within-version cursor.
* Preserve event context per record or per homogeneous batch. A version may span pages, and a page
  may contain several contexts. Commit the event watermark only when all pages through the
  advertised high-water version are consumed. An empty final page still advances the watermark.
  Retrying a page must return identical contents, and a client must detect and ignore duplicates.
* Pull one page per request. Snapshots, maps, lists, status and diagnostic text use the same
  bounded-transfer principle with typed records or offsets, never one giant client buffer.
  Largest atomic event/record must fit the negotiated payload including headers; otherwise
  negotiate refusal or a specified record-fragment format before transfer. No implicit truncation.
* Snapshot pages all describe the same version. A constrained client can invalidate its mirror,
  rebuild it in place from validated pages and mark it usable only after successful completion;
  it need not retain a second complete mirror. On bad/expired/missing pages discard partial state
  and restart. No gameplay action uses a partially restored mirror. Packet validation must precede
  callbacks/state mutation; staging a complete logical snapshot is not required.
* Preserve the existing 512-version event-retention policy. Missing history yields
  SNAPSHOT_REQUIRED. Bounded server-side transfer storage, expiry and outstanding-transfer count
  are fixed in wire.md §4: pinning a transfer must not pin unlimited history. Expired transfers explicitly
  restart or require snapshot; never splice newer data into an older transfer.
* Submissions publish readiness without events; deadline updates have no independent event and
  can change without a version bump. Query MATCH_STATUS for submitted/ready sets and bot labels,
  and SNAPSHOT for deadline/world recovery. Do not invent readiness events or claim events alone
  reproduce the full canonical snapshot. During initial sync: bind, fetch/cache map, restore one
  snapshot at V, then request events after V and refresh status. A racing status has its own version
  and must not overwrite a newer mirror with older state.
* The current host runs a whole round synchronously, so normal network snapshots are between
  rounds. Match tests can expose published executing/tick states; the codec must support them,
  including live `pushed_by`. Recovery at a publication boundary does not promise resuming an
  animation halfway through an individual event, or restoring private engine execution scratch.
* Reconnect to a surviving room uses the retained seat credential, never another JOIN. Rebind,
  retry any uncertain submission with the exact original key/payload, then catch up or snapshot.
  JOIN/CREATE have no H3 idempotency: do not blindly retry them after an ambiguous lost reply.
  B1 adapter-session recovery handles initial JOIN-response loss within its bounded lease;
  after expiry ambiguous lobby operations are not retried automatically.
  A restarted host has no room/session persistence; report that failure rather than replaying
  an old submission into a newly created match.

## 11. Bounded implementation sequence (B1–B3 accepted; B4 for review; B5 not started)

Each step ends with a reviewable commit and its focused tests. B4 stops before constrained-client acceptance in B5. No renderer, bot/engine rewrite, new firmware game service or platform-wide rollout is
part of these steps.

| Step | Deliverable / boundary | Required evidence before proceeding |
|---|---|---|
| **B1 — freeze representation (complete)** | Resolved the explicit O2/O3/O5/O6/O7/O9/O10/O11 gates; assigned stable numeric tables for the complete inventories; specified every layout, optional/null variant, message maximum, negotiation and transfer state machine. Derived conservative full-game resource bounds and defined K1/K4/K8 profiles with admission rules. See binary-v1 companions; measured source-map census is included. | Annotated byte examples for every type/variant; budget worksheet including attribute tables, hazards, counters, stack/scratch and NIO overhead; approved rules-profile decision; no codec yet. |
| **B2 — codec and server bridge** | Pure binary encoder/decoder and portable C99 bounded decoder/encoder; bridge to existing server semantic API. Add negotiation, binding and bounded page production outside Match/Engine. No socket dependency in codec. | Fixtures for all 30 events and nine operations, every code/variant; malformed/truncation/no-allocation checks; exact semantic round trips and JSON fixture/hash regressions. Include negative i32 requests, IDs >255, signed off-board targets and restored IDs above 65535. |
| **B3 — direct TCP vertical slice** | Separate binary listener sharing the JSON server; optional Linux client binary mode; all lobby, submit, map/status/snapshot/event operations. Preserve poll/deadline scheduling. | Real socket full matches with mixed JSON/binary seats and bots, target/cap/fixed endings, every hazard map; split/coalesced frames, partial writes, EOF, bounded slow-client queues; H3 cross-encoding retries and reconnect; pagination through busy versions and eventless readiness updates. |
| **B4 — FujiNet/NIO vertical slice** | C client transport adapter using existing NetworkDevice TCP Open/Read/Write/Info/Close and the B3 endpoint. Reuse the same codec and stream envelope; handle NIO resource/cursor lifecycle. | Same semantic scenario corpus over NIO, partial I/O, NotReady/DeviceBusy, lower-link duplicate-response recovery and disconnect; demonstrate real configured NIO path separately from mocks. Measure actual per-transfer buffer use; do not claim hardware acceptance from direct TCP alone. |
| **B5 — constrained-client acceptance** | Measure and tune representation only within approved layouts/profile; publish byte/RAM/CPU results. Retro renderers remain later work. | Allocation-failing C tests, worst-case supported-map admission tests, cc65/ca65 or soft65c02 fixture evidence as appropriate, measured 1 MHz decode costs, packet-budget assertions and full-match equivalence. Record any unattained historical target explicitly for review. |

Cross-encoding equivalence compares decoded public semantics with presence/null distinctions,
not identical transport bytes. Compare exact canonical state hashes using the same clock and
configuration; exercise intermediate ticks directly through Match tests, and normal rounds through
GameServer. Test map-list ordering, all legal terrain attributes and match metadata, per-round
request-budget exhaustion, replay after close, transfer expiry/retention loss and invalid profile
admission without altering an existing match. Use a full-hazard scenario such as `foundry-01`
in addition to baseline `training-01`; old random training-map maxima cannot bound current events.

## 12. Historical B1 documentation verification

Documentation only; no codec, game source, tests, firmware, library or workspace pins changed.
Reviewed the complete server dispatch, engine emit sites, snapshot/status serializers and map
parser against sections 7 and 10. A source-inventory check verifies that all dispatched request
names, emitted event names and literal server/submit codes occur in this document; Markdown local
links are checked for existence. Run `git diff --check` in `external/botched`. B1 additionally checks fixture byte counts, record lengths, request maxima, inventory coverage,
local links and resource arithmetic against the actual map files. Gameplay suites are
not a meaningful gate for prose-only changes and are not claimed as run. Implementation tests and
measurements above remain future acceptance gates, not results of this review.


### B1 verification record (2026-11-22)

After sourcing the workspace environment, the documentation audit passed for **167 literal byte
examples**: lengths, fixed record/event sizes, payload maxima, nested page record boundaries,
complete event/request inventory and local links. The map census was recomputed from all nine
`resources/maps/*.map` files; its formulas and profile results matched. `git diff --check` passed.
These checks inspect static documentation data; no encoder/decoder, server adapter or game test
was implemented or exercised. Runtime RAM, CPU and stack measurements remain B2/B5 evidence.

The review-visible budget result is explicit: under this conservative packed worksheet every
shipped map exceeds K1 but fits K4 (1484–1752 bytes). K8 covers the 4993-byte independent global
upper bound. No engine limit was reduced to satisfy the historical 1 KB aspiration. The confirmed
240-byte payload is retained, with 128 as the negotiated minimum and NIO raw I/O chunked below it.

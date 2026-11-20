# Botched! binary wire protocol — normative requirements for M1

**Status:** REQUIREMENTS (normative for M1). **Not an M0 task and not implemented.** M0 delivers the
JSON representation only.
**Authority:** `docs/botched-game-spec-v1.md` section 9 ("exact serialization may be compact binary or
JSON, but semantic types and fields are normative") and section 10 (client minimums).
**Rules version:** `botched/1`. The binary protocol is a second *representation* of the same
semantics; it must not alter gameplay.

**Keywords.** MUST, SHOULD and MAY are used as in the game spec. "Target" means a budget proposed here
that M1 must validate with measurements and may revise by an approved change to this document.

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
  semantic structs the engine already uses and back. **No gameplay logic in a codec.**
* JSON stays: it is the reference/debug representation, the format of golden test logs and the
  baseline for equivalence tests (R-T1).
* Transport (FujiBus/SLIP/packet link), serialization and rules remain independent layers.

## 2. Why this is a first-class requirement (measured JSON baseline)

Current JSON costs on the training map (`training01`, 4 players, measured 2026-11-20 from the M0 build):

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
Estimates for a compact binary form are in section 5; the expected reduction is 10-30x in bytes and
two to three orders of magnitude in CPU.

## 3. Facts about the existing FujiNet/NIO transport that this design must build on

(From `repos/fujinet-nio/docs/native-packet-contract.md`, `packet_link.md`, `host_transport_multiplexing.md`.)

* **FujiBus raw packet:** 6-byte header (`device` u8, `command` u8, total length u16 LE, folded
  checksum u8, first parameter descriptor), then parameters/payload; **little-endian**; status is U8
  parameter 0 in responses; canonical raw length 6-65 535 B, with platform adapters imposing smaller
  capacities.
* **Framing/integrity already exist:** SLIP framing for byte streams; the packet link adds sync, generation,
  length and **CRC-32**; the FujiBus header carries its own checksum. A game protocol that adds
  its own framing, checksum or escaping would duplicate them (R-X2).
* **Sessions/handles:** NIO multiplexes sessions with handles *above* the shared stream; request/response
  pairing is the transport's job (one outstanding exchange per link).
* Device IDs already assigned: FujiDevice `0xFB`, DiskService `0xFC`, FileService `0xFE` (new device
  assignment for a Botched service is an open choice, O1).

## 4. Requirements

### 4.1 Numbers, identifiers and parsing

* **R-N1** All numeric fields MUST be binary with explicit widths. No ASCII integers, no `atoi`, `strtol`
  or decimal formatting on a client. Fields MUST be 8-bit or 16-bit unless a value cannot fit.
* **R-N2** Multi-byte fields MUST have one documented byte order for the whole protocol. **Recommendation:
  little-endian**, matching FujiBus and the 6502 (low byte first, so 16-bit values need no swapping).
  Final choice: O4.
* **R-N3** Commands, orders, events, object kinds, terrain, headings, phases, outcome codes and error
  codes MUST be small numeric enumerations (one byte), defined in one normative table with stable values.
  The JSON string names (`FORWARD_2`, `ROBOT_MOVED`, `REQUEST_LIMIT`...) map 1:1 to those values.
  Numeric values MUST never be reused or renumbered within a major protocol version.
* **R-N4** Gameplay traffic MUST NOT repeat textual names, UUIDs or session strings. After the lobby phase
  entities are referenced by compact integers: player id (1 byte), crate id (1 byte; `botched/1` ids
  stay below 256 per match on the example map, bound to be confirmed, O7), group id (1 byte), map
  coordinates (x, y as one byte each, or one packed byte pair).
* **R-N5** Messages SHOULD consist of fixed-size, fixed-offset fields so a client decodes with byte loads and
  a table or jump on the opcode. Variable-length parts MUST carry an explicit count first.
* **R-N6** Bit-packing MAY be used selectively, only where it is plain shifts and masks on a single byte
  (for example heading in 2 bits plus flags, or two 4-bit order codes). It MUST NOT cross byte boundaries,
  use variable-length (varint) integer encodings, or need multiply/divide on the client.
* **R-N7** Every message MUST be decodable by a bounded, non-allocating single pass that never reads
  beyond the declared length, and malformed input (truncated, over-long, unknown opcode, out-of-range value)
  MUST be rejected, not partially applied. Equivalent of the H2 limits for JSON (R-S1).

### 4.2 Content of the protocol

* **R-C1** The protocol carries **semantic state and events only**: no graphics, sprite or character
  data, animation scripts, timing/animation instructions or screen layouts (spec section 1, 10).
* **R-C2** Normal play uses **compact semantic event updates**: one event is an opcode plus a few fixed
  bytes (for example `ROBOT_MOVED player,x,y`; `CRATE_DELIVERED player,crate`). A published version
  carries its events in engine order with the same `(round, tick, sequence)` and `state_version` identity as JSON.
* **R-C3** **Full binary snapshots** are provided for initial synchronisation, reconnect, and recovery
  after `SNAPSHOT_REQUIRED`. A client MUST be able to resynchronise from a snapshot alone, with no replay
  of earlier events (spec section 10).
* **R-C4** **Deltas** during normal play are the events themselves. A separate delta-snapshot format is
  not required unless measurement shows events are insufficient (O8).
* **R-C5** **Static map data** (terrain, spawns, docks, dispensers, conveyor directions, consoles,
  lasers) MUST be transferable once, identifiable by a compact map id/revision, and cacheable so reconnects
  do not resend it. Terrain SHOULD be coded in 4 bits per tile or one byte per tile (O6).
* **R-C6** Programme submission, readiness and the match/status queries of spec section 9.2 MUST each
  have a compact binary form with the same semantic fields (`round`, `revision`, 3 orders, `ready`,
  request correlation).
* **R-C7** Round, tick, sequence and version numbers MUST have a defined width and wrap policy.
  `state_version` is 32 bits in M0 (<= 10^9). A 16-bit wire form needs an explicit rule (relative to the
  client's last acknowledged version, O8) and a way to detect a gap.

### 4.3 Memory, size and processing

* **R-M1** **No dynamic allocation** is required to decode ordinary packets on a constrained client. A
  client MUST be implementable with static buffers and a fixed-size game-state mirror.
* **R-M2** **Explicit maximum packet sizes and bounded buffers.** Every message type has a stated maximum
  length; the protocol has a global maximum game payload (target: 240 B so that payload plus a 6-byte
  FujiBus header fits a 256-byte buffer, O3). Anything that could exceed it (events after a busy tick,
  map transfer) MUST be defined as chunked/continued, never as an oversize packet.
* **R-M3** The server MUST NOT send, and a client MUST NOT need to buffer, more than one maximum packet at
  a time for ordinary play (matches one outstanding FujiBus exchange).
* **R-M4** Budgets in section 5 are normative targets; M1 MUST report measured values against them.

### 4.4 Versioning and negotiation

* **R-V1** The protocol MUST be versioned and negotiated at session start with a capability exchange:
  protocol version(s), rules version (`botched/1`), supported encodings, the client's maximum packet size
  and optional features. The server MUST pick the highest common version, or refuse explicitly.
* **R-V2** Unknown opcodes and unknown trailing bytes in a known message: a receiver MUST be able to skip a
  message of unknown opcode (length-delimited by the transport) and SHOULD ignore trailing bytes it does not
  understand, so fields can be appended in later minor versions. Removing or reinterpreting a field needs
  a new major version.
* **R-V3** The encoding MUST be independent of the rules version: a new rules version bumps `botched/N`
  per the game spec's freeze rule; a new encoding version does not.

### 4.5 Layering with existing NIO transport

* **R-X1** The binary protocol is a **payload** carried by the existing FujiNet/NIO transport (FujiBus
  command/response, over SLIP or the packet link). It MUST NOT introduce its own sync bytes, framing,
  escaping or integrity check where the transport already provides them (R-X2).
* **R-X2** Do not duplicate: length framing, CRC/checksum, retransmission of lost transport frames and
  link-level generation/handshake are the transport's. The game protocol handles only game-level
  correlation and idempotency (R-X3).
* **R-X3** **Request correlation and idempotency** follow the approved H3 contract
  (`docs/request-id-contract.md`): `(player, request id)` single-use, replay, `REQUEST_ID_REUSED`,
  per-round budget, `REQUEST_LIMIT`. A compact binary **per-player request sequence number** (for example
  u16) MAY be offered instead of arbitrary ASCII ids, provided it maps into the same keyspace and
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
* **R-T6** **Size and budget tests:** assert the section 5 maxima per message type; fail the build if a fixture
  exceeds its budget.
* **R-T7** **No-allocation proof:** the reference decoder runs under an allocation-failing hook in tests.
* **R-T8** **Implementability:** a portable C reference decoder/encoder (C99, no libc allocation, no
  floating point, no `atoi`) ships with the fixtures and is tested in CI. The format MUST also be
  implementable in 6502 assembly (ca65), verifiable with the workspace `soft65c02` tester against the
  same fixtures (M2 retro ports).

## 5. Proposed budgets for 8-bit clients (targets to validate)

Estimates, derived from the current semantic model; not measurements. M1 replaces them with measured numbers
via R-M4 and R-T6.

### 5.1 Sizes (bytes of game payload; FujiBus header and transport framing are extra)

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

### 5.2 Client RAM (protocol and state mirror only, excludes UI and OS)

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

These keep one tick well inside a video frame so a client can decode while animating at its own speed
(spec section 10: clients must never block the server).

## 6. Relationship to M0 artefacts

* **JSON stays normative for reference tooling:** `botched-sim`, `botched-server`, golden JSON logs,
  `docs/protocol-limits.md`. Binary limits (R-N7, R-M2) mirror the H2 limits and must be documented
  alongside them.
* **Semantic structs are the contract between codecs and engine:** `Event`, `GameState`, programme
  records, `SubmitResult` codes. The binary event table is generated from the same enumeration as the JSON
  type names to prevent drift.
* **Idempotency** is H3. **Checkpoint/recovery** (M1-02) is separate and unaffected by encoding.
* **No M0 code change** is needed for this document; M1 adds a codec layer beside the JSON one.

## 7. Open design choices for M1

Each needs a decision, with a recommendation where there is one.

| # | Choice | Options | Recommendation / notes |
|---|---|---|---|
| **O1** | Framing and device mapping | New FujiBus device (one command per game op); or a stream device carrying length-prefixed game messages | New FujiBus device, one command per game operation and one `parameter`-based payload per FujiBus convention; reuses header, checksum and handle plumbing; no extra framing (R-X1). |
| **O2** | Session identification | Token at join then connection-bound handle; per-message token; per-message slot byte with transport-level authentication | Credential once at join, then a 1-byte slot/handle bound to the transport session (R-X4). Per-message tokens rejected on size. |
| **O3** | Maximum packet | 128 / 240 / 256 / 512 B | 240 B game payload (fits a 256 B buffer with header); negotiated down by capability exchange. |
| **O4** | Byte order | little-endian / big-endian | Little-endian (FujiBus, 6502 native). |
| **O5** | Request correlation | ASCII id as in JSON; u16 per-player sequence; u8 sequence + epoch | u16 per-player sequence mapped into the H3 `(player, id)` keyspace (single-use, replay, reuse, budget). Needs a rule for sequence exhaustion within a match and for coexistence with JSON ids in one match. |
| **O6** | Map terrain encoding | 4-bit tiles (2 per byte) / 1 byte per tile / run-length | 4-bit tiles, row-major, plus a small table for owners/groups/directions; RLE rejected (client CPU). |
| **O7** | Entity id width | u8 / u16 | u8 for players, groups; crate ids need a bound proof or a recycling rule (a 99-round match with 4 dispensers spawns fewer than 400 crates, so u8 may overflow: decide u16 vs per-round renumbering). |
| **O8** | Event encoding and version width | Fixed opcode+fields; batch per version with a count; delta snapshots; 16-bit relative versions | Fixed opcode+fields, batched per published version with a header `(version, round, tick, count)`; 16-bit relative version with explicit gap detection; no delta snapshots unless measured necessary. |
| **O9** | Oversize results (events, snapshots, maps) | continuation flag / paging / force snapshot | Continuation by sequence ranges for events; chunked map; snapshots fit one packet at supported board sizes or are chunked. |
| **O10** | Capability negotiation content | version list only / feature bitmap / max-packet / board-size limits | Version + rules version + feature bitmap + client max packet + max board dimensions. |
| **O11** | Error and outcome code table | reuse JSON strings by number / merge transport errors | One numbered table for game outcomes and server errors; transport errors remain the transport's. |
| **O12** | Lobby operations in binary | binary / JSON-only on POSIX | Binary (join, start, status) so a retro client can play end to end; admin operations (`CREATE_MATCH`) may stay JSON-only. |
| **O13** | Coexistence | one match with mixed JSON and binary clients | MUST work (R-T1); the codec choice is per connection, not per match. |

## 8. M1 acceptance criteria for this requirement

1. A written protocol specification with the opcode and code tables (R-N3), message layouts, maxima
   (R-M2), negotiation (R-V1) and the resolved decisions O1-O13.
2. A binary codec beside the JSON codec, sharing the semantic structs, with no engine changes.
3. Golden fixtures, round-trip, equivalence, malformed-input, size and no-allocation tests (R-T2..T7) passing,
   including a full simulated 4-player match driven through the binary adapter with the same final
   state hash as JSON.
4. A C reference implementation and measured RAM/size/cycle figures against section 5.
5. A statement of how the binary protocol maps onto FujiBus (device, commands, parameters) reviewed against
   `repos/fujinet-nio/docs/protocol_reference.md`.

## 9. Non-goals

Implementing any of this in M0; defining retro platform renderers or input (M2); changing `botched/1` gameplay;
encryption; replacing FujiBus, SLIP or the packet link.

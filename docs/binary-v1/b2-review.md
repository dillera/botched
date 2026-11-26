# B2 codec and semantic bridge review

B2 implements the accepted B1 bytes (`c6d25ef4`), with no changes to `wire.md`,
`fixtures.json` or `examples.md`. Binary remains v1.0 and rules remain `botched/1`.
B3 has not started: there is no binary listener, socket framing, client transport,
NIO integration or firmware change. Engine, Match and existing JSON clients are unchanged.

## Implementation boundary

- `include/botched/wire/v1.h` and `wire/v1.c`: standalone C99 bounded scalar readers/writers,
  transactional packet/record encoders, exhaustive structural record validation, page visitors
  and a stateful logical response validator. Caller-owned buffers; no heap, recursion or I/O.
  Views borrow the input. A visitor runs only after the entire packet passes validation.
- `server/binary_codec.*`: typed request conversion and all public response/event conversions,
  including exact field presence, nulls and JSON ordering. The host response decoder reconstructs
  JSON for reference/equivalence testing; it is not the constrained-client memory API.
- `server/binary_bridge.*`: complete payloads in/out, negotiation, binding, session recovery,
  exchange caching, profile admission and bounded immutable record streams. Hosts must serialize
  calls and poll GameServer independently. The shared GameServer supplies read-only map/seat seams
  and tracks numeric-key hints and retained event-version boundaries in room metadata.

Constrained consumers call `bw_stream_accept` before applying records, then `bw_visit_page` only
when `apply` is true. Map occupancy, terrain-dependent attribute checks and game-world consistency
belong to the admitted client mirror; the codec checks bytes, counts, order, map-handle consistency
and stream grammar. No renderer or full C world mirror is introduced by B2.

## Recovery invariants

An exact retry of the last exchange returns the saved response bytes without polling or dispatching
again. Changed bytes under the same exchange fail; skipped IDs and wrap fail. A resumed session
invalidates its previous connection, retains its binding/transfer and reports its last exchange.
The tests exhaust all 65535 IDs and verify resume at the boundary.

Each successful logical response owns its serialized bytes until replacement/cancellation/expiry.
FETCH accepts only previously returned canonical page starts or the next page start, never arbitrary
record offsets. It does not serialize current game state. Failed binding and adapter faults retain
the previous transfer. Capacity checks reserve mutation result storage before dispatch; global
accounting conservatively includes the previous transfer while constructing its replacement.

Only a complete, valid EVENTS transfer changes the acknowledged event watermark, using
`max(previous, H)`. Partial windows, malformed pages, duplicate/old pages, snapshots, status and
submissions never lower it. Empty event windows still advance through eventless versions. A future
`since` returning a lower current H does not lower acknowledgement. On a **new adapter session**,
transfer IDs restart; initialize the stream with the existing acknowledgement for the same
server epoch/match. Only a different epoch/match resets that acknowledgement to zero.

Retention tracking records actually trimmed event-bearing versions, not `current_version - 512`.
Tests advance 520 eventless revisions, compare the exact JSON retention boundary, and verify that
an already captured transfer remains usable after its source events disappear.

## Measured packets

The reproducible `binary: measured packet budgets and session-local cursor reset` test uses
training-01, four seats (three server bots), default planning deadline, immediately after joining.
Sizes exclude the future two-byte TCP prefix and requests/FETCHes. Aggregate payload bytes include
all PAGE headers. These are scenario measurements, not worst-case bounds; B1's conservative
bounds remain normative.

| Result | P128 pages / total bytes / largest | P240 pages / total bytes / largest |
|---|---:|---:|
| All nine map descriptions | 19 / 1868 / 125 | 9 / 1648 / 229 |
| GET_MAP | 3 / 293 / 127 | 2 / 271 / 235 |
| GET_SNAPSHOT | 3 / 284 / 127 | 1 / 240 / 240 |
| GET_EVENTS since 0 | 2 / 157 / 121 | 1 / 135 / 135 |
| GET_MATCH_STATUS | 1 / 53 / 53 | 1 / 53 / 53 |

The standalone C encoder reproduces the literal 21-byte bound numeric SUBMIT (23 with TCP prefix).
Golden tests reproduce HELLO30, HELLO_OK48, FETCH11 and all other fixture packet lengths.
The largest fixture record is 101 bytes. A 240-byte payload limit is enforced, including P128 tests.

## Resource evidence and deviations from B1 estimates

On x86-64 GCC 16.2.1, `sizeof(bw_stream)=72`, reader32 and writer32. These are ABI measurements,
not packed 6502 sizes. The C object has only `memmove` and compiler stack-protection imports;
allocation-wrapped standalone C tests abort on malloc/calloc/realloc/free while codec operations run.

`cc -std=c99 -pedantic-errors -Wall -Wextra -Werror -Wvla -O2 -fstack-usage -I include
-c wire/v1.c` reports these static **individual frame sizes**, excluding callees and caller buffers:

| Function | Host stack bytes |
|---|---:|
| bw_validate_record | 8 |
| internal record_body | 96 |
| bw_validate_packet | 144 |
| bw_encode_record | 64 |
| bw_encode_packet | 288 |
| bw_visit_page | 80 |
| bw_stream_accept | 208 |

The transactional packet encoder uses a 240-byte local scratch buffer; the stream validator copies
its state before commit. Thus B1's 32-byte scratch and 32-byte bookkeeping aspirations are **not
achieved by this host reference implementation**, and the 100-byte reserve is not sufficient to
claim the worksheet as an actual port footprint. K1/K4/K8 admission still uses the frozen packed
map worksheet (all shipped maps require K4). This is server-side admission against a declared
profile, not a claim that this host ABI implements a complete K4 client. B5 must measure total
stack/caller/mirror/library storage on the actual compiler and refuse any profile it cannot meet.
No 6502 cycle or hardware result is claimed.

Server transfer storage is limited to 4 MiB per session / 64 MiB aggregate (vector capacities counted),
64 sessions and 1800-second idle expiry. On this host, a session object is 672 bytes
(including its two 240-byte exchange buffers), Binding is 80 bytes and each optional numeric
high-water entry is 8 bytes; dynamically allocated strings/storage are additional. Each mutation reserves 4096 bytes before game dispatch.
The old and replacement buffers both count during construction, so near-capacity replacement may
refuse conservatively. Capped map-list serialization visits map metadata instead of constructing
an unbounded JSON list. Limits do not make arbitrary allocation failures inside existing GameServer
transactional; no new rollback guarantee is claimed.

Two implementation differences from B1's metadata estimates are explicit:

- Numeric hints observe authenticated canonical `b:N` submissions in GameServer instead of
  inspecting Match's private H3 cache. They include REQUEST_LIMIT refusals, so a hint can skip an
  unused numeric value; it never proposes reusing an observed value. H3 responses are unchanged.
- To keep Match unchanged, GameServer retains up to 512 event-version integers per room plus a
  trim boundary/deque overhead. This additional bounded server metadata was absent from B1's
  worksheet. `optional<u32>` and C++ strings/deques also have ABI/allocator overhead; the packed
  five-byte-per-seat estimate is not a C++ object-size claim.

Signed i64 deadline bytes are preserved by C. Host JSON reconstruction rejects seconds outside
its existing signed-i64-milliseconds clock domain before multiplying, avoiding overflow; every
server-produced deadline remains representable. No layout changes or semantic narrowing of
engine-produced data were needed.

## Verification

After sourcing the workspace environment:

- CMake build and complete CTest regression suite pass (existing engine/hash, server, JSON client,
  map/hazard, UI and TCP tests, plus B2 tests and standalone C99 executable).
- 14 focused B2 cases / 76289 assertions pass under the normal build and ASan/UBSan with leak
  detection and halt-on-undefined-behaviour. Standalone allocation-wrapped C tests also pass there.
- All 167 frozen examples are checked for their stated validity, byte identity and truncations;
  all 30 event types round-trip and all nine operation types are covered. The documented
  non-greedy PAGE example is rejected because its entire remainder fits even P128; no bytes were edited.
- Semantic equality and canonical match hashes agree across complete three-round games on all
  nine maps at P128, including bots. Boundary fixtures cover negative i32 requests, signed
  off-board targets, IDs above 255, restored crate IDs above 65535, optional variants and codes.
- Seeded malformed-input mutation tests check no partial visitor application; targeted tests cover
  H3 cross-encoding keys/refusals, request budgets, immutable snapshots/events, profile refusal,
  resource preflight, cancellation, session expiry, rebinding and watermark monotonicity.
- Strict standalone C99 compilation and `git diff --check` pass.

Real socket/mixed-client scheduling belongs to B3; NIO to B4; constrained-target total RAM and
cycle acceptance to B5. Stop here for review.

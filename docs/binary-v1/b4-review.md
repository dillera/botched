# B4 portable FujiNet/NIO transport review

B4 builds on accepted B3 (`cb6edff7`) without changing the frozen wire layouts,
codec, bridge, engine, JSON service, Linux UI or NIO firmware/library. B5 has not
started. This adds a reusable C transport and an executable acceptance harness;
it does not add an Amiga renderer or a Linux `--wire nio` option.

## Integration contract

`include/botched/client/nio.h` and `client/nio.c` provide a C99, allocation-free,
cooperative adapter. `client/nio_fujinet.c` binds its service callbacks to existing
`fn_open`, `fn_info`, `fn_write`, `fn_read` and `fn_close`. The caller owns
`fn_init`/`fn_shutdown`, the platform channel, the client object and the URL.
The library/channel is shared, so serialize its calls; this is not a new
thread-safe NIO library. The transport core itself has no platform dependency.

Typical foreground integration:

1. Initialize the platform library/channel and call `bn_init(&client,
   &bn_fujinet_ops, NULL, "tcp://host:7879", 240, 2, now_ms, timeout_ms)`.
   Profile 2 is K4; use the profile the eventual client can actually support.
   The URL must remain valid until disposal. Do not reinitialize a live object.
2. Call `bn_step(&client, now_ms)` until `BN_READY`, yielding to the application's
   other work on `BN_WAIT`. Supply a wrapping monotonic millisecond clock.
3. Encode a B2 request with `client.next_exchange`, then call `bn_request`.
   The adapter retains an exact private copy; the caller can reuse its buffer.
   Continue stepping until `BN_RESPONSE` or `BN_FAILED`.
4. On response, the validated payload is `client.reply + 2`, `client.reply_size`
   bytes. For PAGE, `bw_stream_accept` has already validated the whole page.
   Visit records only when `client.apply` is true. Encode FETCH for the canonical
   next page until the logical response is complete; no whole-transfer buffer
   is needed. The response view expires when the next exchange starts.
5. On failure inspect `client.error`; `bn_reconnect` explicitly resumes an
   established session, including an uncertain pending request. Session/protocol
   failures cannot be silently retried. Call `bn_dispose` before discarding the
   object. Failed Close retains the handle for another disposal attempt.

Open uses method 0, `FN_OPEN_STREAM_NO_PROBE`, no translation and no eviction.
Each step issues at most one service call. Timeouts apply between calls; existing
`fn_write` can perform internal retries and block inside one call, so the adapter
is not a hard real-time preemption mechanism. The URL limit is 224 printable
bytes, keeping Open within the raw 256-byte packet budget.

## Stream and recovery rules

- Separate 32-bit read/write cursors start at zero for each handle. Successful
  operations advance by actual accepted counts. Every Read/Write is at most 32
  bytes, and frame lengths remain the frozen two-byte prefix plus payload.
  Partial frames are assembled without reading into a following frame.
- NotReady/DeviceBusy mean wait only when zero progress is established. The
  concrete Write binding inspects existing library diagnostics because an error
  with `written == 0` can hide earlier internal partial writes.
- A lost Read reply retries exactly the same cursor **and requested size**, at
  most twice, enabling NetworkDevice's duplicate-read cache. Ambiguous Write
  never repeats bytes on the same stream: close, reopen, resume HELLO with the
  retained secret/epoch, then replay the identical game exchange. Recovery is
  bounded to two attempts within the current operation deadline.
- Unknown Open completion fails rather than risking another resource allocation.
  EOF, exhausted read retries and cursor overflow recover through a new handle.
  Failed Close stops recovery rather than leaking a known handle and opening more.
- Resume verifies epoch, secret, payload/profile and server exchange position.
  Expired recovery reports uncertainty; it does not create another room/seat.
  Reconnect preserves immutable transfer progress and the event acknowledgement.
  B2's `max(previous, H)` rule applies even to eventless and stale/future windows.
- Exchange IDs never wrap. After 65535 the caller must explicitly establish a
  new adapter session and BIND its known seat; preserve the event acknowledgement
  with `bw_stream_init` for the same epoch/match. A different epoch/match needs a
  fresh stream identity. This transport does not own room selection or persistence.

Two details were verified against the existing service rather than inferred
from similarly named library constants: Info flags describe metadata, not socket
connected state; successful Info permits trying I/O, whose NotReady/EOF results
are authoritative. Read TRUNCATED means the requested chunk was filled, not lost
stream data. The public `fn_read` API does not expose raw offsetEcho; packet
validation is delegated to the existing library, while this adapter owns cursor
progress and replay identity. No stronger independent offsetEcho check is claimed.

## Acceptance evidence (2026-11-23)

`tests/test_nio.cpp` drives the production adapter through a scripted service
backed by the actual B2 bridge. It verifies short writes/reads, explicit busy
responses, cursor progress/overflow, lost read/write completion, immutable
pagination, request-copy ownership, monotonic acknowledgement, wrapping timeouts,
malformed counts, URL bounds, failed Close retry and expired ambiguous JOIN
without a second seat. `tests/nio_binding_test.c` checks the concrete library
status/diagnostics mapping, including NotReady/DeviceBusy and ambiguous writes.
`tests/nio_c_test.c` runs strict C99 with allocation wrappers that fail on any
malloc/calloc/realloc/free during adapter operation.

The separate real test is **not a mock NetworkDevice**:

```text
production C adapter -> existing Linux fujinet-nio-lib -> private serial PTY
 -> SLIP/TCP relay -> existing POSIX FujiNet-NIO NetworkDevice TCP
 -> actual B3 binary listener -> shared GameServer <- direct binary / JSON peers
```

`tests/nio_accept.py` starts the existing POSIX NIO executable with a temporary
configuration and a private loopback port. The relay fragments channel replies
into seven-byte writes and drops one successful Read reply and one committed
JOIN Write reply. The actual service replays the identical Read; game-session
resume recovers JOIN without allocating another seat. No firmware patch or
physical retro device is involved. This is real configured POSIX NIO acceptance,
not physical-hardware acceptance.

`tests/nio_real.cpp` compares public map lists, maps, snapshots, status and events
with direct TCP/JSON. It completes matches on all nine shipped maps with an NIO
seat, direct binary seat and server bot; checks fixed and cap endings, H3 retries
in both encodings and after round close, immutable paging during concurrent
mutation, reconnect after submission and acknowledgement, and nonregressing
future event cursors. A target fixture verifies target completion and forty
eventless versions. Another match completes through two planning deadlines while
the NIO client is disconnected, then resumes the same seat.

Final real run (P128, K8): **616 pages, 50,026 response payload bytes, largest
payload 127 bytes**. Largest requested NIO Read/Write: **32/32 bytes**; largest
observed raw channel response: **51 bytes**. Observed **31 NotReady** responses,
one lost Read reply followed by an identical duplicate Read, and one lost Write
reply followed by session recovery. DeviceBusy was not observed naturally;
DeviceBusy and partial service counts are covered by the scripted/binding tests.
These are scenario measurements, not worst-case packet bounds. P240 remains
supported and the existing B2 P128/P240 golden/budget tests remain unchanged.

NIO executable used: `build/fujibus-tcp-debug/fujinet-nio` (POSIX + FujiBus TCP
serial), SHA256 `d3b9b5ee7766a5d4131c72a0c1fbaaec825d1f953f0c0f315472d114b10b3e4c`.
The runner prints the executable hash on each run for reproducibility.

Verification passed:

- Full configured CTest: **6/6 targets**, including all **325 C++ cases**, real
  NIO, standalone codec/adapter C99, concrete binding and existing SDL/CLI tests.
- ASan/UBSan with leak detection: **29 B2/B3/B4 cases, 79,433 assertions**, plus
  both standalone NIO C targets and the real NIO acceptance harness, including
  the disconnected deadline scenario. No sanitizer claim is made for the
  external NIO executable or its prebuilt library.
- Strict C99 `-pedantic-errors -Werror -Wvla` compilation and no-allocation checks
  passed; no compiler warnings in the normal build. Frozen layouts/fixtures,
  engine, JSON clients and NIO source remain unchanged.

## Resource measurements and B5 carry-forward

GCC 16.2.1, x86-64 ABI: **`sizeof(bn_client) = 560` bytes** including the retained
96-byte framed request, 256-byte reply buffer, 32-byte framed HELLO, 32 bytes of
secret/epoch, 72-byte B2 stream validator and control/pointer/alignment storage.
There is no heap allocation in either production C adapter source and no
additional per-page scratch buffer. Caller URL, request encoder, visitor/world
mirror, channel and library storage are additional costs.

At `-O2 -fstack-usage`, individual host frames (excluding callees and caller
buffers): `bn_init` 48, `bn_request` 48, `bn_dispose` 32, `bn_reconnect` 8,
`bn_step` 144 bytes (compiler reports dynamic, bounded outgoing-call storage).
Binding frames: Open/Close 16 each, Info/Read 32 each, Write 64. These are not
whole-call-chain stack bounds or 6502 measurements.

The linked Linux NIO archive also owns 1024-byte request and response globals,
64-byte session storage, 48-byte parse context, 24-byte transport context and a
40-byte stream session, besides further channel/library state. Thus B1's 64-byte
NIO scratch allowance is **not an actual footprint for this library build**,
even though each adapter I/O request is capped at 32 bytes. B2's documented
240-byte transactional encoder scratch and 208-byte stream-validator frame
remain unchanged. B5 must measure total stack, library/channel, caller and world
storage on each intended compiler before claiming K1/K4/K8 memory acceptance.
No speculative codec refactor, retro cross-build or 1 MHz CPU claim is included.
There are no B1 wire-layout divergences.

## Reproduce

From this repository, with the workspace's existing NIO executable and Linux
archive available (build the archive with `make linux` in `fujinet-nio-lib` if
needed):

```sh
source "$NIO_WORKSPACE/scripts/env.sh"
cmake -S . -B build -DBOTCHED_SDL=ON \
  -DBOTCHED_NIO_INCLUDE="$NIO_WORKSPACE/repos/fujinet-nio-lib/include" \
  -DBOTCHED_NIO_LIBRARY="$NIO_WORKSPACE/repos/fujinet-nio-lib/build/fujinet-nio-linux.a" \
  -DBOTCHED_NIO_EXECUTABLE="$NIO_WORKSPACE/repos/fujinet-nio/build/fujibus-tcp-debug/fujinet-nio"
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Without the optional include/archive settings, the portable adapter and scripted
C/C++ tests still build. Without `BOTCHED_NIO_EXECUTABLE`, CTest does **not** claim
real NIO acceptance. A separately configured hardware channel can run
`botched_nio_real` directly, but its `tcp://127.0.0.1` endpoints require the service
and test listeners to share the host network namespace; the provided harness is
intentionally a POSIX localhost test.

Stop at B4 for review. B5 and platform renderers remain separate milestones.

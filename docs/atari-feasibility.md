# Atari thin-client feasibility spike

**Verdict: conditional go for a playable Atari client using the compact stream
API.** The measured headless prototype occupies **18,201 bytes including resident
code and all reserved working storage**, compared with B5's 36,635-byte factory
configuration. Its **2,934-byte complete working allocation fits K4** for all nine
shipped maps. The factory maximum snapshot takes **289,490 CPU cycles**, versus
B5's 5,497,188. These results make a character/tile-based client plausible on an
unbanked 64 KB Atari. They do not establish a finished renderer, smooth physical
SIO operation, or the original 150 cycles/event target.

This follows B5 (`4a06013a`). The production codec, server, JSON clients, engine,
frozen binary v1 layouts and NIO firmware/library are unchanged. Prototype code
is isolated in [`prototypes/atari`](../prototypes/atari/); the optional CMake test
is off by default. [Recorded measurements](atari-feasibility-measurements.json)
include every tested event fixture, compiler version and linked-library hash.

## What is reused

The Atari executable links **the existing**
`repos/fujinet-nio-lib/build/fujinet-nio-atari-compact.lib`, specifically its
`fn_stream.o` and `fn_stream_sio.o` assembly members, through `include/fn_stream.h`.
This is the small TCP path used by Syn-Ack-Wire, not a replacement TCP/SIO driver.
Those members contribute **875 code bytes, 17 BSS bytes and 4 zero-page bytes**.
The required caller-owned stream context adds **262 bytes**, including separate
120-byte request and receive areas. No `fn_open/read/write`, general request/reply
buffers, or allocator is linked. Library revision and archive hash are recorded.

New work is the Botched frame adapter, specialized request builders, bounded
record decoder and display-state mirror. Two new assembly helpers (76 bytes
combined) implement little-endian u32 loads and seven-byte-entry crate-ID scans.
The latter replaces cc65's expensive per-entry `memcmp` calls; no auxiliary index
is allocated. The same simulated image has 15,413 CODE bytes with C helpers and
15,247 with assembly. C and assembly produce identical complete-mirror checksums
for every benchmark. The transport assembly itself is unchanged.

## Wire and mirror design

The client negotiates the existing **P240/K4** profile and all six existing
features. A two-byte TCP length prefix is assembled incrementally. One response
payload lives in `th.rx[240]`; the largest retained outgoing request allocation
is 96 bytes (maximum supported BIND is 92 bytes including framing). Numeric
SUBMIT remains exactly 21 payload bytes / 23 framed bytes. Each stream operation
moves at most 120 bytes. There is no JSON conversion, generic encoder, second
page, whole event batch, dynamic allocation or duplicate snapshot.

`th_accept` checks the page envelope and scans record lengths/domains once.
`th_next` then consumes one already-checked record at a time, updates the mirror,
and returns a borrowed view for narration/animation. It does not reconstruct or
validate the record a second time. Copies retained are the unavoidable stream
receive-to-page copy and persistent world fields. CPU benchmarks also include
the fixture-to-page copy. Callers can pause between events; the page must remain
intact until consumed. A record view expires at the next request.

The fixed 1,291-byte world has nibble terrain for 320 cells, 31 five-byte map
attributes, four full player records, **100 seven-byte crate entries with full
u32 IDs**, 26 group states, 18 hazard states, snapshot/status/map metadata and
validity flags. Counters and the eight deadline bytes are retained at wire width.
All 30 event kinds are delivered. Movement, carrying, scoring, damage, crates,
groups and hazards update presentation state; this is not a local rules engine.
A fresh authoritative snapshot after each round supplies fields not inferred
from animation events, including deadline and other engine metadata.

Map capacity is checked **before JOIN**, using explicit GET_MAP for the known
match ID. Dimensions, terrain values, tile coverage, sorted attributes and their
domains are checked; `walkable + 4 <= 100` proves room for all possible crates,
including carried crates. Attribute count must be at most 31. All shipped maps
pass. Larger otherwise-legal maps can fail this local admission even if the
server's frozen K4 worksheet permits them. The prototype does not advertise
universal K4 map support or silently truncate a world.

Memory safety checks retain length, count, coordinate, player/device index,
capacity, offset and transfer bounds. Full generic grammar validation, redundant
semantic consistency checks, UTF-8 reconstruction and game legality are omitted.
Unknown record/event types fail closed. This is a specialized decoder for the
existing authoritative server, **not a replacement for B2's general validator**.
A malformed input can invalidate a partially updated mirror; no rollback copy
exists. Callers must stop rendering it and recover, not continue consuming it.
Renderers must use checked world dimensions for tile access and clip animation
coordinates; event coordinates are checked against the supported 20x16 envelope.

## Session and recovery subset

| Retained | Simplified or omitted |
|---|---|
| HELLO, epoch, seat credential, BIND, numeric request keys | No retained game-session resume secret or historical reply cache |
| One correlated exchange and immutable paged transfer at a time | No concurrent requests, CANCEL, unsolicited server frames or response queue |
| Sequential FETCH with stable ID, total, origin, offset and high watermark | No materialization of the logical response |
| Complete event consumption before advancing acknowledged watermark | No acknowledgement inferred from a snapshot |
| Bound MAP, SNAPSHOT, EVENTS, STATUS, ready three-order SUBMIT | No CREATE/catalogue UI, drafts, general-purpose requests or spectators |
| Known-disconnect close/new HELLO/BIND/map/snapshot | No automatic replay of ambiguous game mutations on a new TCP session |

Acknowledged event watermark is monotonic within an epoch. Snapshot high water
sets a **separate query cursor** so events already reflected in a resync snapshot
are not animated again; it never reduces or invents an acknowledgement. An
EVENTS response below the current watermark cannot regress it. Eventless versions
and same-epoch rebind retain this distinction. Epoch change invalidates old state.

At the NIO layer, partial writes advance only by `actual`; partial reads preserve
prefix/payload progress. Busy/NotReady are validated zero-progress responses and
use the next library sequence. Carrier error `0x80` repeats the **same operation,
context, size and bytes**, allowing the existing library/firmware replay cache to
recover a lost reply. After two additional adapter attempts (the library also
retries internally), uncertainty stops the adapter; Close/Open cannot overwrite
the outstanding operation. A known EOF or protocol failure permits close/reopen.
The deterministic tests exercise EOF followed by new HELLO/BIND and fresh state.

Uncertain JOIN/SUBMIT completion after a lost TCP session is reported, not retried
blindly. A later playable UI must offer explicit reconciliation/resync. There is
no automatic exchange rollover; the builder refuses exchange 65535. A fresh
session is needed before then. This preserves semantics at the cost of less
transparent recovery than the Linux client. The prototype joins a supplied match
and submits supplied three-order arrays; it does not implement menus or input.

## Complete resident memory

Measured cc65 V2.19, Git `37d4bb24`, `-O`, actual Atari archive, no stubs in the
Atari executable. Numbers include the headless driver, bookkeeping and stack
measurement helper; these are conservative application costs, not deductions
based on a hypothetical final rewrite.

| Working allocation | Bytes |
|---|---:|
| Compact world mirror | 1,291 |
| Client, including rx240, tx96, IDs, credential and session fields | 478 |
| NIO stream context, including both 120-byte buffers | 262 |
| Adapter state | 9 |
| Library BSS | 17 |
| Guest, instrumentation and cc65 runtime BSS | 38 |
| All DATA / all zero page (library and runtime included) | 41 / 30 |
| Reserved software stack / full hardware stack page | 512 / 256 |
| **Complete working RAM** | **2,934** |
| CODE / startup / ONCE / read-only constants | 15,070 / 99 / 12 / 86 |
| **Total resident application allocation** | **18,201** |

BSS is 2,095 bytes. The loaded image starts at `$2000` and BSS ends at `$63FA`;
stack reservation is separate. The loader's temporary SYSCHK overlaps later code
and is not counted twice. File headers and unused address-space gaps are not live
allocations. The real Atari run measured 42 software-stack bytes and 34 hardware
bytes (other runs observed 36); **reservations remain 512 + 256**. Interrupts and
future graphics/sound handlers need their own integration measurement. No claim
that 42 bytes is a safe configured stack follows from this run.

Relative to B5 factory: working RAM falls from 5,674 to 2,934 bytes; code/constants
from 30,961 to 15,267; total from 36,635 to 18,201. Part of the working improvement
is a deliberately measured 512-byte software stack instead of B5's unmeasured
2,048-byte general-NIO reservation. The mirror is actually larger than B5's
factory-specific mirror: it reserves one uniform capacity for all shipped maps.
These are different implementations with different supported session features,
not a claim that the B5 accounting was wrong. K1 remains unsupported.

### Full-client planning envelope

Use **48 KiB unbanked below `$C000` with OS ROM enabled**, not all 64 KiB as a heap.
The following is an explicit future allocation plan, not measured renderer RAM:

| Region or function | Budget |
|---|---:|
| Low memory / OS workspace / hardware reservations | 8 KiB |
| Measured networking, decoder, mirror and stacks, rounded up | 18 KiB |
| Display lists, screen/character data, sprites and graphics assets | 12 KiB |
| Renderer, input, menu and presentation code/state | 6 KiB |
| Sound code/data | 1 KiB |
| Animation queue/state | 1 KiB |
| Unassigned integration margin | 2 KiB |
| **Total** | **48 KiB** |

The low-memory reservation conservatively overlaps zero page/hardware stack
already included in the measured core. Display storage must be explicitly
relocated/budgeted when the renderer replaces the OS text screen. This supports
a tile/character display with dirty updates and a small event animation queue;
full-screen bitmap double buffering plus large uncompressed assets is not
promised. The core has only 231 bytes of slack inside its rounded 18 KiB slot;
further features must use the explicit integration margin or earn their space.

## CPU and link measurements

NMOS 6502 `sim65`, no wait states, raw bracketed cycles including loop/copy
bookkeeping. Seeds, printing and stack painting/scanning are outside timing.
The baseline column recompiles B2's three validation passes with a no-op record
visitor: it is a **lower bound without world updates**, not a substituted B5
full-client benchmark. The published B5 full maximum remains 5,497,188 cycles.

| Case | Pages / payload bytes | C helpers | Assembly helpers | Reference validation only |
|---|---:|---:|---:|---:|
| training initial snapshot | 1 / 240 | 44,181 | 40,803 | 834,031 |
| factory initial snapshot | 2 / 265 | 56,483 | 48,378 | 940,330 |
| foundry initial snapshot | 2 / 280 | 64,411 | 56,221 | 1,057,737 |
| training busy captured round | 4 / 819 | 191,371 | 166,103 | 3,377,001 |
| foundry busy captured round | 4 / 814 | 185,449 | 163,792 | 3,410,209 |
| factory maximum, 100 crates | 6 / 1,217 | 314,723 | 289,490 | 5,372,129 |
| Synthetic 96 reverse-ID crate moves | 10 / 2,251 | 2,255,738 | 607,794 | 8,867,668 |

Add two framing bytes per page to payload totals. Snapshot costs exclude the
separate immutable map transfer. The synthetic case forces long scans through a
full crate mirror; it is an adversarial work fixture, not an engine-produced
round or a proof of the largest possible legal round. Arbitrarily long retained
histories remain paged, with total CPU cost proportional to records consumed.
No bound on an entire history's response time is claimed.

Assembly saves about **3.7x** on that crate workload and **19x** versus B5's full
maximum snapshot path. Numeric SUBMIT takes **4,163 cycles** versus B5's 21,768.
Single-event pages take 8,365–12,083 cycles across the frozen event examples;
those include page overhead and are not comparable to a raw event-only target.
Historical 150/event, 3,000/snapshot and 400/submission targets still fail.

At nominal 1.77 MHz, initial snapshots cost about 23–32 ms CPU, the busy batches
about 93–94 ms, the full snapshot 164 ms and the synthetic moves 343 ms. Display
DMA and interrupts add costs. A caller can yield after each record, keeping
animation responsive without buffering a whole batch; a page's validation scan
still runs synchronously. A future renderer should measure its worst frame with
network traffic rather than assuming these CPU-only figures are frame times.

The headless **64 KB PAL Atari emulator** ran the real assembly through NetSIO,
the existing POSIX FujiNet-NIO NetworkDevice TCP service and the actual Botched
binary TCP listener: foundry, four seats/three bots, three rounds, FINISHED,
25 responses and 90 event records. No mocked stream API on this path. Guest
RTCLOK marks were `[13,132,256,368,500,500,504]`: initial open/handshake/map/join/
snapshot 119 PAL ticks (2.38 s), subsequent rounds 124/112/132 ticks
(2.48/2.24/2.64 s), close four ticks. Host wall time is not a physical link metric:
the emulator runs faster than real time. These results prove integration with
the existing service; they are **not physical Atari/Wi-Fi acceptance**.

The existing stream driver owns POKEY channels 3/4 and preserves channels 1/2 and
VBI/DLI enable. Call it from one foreground owner, never from an interrupt.
Its COMPLETE wait can reach 960 VBI ticks per attempt (about 19.2 s PAL), and
retries can multiply that. `st_step` is cooperative *between* library calls;
it cannot make an individual SIO call nonblocking. VBI/DLI may continue, but a
foreground menu cannot process input during that wait. This is a material
playability limitation under link failure. Resolve scheduling/error UX and test
physical unplug/timeout behavior before promising responsive disconnects; no
firmware or driver timeout changes are part of this spike.

## Verification and reproduction

Run from this repository after sourcing the workspace environment. Existing
cc65 tools, compact Atari library, NetSIO POSIX firmware build and the workspace
Altirra/atari800 test harness are prerequisites. The real run reserves UDP ports
9996/9997 and refuses to displace an existing service. Results/maps/binaries stay
under ignored `build/atari-spike`.

```sh
source "$NIO_WORKSPACE/scripts/env.sh"
cmake -S . -B build -DBOTCHED_ATARI_SPIKE=ON \
  -DBOTCHED_NIO_INCLUDE="$NIO_WORKSPACE/repos/fujinet-nio-lib/include"
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 tests/atari-spike/measure.py
python3 tests/atari-spike/real.py
python3 tests/atari-spike/report.py
make -C "$NIO_WORKSPACE/repos/fujinet-nio-lib" test-atari-stream
```

The host test completes 54 rounds across nine maps, comparing **every event's
presentation state** with the existing Linux playback model and comparing fresh
snapshots after rounds. It injects seven-byte writes, 31-byte reads,
Busy/NotReady, lost replies with exact replay, and EOF/rebind during each match.
Separate checks cover refusal to replace an exhausted uncertain operation,
max-length BIND, no exchange wrap, immutable paging,
intermediate/future watermark behavior, snapshot byte mutations, random frames
and random records within valid envelopes. Frozen examples cover all 30 event
IDs; C/assembly checksums agree. Existing stream soft65c02 tests cover ten API
and five driver cases; their DSL documentation lives in
`repos/fn-rom/unit-tests/soft65c02-docs`.

Sanitizer verification of the host prototype:

```sh
cmake -S . -B build/asan-spike -DBOTCHED_ATARI_SPIKE=ON -DBOTCHED_SDL=OFF \
  -DBOTCHED_NIO_INCLUDE="$NIO_WORKSPACE/repos/fujinet-nio-lib/include" \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build/asan-spike -j4 --target botched_atari_spike
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  build/asan-spike/botched_atari_spike build/asan-spike/corpus
```

`report.py` rejects allocator or general-NIO symbols in the actual Atari link.
All production core data is static and all variable storage is bounded. The host
server/equivalence harness does allocate; its heap is not attributed to Atari.

Verification completed: all eight configured CTest tests passed, including the
existing JSON/binary/NIO regressions and B5 resource test; the sanitizer run,
6502 corpus/checksum benchmarks, real NetSIO match and all 15 existing stream
assembly tests passed. No physical Atari or renderer acceptance is claimed.

## Next review boundary

Accept or revise the measured profile and explicit recovery limitations first.
Then a separate client milestone can add a tile renderer, bounded narration/
animation, three-order input and known-match join UI within the budget; integrate
VBI/sound around the foreground SIO owner; measure complete stacks and DMA costs;
and test physical hardware, planning deadlines and unplug recovery. No Amiga or
Atari graphical client is implemented or started here.

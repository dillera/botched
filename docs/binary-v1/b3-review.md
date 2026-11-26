# B3 direct binary TCP and Linux client review

B3 builds on accepted B2 `5c36415f`. The frozen wire layouts, fixtures, C99 codec,
semantic bridge, engine and JSON listener implementation are unchanged. No NIO,
Amiga or other retro-client work is included. B4 has not started.

## Human playtesting

```sh
./build/botched-tcp-server --binary-port 7879
./build/botched-client --wire binary --ui --create --players 3 --bots 1 --map-id foundry-01
./build/botched-client --wire json --ui --join m1
```

Use the created match ID. Omit `--ui` for terminal play; `--bot` works in either mode.
`--wire json` is the unchanged default. Client ports default to 7878 for JSON and 7879
for binary; an explicit `--port` wins regardless of argument order. The server binary
listener is opt-in, uses the same `--bind` address, and accepts `--binary-port 0` for an
ephemeral port. JSON and binary connect to distinct listeners on one GameServer.

SDL and terminal both select the encoding through `client::Connection`. Board rendering,
order entry, bots, gameplay, narration and playback are shared. The network layer converts
binary pages back to the existing public semantic objects. It drains event windows through
the latest snapshot's version before handing events to playback, including eventless windows.
No alternate UI, gameplay implementation or renderer was introduced.

## Transport and recovery

`server/binary_tcp_host.*` is a separate nonblocking POSIX listener. Its decoder retains
one two-byte little-endian length prefix and one payload; it accepts arbitrary splits and
coalesced frames. Lengths below 3 or above the negotiated P close the connection. Bounded
malformed payloads go through the unchanged B2 bridge and return its cached FAULTs.
Connections get monotonically allocated IDs rather than file-descriptor identities, so fd
reuse cannot accidentally recover another adapter session.

The executable services JSON and binary on the same thread with 10 ms polls for each;
there is no concurrent access to GameServer. Binary passes poll deadlines before and after
I/O and expire adapter sessions even with no requests. With binary disabled, the original
JSON host loop is used. Limits bound I/O work; they do not promise a wall-clock deadline
latency independent of existing engine/bot computation or OS scheduling.

The Linux binary connection uses nonblocking connect/send/receive with poll and a per-logical
request deadline (10 seconds by default; DNS resolution uses the system synchronous resolver).
It retains exact pending exchange bytes. On socket loss it reconnects once, resumes the same
secret and verifies epoch/last-exchange before replaying. Partial replies are discarded on
reconnect; saved bridge responses and transfers supply identical bytes. The first attempt
uses half the remaining exchange time, leaving time for recovery within the request deadline.
A failed/expired resume stops with uncertainty; it never silently makes a fresh CREATE/JOIN.

Each complete response is checked by B2's stream validator before its reconstructed JSON is
returned. Event acknowledgement commits only complete windows and never decreases. A new
adapter session at exchange exhaustion preserves acknowledgement, then rebinds the saved seat.
If exhaustion occurs inside an exceptionally long transfer, the client reports that the read
must be restarted in a new session instead of wrapping IDs or acknowledging a partial result.
This is a safe failure boundary, not a wire-layout change. Shipped maps and tested histories
fit well below this boundary. Recovery secrets/credentials are memory-only: process restart
and server restart recovery remain outside B3.

The reusable binary connection also exposes authenticated BIND for an existing JSON seat.
The CLI still joins a seat normally; no credential persistence or manual rejoin UI is added.

## Bounds and B5 carry-forward

Default binary host bounds:

| Resource | Bound |
|---|---:|
| Live TCP connections | 32 |
| Input storage per connection | 242 bytes |
| Queued output per connection | 4096 bytes |
| Read / write work per connection per pass | 4096 bytes each |
| Accept work per pass | 32 attempts |
| Incomplete prefix/body lifetime | 10 seconds from its first byte |
| Aggregate application output at connection cap | 131072 bytes |
| Aggregate fixed input at connection cap | 7744 bytes |

Output capacity is reserved when accepting a connection. The host checks room for a maximal
242-byte response before dispatching another request; a slow/flooding peer is closed before
exceeding the queue bound. Partial sends retain a cursor; EAGAIN returns to poll. Kernel socket
buffers, C++ object/allocator overhead and the unchanged B2 session/transfer storage are
additional. Normal clients have one outstanding exchange; accepting coalesced input is not a
promise to recover arbitrary pipelined game mutations.

Linux response reconstruction uses the B2 4 MiB logical stream bound and heap-backed public
JSON. The UI connection caps aggregated event JSON text at 8 MiB, matching the existing host
client response-size policy; C++ object overhead is additional. This is not a constrained
client footprint. TCP adds exactly two bytes per payload: numeric SUBMIT21 becomes23,
HELLO30 becomes32, HELLO_OK48 becomes50 and PAGE240 becomes242. B2 scenario measurements
and all frozen payload maxima remain unchanged.

**B5 still owns the documented C99 stack-budget deviations.** No codec refactoring for
speculative memory savings was done. No cc65/6502 RAM or cycle acceptance, NIO transport,
FujiNet firmware change or retro-platform claim is made here.

## Acceptance evidence

All tests use loopback TCP sockets, not calls substituted for network I/O. Deterministic raw
socket tests pump the host on one thread; mixed-client tests run both listeners on a host
thread and access game state through sockets. The deadline test deliberately sets up its
match before pumping and inspects the resulting state without competing host threads.

`tests/test_binary_tcp.cpp` covers:

- Prefix/body byte splits, coalesced frames, one-byte output passes, malformed lengths,
  complete bounded BAD_MESSAGE exchanges, EOF before a mutation completes, partial-frame
  timeout, queue bounds and connection admission limits.
- Lost CREATE/JOIN/FETCH replay, old connection invalidation, changed-ID refusal, and immutable
  snapshot continuation after a concurrent JSON draft changes the live version.
- Real binary P128 and JSON seats plus server bots through completion on all nine maps;
  map/snapshot/status equality, exact event ordering across single-version windows, H3 replay
  both ways (including after round close), conflicting keys and nonregressing acknowledgement.
- Fixed-round, score-cap and score-target endings. A target test crosses forty eventless
  versions before delivery and compares the UI connection's complete event response with JSON.
- A real fault proxy drops committed CREATE/JOIN/SUBMIT/FETCH responses and fragments returned
  frames, exercising the client's automatic reconnect path. Lease expiry after a dropped CREATE
  reports uncertainty; the JSON service confirms only one room was created.
- Planning continues to completion with disconnected, nonreading and partial-frame peers;
  authenticated rebinding and explicit lobby START preserve existing seats.
- Existing terminal gameplay finishes with one binary and one JSON auto-playing client.

`tests/binary_tcp_cli.py` additionally launches the actual server and client executables:
unchanged/default JSON, explicit JSON and binary map lists; binary SDL plus JSON terminal plus
server bot through a complete foundry-01 match, with a BMP screenshot from the SDL renderer.
It runs with SDL's dummy video driver and is registered in CTest when SDL and Python are available.
This proves the SDL code path and shared playback are exercised, not a physical-display usability
assessment. No SDL feature test is claimed in builds where SDL is unavailable.

Verification commands (after sourcing `scripts/env.sh` from the workspace):

```sh
cmake -S . -B build -DBOTCHED_SDL=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
cmake -S . -B build/asan-b3 -DBOTCHED_SDL=ON -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build/asan-b3 -j4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ./build/asan-b3/botched_tests --test-case='B3:*,binary:*'
git diff --check
```

Verification passed on 2026-11-23 with GCC 16.2.1, SDL2 2.32.74 and SDL2_ttf 2.24.0:

- Complete CTest: **3/3 targets**, including all 320 C++ cases, standalone C99 allocation checks
  and the SDL/CLI integration test (54.43 seconds total).
- ASan/UBSan with leak detection: **24/24 B2+B3 cases, 77433 assertions**. The ten B3 cases
  contribute 1144 assertions; the accepted B2 tests remain intact.
- Builds emitted no compiler warnings; `git diff --check` passed. A diff against accepted B2
  confirms unchanged engine, C99 codec, bridge, binary layouts and literal fixtures.

Stop at the B3 commit for review; do not begin B4.

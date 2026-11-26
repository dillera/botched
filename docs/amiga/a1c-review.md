# A1c — Amiga reliability and mixed-client acceptance

A1c keeps the accepted A1b gameplay loop and the stock A500/68000, Kickstart
1.3, 512 KB Chip + 512 KB slow expansion configuration. The new work is bounded
session recovery, cooperative worker shutdown and reliability acceptance. There
are no engine, wire, NIO firmware/library, Linux client or Atari changes.

## Behaviour and ownership

`session.c/.h` extracts the existing worker's portable request/transfer logic
behind clock, pause and cancellation hooks. The production Amiga worker and
native failure tests use **the same implementation**. The Exec process still
exclusively owns the session, library and command port. Foreground publication,
page-at-a-time playback and the three fixed world mirrors are retained.

B4 continues to perform its existing transient link recovery. A1c additionally
allows **one** recovery after a 30-second request timeout: resume the existing
server session, then send its retained exchange bytes. It never reruns a logical
JOIN or allocates a replacement H3 submission key. The timeout budget is at most
two 30-second windows **between returning service calls**, not an unconditional
60-second wall-clock deadline. B4's lower-level recovery attempts remain bounded.

A LOAD attempt is latched, including failed attempts. Repeating LOAD is refused
(code 208). Session/protocol failures are terminal; requests cannot allocate a
new H3 key after a failed session. Expired session recovery displays **“Session
expired: outcome uncertain”** and stops. There is no silent fresh HELLO/JOIN,
seat takeover, persistent credential file or automatic fresh-session BIND.
Within the existing lease, HELLO resume restores the same binding and exchange
history. After expiry, restarting this join-only application is not a supported
way to reclaim the old seat; the operator must resolve the uncertain outcome.

## Shutdown and broker limits

Escape now requests cooperative cancellation rather than blocking the foreground
in `WaitPort`. The worker observes cancellation before its next `bn_step`, stops
fetching pages, replies to the outstanding job, and receives a separate STOP job.
The foreground continues draining input and waiting for display frames. After
500 frames it explains that the broker is still busy and exit is not yet safe.
It does not free live buffers, kill the worker or unload code while I/O may own it.

The previous unchecked `bn_dispose()` is replaced by up to **three** checked
attempts. Failed attempts retain the handle; a later attempt may succeed. If all
fail, the worker still shuts down its local library channel after the calls
return, and the application reports an unconfirmed remote release and exits with
status 10. It does not claim the handle was released or silently clear it.
Successful Close and pending-job cancellation are recorded in acceptance logs.
`CLOSE confirmed=1` means the existing library/B4 API returned success, not a
separate enumeration of the remote handle table.

Investigation of the unchanged workspace implementation:

* `repos/fujinet-nio-lib/src/platform/amiga/fn_transport.c` creates a local
  message port and request for each exchange and calls synchronous **DoIO**.
  Its private request is not exposed to this application for safe AbortIO.
  `fn_transport_close()` closes the OpenDevice binding without an erroneous
  WaitIO on that OpenDevice-only request.
* `src/common/fn_info_close.c` calls the transport for Close and frees its local
  handle entry even when the transport returns failure. Retrying the retained
  numeric remote handle is still possible; `fn_close` does not require that
  local entry. Its returned transport result is the available success contract.
* `src/common/fn_shutdown.c` closes the local transport and clears local session
  storage. It does **not** prove that a remote NetworkDevice handle was freed.
  An unconfirmed handle may remain until remote cleanup/reset; repeated failed
  closes can exhaust the service's finite handle pool. The application does not
  evict another application's handle.

The existing broker contract requires bounded backend completion, but a wedged
broker, its FIFO queue, or a synchronous library call can exceed the application's
time budget. **Hard bounded shutdown independent of broker health is not met.**
A responsive waiting UI with retained ownership is the safe application-level
behaviour under the unchanged library constraint. There is no fabricated timeout
that returns while another process can still execute freed code. A real paused
NIO process is tested below; a permanently wedged broker remains unverified.

## Reconciliation and native failure coverage

`tests/amiga/reliability.cpp` combines the real Amiga session, B4 transport, frozen
codec, binary bridge and authoritative server with a deterministic NetworkDevice
service double. It is not an emulator or physical transport test. It checks:

* Lost completion after JOIN and SUBMIT, with both ambiguous writes and a
  request-timeout resume: retry bytes, exchange ID and H3 key are identical;
  JOIN returns the original seat and SUBMIT allocates only one key.
* Session lease expiry after an uncertain JOIN or SUBMIT: terminal error,
  retained pending request, no fresh JOIN and no new submission key.
* Partial reads/writes, recurring NotReady/Busy and exact offset/size replay of
  a lost read response.
* A permanently NotReady service reaches the two-window timeout instead of
  spinning forever. Cancellation interrupts a pending request between calls.
* Failed Close twice followed by success, and failure on all three attempts
  retaining the handle and reporting failure.
* A real eventless ready submission while other human seats are waiting:
  the empty EVENTS transfer advances the consumed version and ACK.
* Immutable multi-page EVENTS while the server's next planning deadline fires;
  the captured transfer drains in order. A newer snapshot is held until its
  event gap has been consumed. Duplicate pages are ignored.
* 150 rounds without sufficient history consumption, exceeding the server's
  retained event-version window: error 15, replacement snapshot baseline,
  unchanged ACK during resynchronisation, then an eventless catch-up transfer.

The existing nine-map, 27-round, 803-event presentation equivalence tests still
pass. Focused B4 tests separately exercise malformed counts, cursor overflow and
terminal protocol errors. All Amiga native tests pass under ASan/UBSan.

## Real A500/NIO acceptance

Run `python3 tools/amiga/accept.py --a1c`. It shares A1b's disposable licensed
KS1.3/Workbench media, wb13 broker, serial bridge and actual NIO NetworkDevice
TCP firmware. The guest binary is the normal application, with `--capture` logs;
there is no gameplay or fault-injection backdoor in it. Human seats are driven
by Amiberry raw keys and the **unchanged Linux SDL clients' scripted programme
option**, not server bots or the Linux `--bot` option. This is automated
human-seat acceptance, not a claim that three people manually playtested it.

**Passed:** five launches on one boot, four completed mixed-client matches,
20 completed rounds and **262 ordered events** (34 / 40 / 148 / 40).
The fifth match is deliberately interrupted for shutdown testing.

The scenarios exercise:

1. Training-01 target-score match, three human seats: Amiga, Linux binary and
   Linux JSON. Amiga submits FWD2/FWD2/AROUND then FWD2/FWD1/WAIT, delivers a crate
   and wins at score 5 before the ten-round cap. A TCP proxy drops the complete
   JOIN reply and first SUBMIT reply **after server execution**, closes the
   connection, and verifies byte-identical resumed requests. Exactly two
   programmes are accepted.
2. Foundry-01, three fixed rounds with eight-second deadlines, the same three
   client types. Linux seats submit WAIT programmes; Amiga misses the deadlines.
3. Foundry-01, twelve rounds with five-second deadlines: the server advances
   while the A500 is replaying. Every event type is compared in order from the
   guest's initial snapshot baseline through completion.
4. A live match with a pending request: SIGSTOP the actual NIO host process,
   press refresh, then Escape. The application must remain loaded while blocked.
   Resume NIO, allow cancellation/Close to settle and return to Shell safely.
5. Another three-round deadline match and clean exit, verifying that later
   launches still work after the stall and recovery cases.

For every completed match, final player positions and scores match the server.
Planning-closure events distinguish all-ready completion from defaulting only
Amiga's missing programme. ACK samples never regress. The harness records all
five post-exit memory readings, full guest logs, server event arrays, machine
settings and executable/library/broker hashes.

This is **real emulator plus NIO integration**, with host-injected failures.
No physical Amiga, physical serial cable or FujiNet hardware was tested. Lease
expiry, persistent Close failure and retained-history eviction use deterministic
native injection, not a 30-minute emulator wait or modified server firmware.
The longer test is finite acceptance, not an overnight soak or proof against
every possible failure schedule.

## Reproduce

From Botched, with the existing workspace tools and licensed media available:

```sh
source "$NIO_WORKSPACE/scripts/env.sh"
make -f tools/amiga/Makefile
cmake -S . -B build
cmake --build build -j4 --target botched-tcp-server botched-client botched_tests botched_amiga_model_test botched_amiga_gameplay_test botched_amiga_reliability_test
ctest --test-dir build -R 'botched_amiga|botched_wire_c_test|botched_nio_c_test|botched_nio_binding_test' --output-on-failure
build/botched_tests --test-case='B4*'
python3 tools/amiga/accept.py --a1c
```

For sanitizer validation, configure a separate build with
`-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and the
same CXX flags, build the three Amiga test targets, and run `ctest -R
botched_amiga` with `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`.

## Resource results and review boundary

The [measurement record](a1c-measurements.json) identifies the exact final build
and contains all scenario samples. A1a/A1b historical measurements are retained.
The display remains 24,000 Chip bytes and the full reserved stacks remain
16,384 bytes foreground + 8,192 worker. Resident hunks include all world mirrors,
page copies, caller state, runtime and linked NIO buffers; dynamic OS/broker and
library costs are reflected in the guest's available-memory readings.

| Measurement (bytes) | A1b | A1c |
|---|---:|---:|
| Executable file | 54,564 | **55,568** |
| Text / DATA / BSS | 44,324 / 312 / 17,292 | 45,324 / 312 / 17,364 |
| Resident hunks | 61,928 | **63,000** |
| Reserved UI / worker stacks | 16,384 / 8,192 | 16,384 / 8,192 |
| Conservative measured UI / worker stack | 1,852 / 1,876 | **1,856 / 1,940** |
| Lowest sampled total free RAM | 739,736 | **738,720** |
| Chip / slow expansion free at that sample | 415,184 / 324,552 | 415,184 / 323,536 |
| Largest Chip / slow expansion block | 414,944 / 324,040 | 414,944 / 322,536 |
| Total free after exit | 851,312 (two launches) | **851,312 (all five launches)** |

The explicit hunks + reserved stacks + bitplanes subtotal is **111,576 bytes**.
The live cost relative to the broker-loaded baseline is **120,568 bytes**;
OS/runtime allocations account for the difference. First-use service retention
remains **7,976 bytes**. All five post-exit available-memory totals and largest
blocks are identical, including the launch after the NIO stall. No accumulating
per-launch loss was observed. These are sampled values with capture/logging;
startup/interrupt/transient allocator maxima are not exhaustively measured.
The inherited stack probes include conservative guard space; reservations were
not reduced based on their high-water readings.

The final emulator run is `build/amiga/evidence/a500-tih8tulf`; the ignored
`build/amiga/latest-a1c-evidence` pointer names it. The added planning-missing and
actual KS1.3/slow-memory log assertions were also checked against that run's
saved evidence. Ordinary Close took 4–7 foreground wait frames; paused-NIO Close
waited **138 frames**, with the foreground still servicing its loop. This is an
observation of that injected pause, not a broker-independent shutdown bound.

A1c is complete for review with the limits above. No later milestone has begun.
No additional hardware or higher-memory fallback was used. Failed remote Close
cannot guarantee remote reclamation, and permanently blocked broker calls cannot
be safely force-terminated from this client. Those limits are explicit rather
than hidden by claiming unconditional cleanup or a hard wall-clock bound.

# A2b slice 2 — authoritative replay explanations

Review gate after `594941ca`. Retains the accepted 320×256 artwork/layout,
empty-slot editor, mouse controls, viewport and persistent hazard guides. No
network/session, engine, protocol, library, firmware or Atari changes.

## Presentation

The rotating priority strip outlines the current order's numbered player; an
underline records completed entries and a strike records skipped disabled orders.
**M** is separately highlighted for machinery, including turret capture. The
sidebar identifies the player/order or machinery phase and the next seat. It
explicitly says the server clock continues while replay is displayed.

Every consumed event produces an owned two-line actor/action/result caption.
All 30 frozen event types are covered: coordinates and facing for motion/turns,
pusher and victim, cargo IDs and delivery score, conveyor movement/control,
trap ownership/trigger/damage, laser source/damage, turret capture/charge/fire,
tar restrictions, disable/respawn, explicit SHUNT/sabotage points and round edges.
Damage text uses the event's accumulated damage and the existing three-point
health rule. Scores are never inferred from movement or proximity.

`H` opens the last **16 captions**, four at a time. Up scrolls older, Down newer;
H/Escape/right-click returns. Replay and network service continue. Scrolling an
older entry keeps that position as new entries arrive until it is evicted. Each
entry owns two bounded 52-character strings and round/tick/type context; there
are no borrowed page pointers or second event queue. History is local to one
launch. A history-expiry snapshot rebase appends **STATE REFRESHED**, clears
execution highlighting and does not invent the missing actions. A local failure
status takes precedence over captions, keeping stopped/uncertain sessions visible.

Binary-v1 limitation: ROBOT_PUSHED contains pusher/victim but no ORDER/BELT cause.
A push outside an already identified machinery phase therefore shows **PUSH
SOURCE / PHASE UNRESOLVED**, with the known source player, until a cause-bearing
record identifies the phase. It does not guess from the previous actor or peek
forward across immutable pages. Empty machinery phases have no event to animate.
Completion markers stay conservative where the phase is unresolved. This is
presentation of the frozen protocol, not a new field or inferred outcome.

## Motion and effects

Following the pacing addendum, robot movement has **12 intervals**, pushes **16**;
crate belt motion retains eight. The detail renderer interpolates only the before
and after positions of the current authoritative event, keeping facing intact for
pushes. The terminal position gets its own draw before the next event is consumed.
This does not assert that previous movement never animated: the old renderer did
interpolate, but its terminal draw could be replaced by the next event's first
frame. The new sampling makes travel more deliberate and pushes easier to follow.

Routine administrative events use one interval; turns/pickup/drop/laser hit/tar
use three; delivery, disable, burst, turret shot and scoring use five. Significant
outcomes get emphasis without extending every event. A laser/turret hit flashes
its authoritative source-to-victim lane, a triggered trap flashes its footprint,
and blocked movement outlines the obstruction. Persistent hazard guides remain
unchanged. Overview remains schematic; smooth pixel motion is in detail view.

Unchanged objects are now repainted only when they overlap dirty cells. Caption
changes do not invalidate the entire header/sidebar unless their displayed
context changed. No extra bitmap, asset set or dynamic event allocation is added.
The accepted 40,960-byte terrain cache remains; the earlier Chip-budget deviation
is not hidden or reversed here.

## Verification and measurement method

After sourcing workspace `scripts/env.sh`:

```sh
make -f tools/amiga/Makefile
cmake --build build -j4 --target botched_amiga_narration_test botched_amiga_view_test botched_amiga_model_test botched_amiga_gameplay_test botched_amiga_reliability_test
ctest --test-dir build -R botched_amiga --output-on-failure
# Existing build/a2b-sanitize uses -fsanitize=address,undefined on C and C++.
cmake --build build/a2b-sanitize -j4 --target botched_amiga_narration_test botched_amiga_view_test botched_amiga_model_test botched_amiga_gameplay_test botched_amiga_reliability_test
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build/a2b-sanitize -R botched_amiga --output-on-failure
python tools/amiga/accept.py --a2b2
python tools/amiga/replay_metrics.py "$(cat build/amiga/latest-evidence)"
```

Native coverage includes 44 golden event variants (all 30 types), exact source/
victim/durability/score text, malformed record rejection, bounded owned history,
scroll bounds, machinery context, signed pixel interpolation/endpoints and
snapshot-refresh semantics. Nine-map gameplay compares the event-applied mirror
against authoritative JSON after every record and checks that animation leaves
it unchanged. Existing cursor, duplicate-page, eventless-version, submission and
recovery suites remain in the run.

The real-NIO harness compares every emitted event and animation completion to
server events, exactly once and in order; checks the renderer’s interpolation samples for each movement/push have
contiguous frame numbers, exact endpoints and no pixel step above two; exercises H during
replay; completes Foundry, deadline-only Factory and triggered-trap matches; and
adds an Amiga/JSON two-human-seat Shuntworks match ending with a real push into a
pit and an authoritative +8 SHUNT. All nine maps are launched on stock 68000,
OCS, KS1.3, 512 KB Chip + 512 KB slow expansion, without true Fast RAM.

`FRAME render_fields` measures `ba_draw` elapsed fields, including preemption.
`interval_fields` measures successive completed draws, separately including loop
waits, input and capture logging. `ANIM fields` covers first-frame setup through
endpoint drawing. `ROUND fields` covers PLANNING_CLOSED through ROUND_ENDED,
including page fetches and overlay use, but excluding prior planning time. PAL
resolution is 20 ms. These are capture-enabled emulator elapsed timings, not
cycle counts or claims of uninstrumented physical-A500 performance.

## Results

**PASS:** `build/amiga/evidence/a500-j2uctsmt`, using the exact executable hash in
[measurements](a2b-slice2-measurements.json). Five native suites passed in 6.75 s;
all five passed with ASan/UBSan and leak detection in 29.95 s. The 68000/nix13
cross-build passed without warnings. No physical-Amiga result is claimed.

Real Amiberry/NIO acceptance completed **four matches, ten rounds, 257 events and
1,429 captured frames**, with exactly ordered event/caption/animation completions.
All nine map launches exited cleanly. [Replay evidence](a2b-slice2/replay.json)
contains every ordered caption and completion; measurements include aggregate
frame distributions and the first movement plus both pushes' pixel samples.
The live SHUNT screenshot was captured by an additional IPC watcher during this
run; the same capture wait is now included in `accept.py --a2b2`.

| Completed match | Events | Complete-round playback, seconds |
| --- | ---: | --- |
| Foundry, three submitted programmes | 86 | 22.12 / 14.76 / 12.90 |
| Factory, deadline-only WAIT | 56 | 22.12 / 15.62 |
| Trapworks, trigger and disarm | 67 | 24.86 / 21.00 |
| Shuntworks, Amiga + JSON human seats | 48 | 12.34 / 7.58 / 7.70 |

The first Foundry round includes opening/scrolling history. These wall times
include NIO pagination. Factory's eight-second server deadlines can expire during
replay; the client catches up without dropping or reordering events. Deliberate
motion therefore does consume planning time in short-deadline games. This slice
does not pause the server, skip playback or change acknowledgement semantics.

| Timing sample | Movement (73 events) | Push (2 events) |
| --- | ---: | ---: |
| Moving-frame draw cost, median / p95 | 40 / 60 ms | 40 / 60 ms |
| Completed-frame interval, median / p95 | 80 / 100 ms | 80 / 100 ms |
| Initial frame draw, median | 60 ms | 240 ms |
| Whole animation, median | 1,040 ms | 1,510 ms |
| Whole animation, range | 920–1,700 ms | 1,460–1,560 ms |

The accepted correction's Foundry movement sample was 640–920 ms, median 800 ms
(18 events). Its older timer stopped before drawing the terminal frame, so this
is a useful pacing reference, not an identical-instrumentation speed ratio. The
new movement is deliberately slower; routine events remain short. Rendering is
still not 50 fps. History/panel redraws and viewport changes produce outliers:
one moving-frame draw reached 460 ms, interval 500 ms. Cold/full drawing peaked
at 1,260 ms, unchanged from the correction. Foundry averaged 90 ms/draw over 553
draws; the prior 103 ms aggregate had fewer cheap interpolation frames and is not
a controlled renderer-only comparison.

| Resource | Accepted correction | Slice 2 |
| --- | ---: | ---: |
| Executable file | 84,120 B | 93,256 B |
| Resident text + data + BSS | 100,276 B | 110,540 B |
| Explicit graphics Chip allocation | 132,416 B | 132,416 B |
| Minimum sampled free Chip | 306,728 B | 306,728 B |
| Minimum sampled free total | 592,856 B | 582,592 B |
| Main / worker stack high-water | 2,156 / 1,940 B | 2,180 / 1,940 B |
| Reserved main / worker stacks | 16,384 / 8,192 B | unchanged |

Final sections are text 79,632, data 300, BSS 30,608 bytes. Narration/history owns
1,764 ordinary-RAM bytes; the current owned event adds 100 bytes to replay storage
and effect metadata six bytes. Total resident growth is **10,264 bytes**, including
code and alignment, with **zero new Chip allocations**. Available-memory samples
include OS, broker, library/caller storage, existing world mirrors, stacks and
all graphics. They are sampled minima, not exhaustive worst-case proofs.
Every launch returned **851,312 total / 439,344 Chip bytes free** to DOS after exit,
with no progressive loss observed. This remains the actual 1 MB slow-RAM profile.

Real gameplay exercised 20 event types, including trap/pit damage, pushes, turret
capture/charge, pickup and SHUNT. Golden fixtures cover all 30 types; this run did
not physically exercise every laser/turret-shot/cargo-delivery/tar variant. Native
nine-map replay/JSON equivalence covers broader event state updates. Human review
of deliberate pacing, text readability and longer complex rounds remains the
review gate; no sound or subsequent milestone has been started.

## Actual emulator screenshots

- [Machinery phase, priority and source narration](a2b-slice2/machinery.png)
- [Bounded history while replay continues](a2b-slice2/caption-history.png)
- Push sequence: [start](a2b-slice2/push-0.png), [step](a2b-slice2/push-1.png),
  [continuing](a2b-slice2/push-2.png), [later](a2b-slice2/push-3.png)
- [Authoritative +8 SHUNT and exact score](a2b-slice2/shunt-score.png)
- [Completed-match history](a2b-slice2/shunt-history.png)
- [Trap disarmed after authoritative playback](a2b-slice2/trap-disarmed.png)

The mixed-seat scenario uses Shuntworks, two humans, no bots, three untimed rounds:

| Round | Amiga P1 | JSON P2 |
| --- | --- | --- |
| 1 | FWD2, FWD2, RIGHT | FWD2, FWD2, LEFT |
| 2 | FWD1, LEFT, FWD1 | FWD2, WAIT, WAIT |
| 3 | RIGHT, FWD1, WAIT | WAIT, WAIT, WAIT |

All positions, pushes, disables and points come from the server; the test submits
programmes without mutating the game world.

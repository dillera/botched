# A2b slice 1 — interaction and hazard correction

Corrective review gate after `a397609d`. The 320×256 layout, original sprite/font
artwork, player labels/colours/numbers and priority display are retained. Slice 2
narration/animation has not started. Network/session/replay logic and binary v1
are unchanged.

## Interaction

All three registers start **empty** each round. Empty is a local `BA_EMPTY` marker,
never a wire order. WAIT is command 9 and must be explicitly selected. Both the
Lock action and the wire-body builder reject incomplete programmes.

- Up/Down browses; Space **or Enter** places the chosen command. Number keys 1–9
  place directly. Placement fills the selected empty register, or searches forward
  cyclically for the next empty register. A full programme is never overwritten.
- Tab/Left/Right selects a register; Backspace clears it. Clicking a register
  selects and clears it. Clicking a command uses exactly the keyboard placement
  action. The selected register remains visible after placement.
- **L: LOCK** / the Lock button is separate from placement, visibly disabled until
  all three orders exist. Sending, accepted and locked semantics are unchanged.
  Repeated Lock and edits while pending/locked cannot enqueue another submission.
- Left-click the board for inspection, including overview coordinate conversion.
  Right-click closes inspection/panels; I/Escape remain available. Normal mouse
  input uses KS1.3 Intuition `IDCMP_MOUSEBUTTONS` and copies message data before
  replying. No tablet driver, host pointer APIs or polling loop is in the game.
- Incomplete programmes at a deadline remain the server's responsibility; there is
  no client-side WAIT insertion or speculative submission.

## Persistent hazard language

Normal detail and overview modes show these guides; inspection adds a brighter
outline. They are painted **before crates and robots**, leaving silhouettes and
player identification unobscured. The field guide explains the markings.

| Hazard | Persistent indication |
| --- | --- |
| Armed burst trap | Small red corners on the surrounding eight cells (walls excluded); disappears when authoritative state disarms it |
| Tar generator | Purple broken marks on the surrounding walkable/non-pit cells; permanent |
| Fixed laser | Thin continuous red lane in its facing direction up to static blocking terrain |
| Charging turret | Subtle cross-range dots, up to two cells along each cardinal ray, stopped by blocking terrain; existing owner number and charge pips retained |
| Pit | Existing dark hole silhouette and field-guide warning retained |

Trap armed/owner and turret charge/owner come from the displayed authoritative
snapshot/event mirror. Arm and trigger events invalidate cached affected tiles.
Geometry follows the Linux SDL board's hazard overlays: lasers and turret guides
stop at static blockers, not at transient robots/crates. These are potential lanes
and ranges, **not predicted victims, damage or execution outcomes**. A disarmed
trap retains its dim centre. No artwork or hazard rules were changed.

## Rendering and budget

Profiling the previous slice showed frequent terrain reconstruction and unnecessary
UI repainting. The correction retains a 320×256 four-plane terrain bitmap, updating
only changed group/hazard cells. A camera pan copies the visible rectangle with one
blit. Dynamic objects and existing animations still use dirty cells. Inspection
relations are cached until the selection changes. The inspector hashes live records
rather than the entire world capacity; programme cards and footer lines now repaint
individually.

The terrain cache costs **40,960 additional Chip bytes**, plus bounded ordinary
metadata. This exceeds the original proposal's incremental 24 KB Chip aspiration;
it is a deliberate measured speed/memory tradeoff within the required 1 MB machine,
not hidden in the allocation accounting. It is freed on shutdown. Existing screen,
back buffer, atlas, masks and font remain 91,456 Chip bytes; including the new cache
these graphics allocations total **132,416 bytes**.

## Verification

After sourcing workspace `scripts/env.sh`:

```sh
make -f tools/amiga/Makefile
cmake --build build -j4 --target botched_amiga_view_test botched_amiga_gameplay_test
ctest --test-dir build -R botched_amiga --output-on-failure
cmake -S . -B build/a2b-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build/a2b-sanitize -j4 --target botched_amiga_view_test botched_amiga_model_test botched_amiga_gameplay_test botched_amiga_reliability_test
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build/a2b-sanitize -R botched_amiga --output-on-failure
python tools/amiga/accept.py --a2b
```

Native tests cover incomplete and full programmes, explicit WAIT, cleared/replaced
registers, Space/Enter placement vs explicit Lock, mouse/keyboard equivalence,
pending protection, next-round clearing, board hit testing and viewport bounds.
Valid binary trap-trigger/arm records remove/restore AOE; permanent tar, blocked
laser lanes and turret reach are checked. Existing native gameplay/reliability
suites preserve ordered replay, acknowledgement and JSON equivalence.

The four native suites passed (6.58 seconds), and all four passed under
ASan/UBSan with leak detection (28.31 seconds). The A1c emulator script's Lock key
was updated, but its full disconnect suite was not rerun for these presentation
changes; the native reliability suite was rerun.

The real-NIO emulator gate additionally checks mouse WAIT/clear/Lock, Enter
placement, Backspace, right-click inspection exit, mixed keyboard/mouse programmes,
duplicate Lock, and incomplete orders at a server deadline. A separate Trap Works
match runs two programmes, compares every replay event/animation to the server and
asserts the final trap is disarmed. This is emulator evidence, not physical Amiga
or physical mouse acceptance.

## Measured resources and response

Executable: **84,120 bytes**, SHA-256
`b8ec961b72f6a607437dc2cfb9c33f64bbf7c2b447d238868da4c90cb1ef315e`.
Text/data/BSS: **71,240 / 300 / 28,736**, totalling **100,276 resident bytes**
before runtime allocations. Compared with slice 1, the file grows by 2,996 bytes
and resident sections by 4,992 bytes, in addition to the 40,960-byte Chip cache.
The final rebuild exactly matches the executable exercised by the emulator.

Timing compares the same stock PAL A500/68000/KS1.3, OCS, 512 KB Chip + 512 KB slow
RAM configuration against [slice 1's measurements](a2b-slice1-measurements.json).
`fields × 20 ms` measures drawing, including preemption during NIO activity. New
`dispatch_ms` measures action processing through completed rendering, excluding
pre-dequeue queueing. The host separately times mouse button injection to flushed
UI publication (10 ms polling): that includes IPC/log propagation overhead and is
not a physical input-to-photon measurement. Injected Intuition event timestamps
were unsuitable for this comparison and are not used.

**PASS:** `build/amiga/evidence/a500-xemumoq6`; all nine maps, three completed
matches, 209 server events with identical ordered animation completions, and nine
clean exits. [Full measurements](a2b-slice1-corrections-measurements.json) retain
machine settings, binary/library/broker hashes, each launch, UI timings and mouse
publication observations.

| Draw operation | Slice 1 | Correction |
| --- | ---: | ---: |
| Command browsing | 160 ms | 100–160 ms |
| Space placement (FORWARD 2) | 200 ms | 80 ms |
| Six viewport pans | 560–820 ms; mean 707 ms | 220–260 ms; mean 243 ms |
| Pending Lock redraw | 420–500 ms | 260–380 ms |

The matched pan sequence is about **2.9× faster** (66% less drawing time).
Mouse click-to-publication was **152 ms** for each of two palette placements,
**112 ms** for clearing, **264 ms** for board inspection, and **304 ms** for Lock.
There was no mouse baseline because the previous slice did not support it. These
are individual measured samples, not latency guarantees. Browsing still sometimes
takes 160 ms; inspection and panels are not instantaneous.

The first match averaged 103 ms/draw (469 draws, 14 viewport rebuilds), versus
146 ms/draw previously. The input sequence now contains additional mouse/editor
checks, so that aggregate is context rather than a strict frame-rate comparison.
Cold/full rendering still peaked at **1,260 ms**, worse than the previous 1,080 ms
maximum: caching and hazard guides add initial work. No claim of smooth 50 fps or
finished rendering optimization is made.

| Resource measurement | Slice 1 | Correction |
| --- | ---: | ---: |
| Minimum sampled free Chip | 347,688 B | **306,728 B** |
| Minimum sampled free total | 638,688 B | **592,856 B** |
| Main stack high-water across launches | 2,144 B | **2,156 B** |
| Worker stack high-water | 1,940 B | **1,940 B** |
| Reserved main / worker stacks | 16,384 / 8,192 B | unchanged |

Free-memory samples include the OS, resident broker, client image/data, world,
caller and NIO storage, allocated stacks and graphics. Expansion is A500 **slow**
RAM, not accelerated Fast RAM. These are observed samples/high-water marks, not
proofs of every possible worst case. DOS reported **851,312 total / 439,344 Chip
bytes free after every one of the nine exits**, identical to the earlier slice;
no progressive allocation loss was observed.

## Actual emulator captures

- [Empty programme and persistent hazard guides](a2b-slice1-corrections/ui-detail.png)
- [Filled programme](a2b-slice1-corrections/orders-1.png)
- [Mouse board inspection](a2b-slice1-corrections/ui-mouse-inspect.png)
- [Panned viewport](a2b-slice1-corrections/ui-pan-south.png)
- [Fixed laser lanes ending at obstacles](a2b-slice1-corrections/map-laserworks-01.png)
- [Armed trap area](a2b-slice1-corrections/map-trapworks-01.png)
- [Disarmed after authoritative replay](a2b-slice1-corrections/trap-disarmed.png)
- [Permanent tar](a2b-slice1-corrections/map-tarworks-01.png)
- [Turret ranges](a2b-slice1-corrections/map-turretworks-01.png)

Stop for review. Slice 2, sound, lobby and Atari work remain unstarted.

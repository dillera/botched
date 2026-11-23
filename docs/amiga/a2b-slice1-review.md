# A2b slice 1 — UI and viewport

First review gate only. The approved [design](a2b-proposal.md) is implemented for
static presentation, command entry and navigation. Event narration, action/phase
queue highlighting, caption history and redesigned animation remain **slice 2**.
The existing sequential event playback, cursor reconciliation, NIO process and
submission/retry behaviour are retained.

## What changed

- 320×256, four bitplanes, 16×16 terrain/robot sprites and a 192×160 detail viewport.
  Seven shipped maps fit; Foundry and Factory pan. `V` gives a schematic 8×8-cell
  full-map overview. The white rectangle shows the detail viewport, not a predicted
  route. The palette's OFF numbers identify players outside the detail viewport.
- All nine commands have visible names, icons and numeric shortcuts. Up/down browses,
  Space places, Tab/left/right selects the register, Backspace sets WAIT, Enter locks.
  Browsing cannot silently change a programme. Pending and locked programmes cannot
  be edited or submitted again. The action button distinguishes sending, accepted,
  locked, replay and finished states; rejection/error text remains visible.
- `PLY1`–`PLY4` are **local four-character labels**, independent of server bot names.
  Scores use exactly two digits through 99 and **`++` above 99**. `F2` shows the
  exact value with both label and player number; inspecting an occupied tile also
  shows the exact score. Display formatting never writes the authoritative mirror.
  A dedicated unsigned decimal formatter avoids KS1.3 RawDoFmt's unsupported `%lu`
  in `sprintf`. Zero, 9, 10, 99, 100, 1000 and UINT32_MAX are native-tested; the
  formatter's full u32 capacity is not a claim that every value is a legal server score.
- Player colours, numbers, facing noses and carried-crate packs remain distinct.
  Crates are gold; conveyors use large arrows; docks, dispensers and machinery have
  separate silhouettes. Current trap/turret owner and armed/charge state are drawn
  from the mirror. Three pips show remaining durability, not a two-hit health rule.
- The three-tick priority preview rotates using round and player count. It displays
  seat numbers, not opponents' hidden programmes. The current replay tick is framed;
  per-actor completion and machinery-phase highlighting belong to slice 2.
- `I` inspects tiles and pans with the arrows. Details include coordinates, group
  letter/direction, device ID/owner/state, player heading/damage/cargo and exact score.
  Selection highlights console links and static hazard geometry, never predicted
  victims. `F1` provides a field guide. Escape closes panels/inspection before quitting.
  Help and inspection do not stop the network worker or advance/acknowledge events.

`view.c` contains portable, bounded navigation/formatting/preview logic. `ui.c`
handles panels and cached UI regions; `display.c` owns bitmaps, clipping and dirty
cells. Only two palette rows redraw when command focus changes. UI regions are
published after their complete rendering, avoiding half-written programme cards.
Camera changes rebuild only the board viewport. `assets_data.h` is generated from
the approved original pixel-art source by `tools/amiga/generate_assets.py`; normal
builds require neither Python nor Pillow. No Linux client, firmware, library, wire,
engine or configured-pool changes are included.

## Verification and evidence

Commands after sourcing workspace `scripts/env.sh`:

```sh
make -f tools/amiga/Makefile
cmake --build build -j4 --target botched_amiga_view_test botched_amiga_model_test botched_amiga_gameplay_test botched_amiga_reliability_test
ctest --test-dir build -R botched_amiga --output-on-failure
python tools/amiga/accept.py --a2b
```

All four native suites pass, also under ASan/UBSan with leak detection. They cover
all 2/3/4-player priority rotations through round 200 against the engine, all legal
viewport dimensions, inspection boundaries/static blockers, browsing vs placement,
pending/locked input, exact/overflow scores and unchanged world state. Existing
nine-map gameplay/event equivalence and recovery tests remain in the gate.

The emulator gate uses licensed KS1.3, OCS, cycle-exact 68000, 512 KB Chip + 512 KB
slow expansion, no Fast/Z3 RAM, and real NIO NetworkDevice TCP. It waits for captured
UI publication before screenshots, verifies panning reaches the last Foundry row,
submits F2/TOGGLE/WAIT (including browse/Space entry), completes two matches, compares
all events and animation completions to the server, and launches the other seven
maps. Memory is compared across clean exits. This is emulator evidence, not physical
A500/CRT testing. Exceptional scores are native-tested; the real match screenshots
show ordinary scores rather than injected server values.

## Minimum-machine measurements

The built executable is **81,124 bytes**, SHA-256
`81dfb8e7b5a53dace4e01f42efb6502c466e0e87b3823a7aa16cf689ac1d14e1`.
Text/data/BSS are 68,644 / 300 / 26,340 bytes: **95,284 bytes resident** before
runtime allocations. Compared with the verified A2a follow-up `b24b293a`, this adds
18,524 file bytes and 17,928 resident bytes. Regenerating the packed assets and
rebuilding reproduced the same executable hash.

Screen and offscreen bitplanes remain 81,920 Chip bytes. Atlas and masks use 8,960,
and font bitmap 576: **91,456 Chip bytes** for these graphics allocations, an
increase of 4,376 over A2a. There is no additional full-screen bitmap. The static
tar geometry cache uses 320 ordinary bytes. Reserved stacks remain 16,384 main and
8,192 worker bytes. These allocations fit the design's incremental 48 KB total /
24 KB Chip ceilings; actual free-memory and stack measurements are recorded below.

The final `--a2b` gate passed in `build/amiga/evidence/a500-k9dnkyeq`.
[Machine-readable measurements](a2b-slice1-measurements.json) include executable,
library and broker hashes, machine settings, each launch and captured UI timings.
Two matches produced 86 + 56 authoritative events with corresponding animation
completions. Three programmes were accepted exactly once despite duplicate Enter;
the second match completed on planning deadlines. All nine maps rendered and exited.

| Measurement | A2a follow-up | A2b slice 1 |
| --- | ---: | ---: |
| Lowest sampled free Chip RAM | 352,064 | 347,688 |
| Lowest sampled free total RAM | 661,224 | 638,688 |
| Main stack high-water | 1,932 | 2,144 |
| Worker stack high-water | 1,940 | 1,940 |

Stack maxima above include both matches and all seven additional map launches;
the first match alone measured 1,876 main bytes. These are observed high-water
marks, not proofs of worst-case depth. Free memory includes the running OS, broker,
client code/data, reserved stacks, world/caller storage and graphics allocations;
it is sampled at logged checkpoints rather than continuously. Expansion RAM is
**slow RAM**, despite DOS `Avail` naming its column `fast`. After each of the nine
exits, DOS reported exactly **851,312 free total bytes / 439,344 Chip bytes**:
no progressive loss was observed.

Timing is measured in 20 ms PAL fields around drawing, including elapsed time under
real NIO activity; it excludes waiting for the input message and the final WaitTOF.
Command browsing took 160 ms, explicit placement 200 ms; subsequent register/command
edits took 40–300 ms, and pending-submission redraws 420–500 ms. Inspection movement
without panning took 220–420 ms. Viewport pans took **560–820 ms**, panel opening
440–460 ms and restoration 800 ms. These are visible pauses on the stock 68000,
**not a claim of smooth scrolling or 50 fps**. The first match made 414 draws with
14 viewport rebuilds (2,417 dirty cells), averaging 146 ms/draw and peaking at
1,060 ms; the second averaged 117 ms/draw and peaked at 1,080 ms. Initial full paint
and camera changes dominate the worst draws. The viewport is not rebuilt for every
command or animation frame. Further rendering performance work is warranted,
particularly for panning; this limitation is explicit at this review gate.

## Captures

These are actual Amiberry captures of the final executable, not the design mockups.

- [Detail viewport and command palette](a2b-slice1/ui-detail.png)
- [Exact-score panel](a2b-slice1/ui-scores.png)
- [Field guide](a2b-slice1/ui-guide.png)
- [Whole-map overview](a2b-slice1/ui-overview.png)
- [Inspection at the bottom of Foundry](a2b-slice1/ui-pan-south.png)
- [Training map](a2b-slice1/map-training-01.png)
- [Completed match](a2b-slice1/finished.png)

## Review boundary

Stop here for human readability/interaction review. Slice 2 has not started:
actor/machinery phase progress, explanatory event narration/history, and redesigned
movement/turn/push/crate/hazard/scoring animation remain outstanding. Existing
animation is retained solely to preserve functional sequential playback. No sound,
lobby, names from the server or protocol changes are included.

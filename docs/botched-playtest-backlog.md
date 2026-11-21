# Botched! — Playtest & Presentation Backlog

**Status:** Working backlog, not a change to `botched/1` rules.  
**Goal:** Make round execution understandable and entertaining before adding new mechanics.  
**Working rule:** Implement one small item at a time. Keep the server authoritative; presentation is client-side and must never delay the next round.

## Next — P1: Watch the round unfold

- [x] **B-01 — Visible execution queue (SDL client; done, see notes below).** At the start of each resolved round, show the execution order as a horizontal queue, e.g. for two players in a round starting with P1: `P1(1) P2(1) P2(2) P1(2) P1(3) P2(3)`. Highlight the active entry, then remove/advance it when its action has been shown. The next round rotates priority automatically from the established engine rules. **Do not** use a fixed alternation within every round: priority rotates *by tick*, and the exact order must be derived from the existing engine's rules/events. **Done when:** the displayed queue agrees with `ORDER_EXECUTED` events and all 2–4-player rounds.
- [x] **B-02 — Animate movement and machinery separately (SDL client; essential parts done, see notes below).** Replay the *existing ordered event stream* on the client: each order, individual `FORWARD_2` substeps, rotations, pushes, blocked moves; then the conveyor phase; then hazards, disable/respawn and crate/delivery events. Preserve intermediate board state so players see why a `WAIT` robot moves or a robot falls into a pit. Clearly label phase changes. **Done when:** the documented pit scenario visibly shows entering the conveyor, one belt displacement, falling into the pit, and later respawning, with no invented positions.
- [~] **B-03 — Playback pacing and skip (SDL client: skip + speed done; see notes below).** Default to readable, short per-event holds rather than a hard-coded 15–20-second round. Provide a simple fast/normal/skip option; user input can skip animation and jump to the authoritative final snapshot. Animation is purely local and never blocks server progress. **Done when:** two clients can watch at different speeds and still converge to the same server state.

**Execution note:** An *order* is not the same as a *tick*: each tick processes players in priority order, **then conveyors and hazards**. The display queue should include clear machinery/phase boundaries (or a separate phase indicator) after each tick. Use the authoritative event order instead of simulating movement independently. If the current events are insufficient to replay a particular transition accurately, report the exact gap before proposing a server change.

### SDL implementation notes (execution queue + round playback)

*Where:* `apps/ui_playback.{hpp,cpp}` (SDL-free step builder + playback state machine, unit-tested in `tests/test_ui_playback.cpp`) and `apps/sdl_ui.cpp` (drawing). The terminal client is unchanged.

* **B-01 done.** A strip under the board lists `P1(1) P2(1) ⚙ P2(2) P1(2) ⚙ …` (⚙ drawn as a gear). Entries come only from the round's `ORDER_EXECUTED` / `ORDER_NO_OP` / `ORDER_SKIPPED_DISABLED` events plus one machinery entry per tick, so the order is the server's. Tests pin round 1 `P1(1) P2(1) * P2(2) P1(2) * P1(3) P2(3) *`, round 2 `P2(1) P1(1) * P1(2) P2(2) * P2(3) P1(3) *`, and 3-/4-player rotation over whole matches. The live entry is highlighted; finished entries are dimmed with a tick; chips show the order code (`F2`, `W`, `-` for skipped).
* **B-02 essentials done.** The client keeps a local copy of the board (positions, headings, carried crates, scores, damage, belt-group state), starts from the last snapshot and **commits one server event at a time**: `ROBOT_MOVED` / `ROBOT_ROTATED` (turn animates) / `ROBOT_PUSHED` (victims and pusher move together, impact burst) / belt moves (amber trail + gear badge, only in the machinery phase) / `MOVE_BLOCKED` (nudge + red ✕) / pickup, drop, delivery (ring + "DELIVERED!") / `GROUP_TOGGLED` (belts flip and pulse at that moment) / `LASER_HIT` / `ROBOT_DISABLED` (explosion, then removed) / `ROBOT_RESPAWNED` / `CRATE_SPAWNED`. Nothing is predicted: every coordinate is in an event. Intermediate frames are verified in tests to be exact prefixes of the event stream, and the replay must end on the authoritative snapshot (compared field by field, every round of a scripted 2-player round set and of 12-round 4-player bot matches).
* **Phases are distinct.** Order steps highlight the acting robot; machinery steps get an amber board border, a gear queue chip and a "MACHINERY" caption, and belt chevrons only animate then (frozen during order steps). A `WAIT` robot moved by a belt reads "BELT carries P1 (ordered WAIT)".
* **B-03 partly done.** Default pace ≈ 5–8 s for a 2-player round and 10–14 s for 4 players (measured over 12 bot rounds each). `--anim-speed X` sets the speed, `[` / `]` (or `-` / `=`) change it live (0.5×–4×), `Space` or the SKIP button commits all remaining events at once, `--no-anim` disables the replay. Playback is local; the server and other clients never wait for it. Not done: per-event hold tuning beyond the constants in `ui_playback.cpp`.
* **Known limits / observations.**
  * If rounds resolve faster than they play (everyone locks in at once), replays queue up; SKIP clears the whole backlog. The title shows `REPLAY` while a replay is running.
  * The hover read-out and the "your robot" panel describe the authoritative state, not the mid-replay board.
  * Narration lines are released per tick as the replay reaches them; the next round's spawn/respawn lines arrive with the last tick.
  * The match-over overlay waits for the replay to finish.
  * Testing aid: `botched-client --ui --programs "R F2 L;F2 F W;…"` auto-submits scripted orders per round.

## Soon — P2: Playtesting quality

- [ ] **B-04 — Replay last resolved round.** Allow a player to rewatch the most recent round locally from its starting snapshot and events, if that data is retained; otherwise record the limitation. Valuable for understanding surprising pushes and conveyor effects.
- [ ] **B-05 — Clear instruction priority/help.** Show the active player/order, upcoming actions and a small legend for the rotating tick-priority rule. Keep the board visually dominant; verbose explanations belong behind `help`.
- [ ] **B-06 — Test map interactions, not balance changes.** Keep `training-01` unchanged. Manually play two to four players into the centre to assess conveyor pushing, consoles, pits and whether cause/effect is legible. Record observed friction, rather than immediately changing rules.

## Later — P3: Broader game experience (parked)

- [ ] **B-07 — Server-owned bot seats in the lobby.** Support any mixture of humans and bots adding to 2–4 players, including solo human vs 1–3 bots. Bots submit normal legal programmes, have imperfect but goal-directed behaviour, and don't require separate client processes. Distinct from existing `--bot` demo clients.
- [ ] **B-08 — Playtest map variants and scoring discussion.** Experiment with shared/contested dispensers and short routes that encourage interaction; judge instructions-to-delivery and avoid repeatable safe loops. **No new scoring, traps or teleporters yet.**
- [ ] **B-09 — Event sound cues.** Optional local audio for movement, impacts, pushing, crate pickup/delivery and comic disable/respawn (the “whack, whack, oops” moment). Should be unobtrusive, muteable and client-specific.
- [ ] **B-10 — Platform-specific presentation.** Amiga: smoother sprites/animation and sound. Constrained clients (e.g. Palm/8-bit): discrete event stepping. Same semantic events and final state across platforms.
- [ ] **B-11 — Compact binary transport (existing M1 requirements).** Revisit `docs/binary-protocol-requirements.md` when starting retro-client integration; do not expand its scope during UI playtesting.


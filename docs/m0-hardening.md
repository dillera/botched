# Botched! — M0 Hardening Tracker

**Authority:** `docs/botched-game-spec-v1.md`  
**Interpretations:** `docs/rules-interpretations.md`  
**Rules version:** `botched/1` (do not change without approval)  
**Scope:** M0 correctness, robustness and acceptance. **No M1 implementation in this pass.**

## Working agreement

- Work **one unchecked task at a time**, in order. Stop after each task and report before moving on.
- Preserve the existing C++20 architecture and deterministic game behaviour. Do not redesign or add mechanics.
- Add focused regression tests for each fix; run relevant tests and commit changes separately.
- Update each task below with a commit SHA, tests run and any remaining concern before checking it off.
- If a fix conflicts with the authoritative spec, changes the `botched/1` wire/rules contract, or meaningfully increases scope, **stop and request a decision** rather than silently changing behaviour.

## M0 tasks

### [x] M0-H1 — Validate saved/restored state

Audit `read_world()` and `Match::restore()`. Reject malformed or incompatible checkpoints cleanly: robot and crate identities/ownership, positions/occupancy, carrying relationships, group state, round/tick/phase consistency, programme records, and map/rules compatibility. Add valid restore/replay and corrupted-state tests. Keep validation at the restoration boundary, not in the hot simulation path.

**Evidence:** Commit: `159f7047` · Tests: `tests/test_restore.cpp` (12 cases); full suite 111 cases / 6109 assertions pass, also under ASan+UBSan; `botched-sim` state/event hashes byte-identical to pre-H1 (`e846c1c016b36358` / `5d9ebce915bed1f8`) · Notes:

- **Baseline first:** the new tests were run against the old code: null `cfg.map` aborted the process, and ~190 corruption assertions failed (protocol/rules/board ignored, no range checks, no occupancy/ownership/phase checks). Valid round-trips already worked.
- **Fixed (M0 blockers):** malformed checkpoint could place a robot off-board or in a wall/pit and make the engine index out of bounds (UB) — a robustness defect at an untrusted boundary; `restore()` also accepted another rules version/match/map silently.
- **Atomicity:** `restore()` is a static factory that validates everything into locals and only then builds the `Match`; failure builds nothing. Test: a checkpoint corrupted in the *last* stage inspected (programmes) is rejected, the source match / shared map / checkpoint are unchanged, and a good restore works afterwards.
- **Producer fix found by the tests:** `Match::Entry::orders` value-initialised to `FORWARD_1`, so `save()` wrote a bogus order for unsubmitted players. Never executed (close substitutes WAIT), but non-canonical; now defaults to WAIT. No `botched/1` gameplay or wire change.
- **Derived-field hole found by a mutation survey:** `submitted_players`/`ready_players` were silently ignored on restore; now cross-checked against `programs`.
- **Rejected by design:** non-contiguous ids, active robot on pit/wall/outside board, two robots on a tile, damage 3 on an active robot, score != 5 x deliveries, damage > damage_taken, deliveries > crates ever spawned, carried-crate/carrier disagreement, loose crate on wall/pit or sharing a tile, unsorted/duplicate crate ids, groups differing from the map, `FINISHED` anywhere but the last round, `state_version` below what the history implies, `event_cursor` not equal to (round, tick).
- **Non-blocking / not done (by design):** validation proves *internal consistency and engine-safety*, not *reachability* — a hand-edited but self-consistent state (e.g. a robot moved to another legal tile) is accepted, as it is a legal state. Checkpoints are not authenticated or signed (integrity vs tampering is host/M1 scope). Request-id history is not in the checkpoint (M1-02). The `Json` API cannot take a `long long` literal directly on this platform (ambiguous overload); test-only nuisance, left alone.

### [x] M0-H2 — Harden untrusted input

Review JSON parsing and `GameServer` request handling for input/depth/collection limits, numeric overflow or narrowing, invalid field types/ranges and malformed messages. Reject bad requests without crashes or state mutation. Keep dependencies lightweight; add negative tests. Avoid turning the stdio reference host into a production network server.

**Evidence:** Commit: `e91271c8` · Tests: `tests/test_hardening.cpp` (18 cases); full suite 129 cases / 17924 assertions pass, also under ASan+UBSan; `botched-sim` state/event hashes byte-identical to pre-H2 · Limits: `docs/protocol-limits.md` · Notes:

- **Baseline first:** the field/parser tests were run against the old code (21/21 wrong-type optionals, 11/15 wraparound, 25/34 SUBMIT, 6/9 `since_version`, 6/7 identifiers, 4 number, 3 string, 2 duplicate-key assertions failed).
- **Real defects (M0 blockers, fixed):**
  1. *Narrowing:* `static_cast<int>` on client int64s. `round`/`revision` = 2^32+n was processed as n (a stale-round check bypass); `player_count` 2^32+4 created a 4-player match; `planning_seconds` 2^32 silently disabled the timer.
  2. *Silent defaults:* a present-but-wrong-typed `map_id`/`player_count`/`rounds`/`planning_seconds`/`ready`/`request_id` was replaced by its default (a non-string `request_id` silently turned idempotency off).
  3. *Unbounded memory:* stdio `getline`; per-match request-id cache (unique ids forever); number of hosted matches; JSON with no size/array/member/string/node limits.
  4. *Parser ambiguity:* duplicate keys (last wins), leading zeros, raw control bytes, non-ASCII.
- **Fixed by:** `ParseLimits` + strict dialect; `kMaxRequestBytes` (2048) checked before parsing and tight per-request parse limits; typed field readers with int32 range checks before any narrowing; token-shaped identifiers; `max_matches` (16); request-id memory bounded to 64/player; `read_line_bounded()` for the stdio host; errors echo only well-formed identifiers.
- **Proof of "no mutation":** 6000 seeded mutations of valid requests (byte flips, truncation, hostile values, chunk dup/delete, concatenation) assert that any `ok:false` response leaves the authoritative state of every match byte-identical; each field also has direct negative tests. A 50 MB newline-less hostile stdio stream (+ binary junk, 100 000-deep nesting) was answered with `REQUEST_TOO_LARGE`/`BAD_REQUEST` at ~12 MB peak RSS and the server then served valid requests.
- **Contract impact (flag for review):** gameplay and the wire format are unchanged and in-range requests behave exactly as before. Additive/tightening items: new error codes `REQUEST_TOO_LARGE`, `TOO_MANY_MATCHES`; `BAD_REQUEST` (instead of silent defaulting or wraparound) for malformed fields; duplicate JSON keys now refused. A `request_id` retry older than the last 64 for that player is no longer answered from memory (see H3). I judged these within "reject bad requests" and not a rules/wire change, but they are the items to veto if you disagree.
- **Non-blocking / deferred:** the lobby operations (`CREATE_MATCH`/`JOIN_MATCH`/`START_MATCH`) remain unauthenticated and there is no rate limiting or connection handling — transport/M1 scope, stated in `protocol-limits.md`; finished rooms are never reclaimed (M1); `GameServer::handle(const Json&)` cannot bound a tree the caller already built (hosts must parse with `handle_text`). The 16-match and 64-id values are defaults chosen for constrained hosts, not spec numbers.

### [x] M0-H3 — Review request-ID semantics

Verify that identical `(player, request_id)` retries return their original response. A reused ID with different payload currently returns that original response; assess the trade-off of rejecting it as a conflict. **Document the recommended contract first and seek approval if this changes `botched/1` behaviour.** Add/adjust regression tests only for approved semantics.

**Evidence:** Commits: contract approval `bc034136` · binary-protocol requirements `8c941c2f` · implementation `eacfb2c8` (proposal was `a0fcd19b`) · Tests: `tests/test_idempotency.cpp` (21 cases); full suite 149 cases / 28073 assertions pass, also under ASan+UBSan; `botched-sim` state/event hashes byte-identical (`e846c1c016b36358` / `5d9ebce915bed1f8`, now pinned by a test) · Contract: `docs/request-id-contract.md` · Notes:

- **Decisions (all approved):** D1 no eviction + per-player per-round budget; D2 reused id with different payload -> `REQUEST_ID_REUSED`; D3 identical retries always replay (also after close/advance/end); D4 accepted *and* rejected outcomes recorded; D5 budget 32, configurable 8-128 (clamped); D6 see below; D7 new codes `REQUEST_ID_REUSED`, `REQUEST_LIMIT`; D8 persistence deferred to M1-02.
- **D6 resolved (exact records, not digests):** a 64-bit digest cannot guarantee that distinct requests are never conflated (collision p ~ 4.5e-13/match, undetectable; an unkeyed hash is forgeable against the forger's own slot). The payload is 12 exact bytes, so the id is stored verbatim and the payload field by field. Cost: 56 B/record (+65 B heap for ids > 15 chars) vs 32 B; bound = players x rounds x budget (86-209 KB for 4 players x 12 rounds at the default budget; table in the contract). One deliberate lossy corner, argued safe in the contract: different *unrecognised* order tokens are one value because the rules answer them identically with no effect.
- **Budget cannot be bypassed:** malformed, unauthenticated, unknown-player and structurally invalid requests never reach the store; replays, `REQUEST_ID_REUSED`, `REQUEST_LIMIT` and unkeyed requests are never recorded or charged; repeated identical rejections are replays; fresh-id rejections are charged until `REQUEST_LIMIT`; only a round advance refills; players are independent; accounting depends on the call sequence only, not on time, hash order or other players' interleaving. Tests cover each, plus a model-based fuzz (3 seeds x 6000 ops) and mutation checks (three deliberately broken implementations are caught).
- **Measured defect that motivated it:** with the H2 LRU, a retry of an accepted+locked programme returned `PROGRAM_LOCKED` after 64 later ids (the cheap junk can be rejected requests), reachable by a conforming client (~48 ids/player/match).
- **botched/1 clarifications/tightenings (listed in the contract, section 6):** two new codes; reused id with different contents refused; retries older than 64 ids now replay; new ids rationed per round; rejections replayed (a stale "too early" rejection needs a fresh id); store empty after a checkpoint restore. Gameplay, resolution order, events and `state_version` are unchanged and replays emit nothing.
- **Known limitation / non-blocking:** an exhausted budget locks that player's own slot for the rest of the round (by design, D1; refills when the round advances). The store is lost on restore (M1-02). Which order tokens count as one "unrecognised" value is an implementation detail documented in the contract.
- **Test-hygiene fix:** the H2 fuzz fixture used random session tokens, making its assertion count vary run to run; it now uses the deterministic-token option so failures are reproducible.
- **Binary protocol requirements recorded (not implemented):** `docs/binary-protocol-requirements.md` (M1, normative). JSON baseline sizes in it are measured from this build.

### [x] M0-H4 — Freeze and test rules interpretations

Cross-check all entries in `docs/rules-interpretations.md` against the authoritative spec, implementation and fixtures. Pay particular attention to conveyor displacement, robot/crate conveyor interactions, cumulative damage tie-breaks and pit-disable crate drops. Fill test gaps without changing gameplay; flag any normative disagreement for decision.

**Evidence:** Commit: `92909d2b` (closeout together with H5) · Tests: one existing test corrected (below); no new suites · Notes:

- **Result: no gameplay contradictions found.** All 24 interpretations were re-read against spec sections 3, 5, 6 and 9 and against the tests that pin them. Conveyor displacement (#5, #6), robot/crate conveyor interaction (#7), the damage tie-break (#11) and pit-disable crate drops (#12) are each pinned by a named test that exercises the behaviour, and none contradicts the spec text; they remain the four readings a rules owner may wish to confirm (changing any is a rules-version bump).
- **Spec vs implementation, mechanically:** the 22 event types in spec 9.3 equal the 22 the engine emits (none missing, none extra). Random play produces 20 of them; `CRATE_DESTROYED` and `RESPAWN_REDIRECTED` are covered by direct unit tests.
- **One genuine coverage gap, fixed:** spec 11 item 14 names *group states*, but random play leaves a group reversed in only 1 of 360 round-end states and the H1 test titled "...and toggled group" never toggled anything, so restoring a reversed group was not really exercised. That test now plays a real route (walk to the console, collect a crate on the way, `TOGGLE`), restores **mid-round** with a reversed group, a carried crate and a pending phase, and continues both copies to an identical state. It replaced the mislabelled test; it is not an additional suite.
- **Not blockers (recorded, nothing started):** #8 (failed crate conveyor move emits no event) and #19 (a respawn onto a loose crate does not pick it up) have no pinning test; the spec is silent and neither affects score, safety or determinism. One loose reference (`server: deadline`) was corrected to the real test name.

### [x] M0-H5 — Final M0 acceptance

Run complete tests, ASan/UBSan and deterministic full-match simulations (2–4 players, multiple seeds, missed submissions/retries and save/restore). Check all 14 acceptance categories in spec §11 and invariants beyond hash equality. Summarise pass/fail evidence, known M0 blockers versus deferred work, rules compatibility and whether M0 can be accepted. Update README/status documentation accurately.

**Evidence:** Commit: `92909d2b` · Tests: 149 cases / 28 082 assertions · **Verdict: M0 ACCEPTED, no blockers.** Details:

| Check | Result |
|---|---|
| Full suite, Debug | 149/149 cases, 28 082 assertions pass; `ctest` passes |
| ASan + UBSan (`-fno-sanitize-recover`) | 149/149, clean |
| Release `-O2 -Werror` | builds without warnings; 149/149 |
| Deterministic simulations | 2, 3 and 4 players x seeds 1, 7, 42, each run twice by `botched-sim --check`: identical state and event hashes; with 25% missed submissions (13 timeouts) also identical |
| Debug vs Release | identical state/event hashes (default seed and the missed-submission run) |
| `botched/1` behaviour | simulator hashes unchanged since before H1 (`e846c1c016b36358` / `5d9ebce915bed1f8`, pinned by a test) |
| Invariants beyond hash equality | per-seed simulation checks (25 seeds, incl. missed submissions): score = 5 x deliveries, every carried crate has exactly one carrier, damage in range; H1 validator enforces ~25 structural invariants on every checkpoint |

Spec 11 acceptance categories (each pinned by named tests in `tests/`):

| # | Category | Tests |
|---|---|---|
| 1 | rotation, forward/back, wall/border | `test_movement` "1." |
| 2 | `FORWARD_2` | "2." |
| 3 | push chains, blocked chains, pit pushes | "3." |
| 4 | rotating priority | "4." |
| 5 | pickup, own/rival dock, capacity | `test_crates` "5.", e2e in `test_match` |
| 6 | drop, conveyor movement and contention | "6." |
| 7 | conveyor snapshot (no double move) | "7." |
| 8 | console before conveyors, toggles cancel | `test_machinery` "8." |
| 9 | lasers: wall, several robots, overlap | "9." |
| 10 | third-damage disable, pit, crate drop/destroy, skipped orders, respawn tie-break | "10." |
| 11 | missing programme, idempotency, stale revisions | `test_match` "11.", `test_idempotency` |
| 12 | arrival-order and pacing independence | "12.", `test_idempotency` determinism |
| 13 | round-12 end, tie-breaks | "13." |
| 14 | reconnect: carried crates, group state, pending phase | "14.", `test_restore` (now with a real reversed-group restore) |

M0 acceptance criteria (spec 12 "M0 - Pure rules engine" plus the original task priorities 1-5): map parser/validator, robots, orders, pushing, pickup/delivery, turn priority, snapshot, deterministic replay, unit tests, no network/rendering; deterministic headless library; grid/objects/orders/collisions/round lifecycle; canonical state and structured events; section 11 tests; POSIX reference app completing a 4-player match. **All met.** Priority 6 (NIO transport and platform clients) is M1 by design.

**Known non-blocking items** (recorded, nothing started): rules-owner confirmation of interpretations #5/#7/#11/#12; #8/#19 unpinned (H4); determinism verified only with GCC 16 on x86-64 Linux, so cross-compiler/target checks belong in M1 CI; simulated bots are weak and random so ties/shared wins are common in simulation (balance is a playtest criterion per spec 7, not an M0 criterion); lobby operations unauthenticated, no rate limiting, rooms never reclaimed, stdio host only checks deadlines when a request arrives, no host-level persistence (M1-01/M1-02/M1-03).

## Deferred — M1 (do not implement under this tracker)

- **M1-01: Unattended planning deadlines.** The blocking stdio host only calls `poll()` when input arrives; the real host needs scheduling independent of requests. *(Done for the TCP host in M1-A: its loop applies deadlines every pass; the stdio host is unchanged.)*
- **M1-02: Full server recovery.** `Match::save()/restore()` is not persistence for rooms, sessions, deadlines or **request-ID idempotency records** (approved decision D8; the contract's records are 56 B and cheap to checkpoint). Define and implement the required host-level recovery during M1.
- **M1-03: Network transport and playable client.** NIO adapter, interactive ASCII reference client and real multi-process play belong to M1; retro ports to M2. *(M1-A delivered the POSIX TCP host and the interactive terminal client; the NIO adapter remains.)* The transport carries whichever wire representation M1-04 defines.
- **M1-04: Binary wire protocol for retro clients.** One authoritative engine, multiple wire representations: keep JSON for POSIX testing/debugging/reference tooling and add a compact binary encoding for production retro (6502-class) clients. Normative requirements, budgets, open design choices (O1-O13) and acceptance criteria: **`docs/binary-protocol-requirements.md`**. Must honour the approved H3 request-id contract (a per-player sequence number may replace ASCII ids only by mapping into the same keyspace and semantics), reuse existing FujiBus/SLIP/packet-link framing and integrity, and prove JSON/binary equivalence with golden fixtures, round-trip and cross-encoding tests. M0 changes nothing for it.

## Completion log

| Task | Commit | Verification | Decision / remaining issue |
|---|---|---|---|
| H1 | `159f7047` | 111 cases / 6109 assertions; ASan+UBSan clean; sim hashes unchanged | None blocking. Reachability and authentication are out of scope (see notes). |
| H2 | `e91271c8` | 129 cases / 17924 assertions; ASan+UBSan clean; sim hashes unchanged; 50 MB hostile stream at ~12 MB RSS | None blocking. New error codes and request-id memory window flagged for review (see H2 notes / H3). |
| H3 | `eacfb2c8` (contract `bc034136`, binary requirements `8c941c2f`) | 149 cases / 28073 assertions; ASan+UBSan clean; sim hashes unchanged and pinned; mutation-checked | None blocking. D6 resolved to exact records. Budget lock-out of a slot for one round and store-not-checkpointed are by design (D1, D8/M1-02). |
| H4 | `92909d2b` | 24 interpretations re-checked; 22/22 spec event types match; one real coverage gap (reversed-group restore) fixed | None blocking. #8/#19 unpinned, non-critical. |
| H5 | `92909d2b` | 149 cases / 28 082 assertions; ASan+UBSan and Release -Werror clean; 2/3/4 players x 3 seeds + missed submissions deterministic; Debug==Release hashes | **M0 accepted.** GO for the playable MVP (M1). |

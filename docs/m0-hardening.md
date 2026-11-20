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

### [ ] M0-H2 — Harden untrusted input

Review JSON parsing and `GameServer` request handling for input/depth/collection limits, numeric overflow or narrowing, invalid field types/ranges and malformed messages. Reject bad requests without crashes or state mutation. Keep dependencies lightweight; add negative tests. Avoid turning the stdio reference host into a production network server.

**Evidence:** Commit: _pending_ · Tests: _pending_ · Notes: _pending_

### [ ] M0-H3 — Review request-ID semantics

Verify that identical `(player, request_id)` retries return their original response. A reused ID with different payload currently returns that original response; assess the trade-off of rejecting it as a conflict. **Document the recommended contract first and seek approval if this changes `botched/1` behaviour.** Add/adjust regression tests only for approved semantics.

**Evidence:** Commit: _pending_ · Tests: _pending_ · Notes: _pending_

### [ ] M0-H4 — Freeze and test rules interpretations

Cross-check all entries in `docs/rules-interpretations.md` against the authoritative spec, implementation and fixtures. Pay particular attention to conveyor displacement, robot/crate conveyor interactions, cumulative damage tie-breaks and pit-disable crate drops. Fill test gaps without changing gameplay; flag any normative disagreement for decision.

**Evidence:** Commit: _pending_ · Tests: _pending_ · Notes: _pending_

### [ ] M0-H5 — Final M0 acceptance

Run complete tests, ASan/UBSan and deterministic full-match simulations (2–4 players, multiple seeds, missed submissions/retries and save/restore). Check all 14 acceptance categories in spec §11 and invariants beyond hash equality. Summarise pass/fail evidence, known M0 blockers versus deferred work, rules compatibility and whether M0 can be accepted. Update README/status documentation accurately.

**Evidence:** Commit: _pending_ · Tests: _pending_ · Verdict: _pending_

## Deferred — M1 (do not implement under this tracker)

- **M1-01: Unattended planning deadlines.** The blocking stdio host only calls `poll()` when input arrives; the real host needs scheduling independent of requests.
- **M1-02: Full server recovery.** `Match::save()/restore()` is not persistence for rooms, sessions, deadlines or request-ID history. Define and implement the required host-level recovery during M1.
- **M1-03: Network transport and playable client.** NIO adapter, interactive ASCII reference client and real multi-process play belong to M1; retro ports to M2.

## Completion log

| Task | Commit | Verification | Decision / remaining issue |
|---|---|---|---|
| H1 | `159f7047` | 111 cases / 6109 assertions; ASan+UBSan clean; sim hashes unchanged | None blocking. Reachability and authentication are out of scope (see notes). |
| H2 | | | |
| H3 | | | |
| H4 | | | |
| H5 | | | |

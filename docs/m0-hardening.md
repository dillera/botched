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

### [ ] M0-H4 — Freeze and test rules interpretations

Cross-check all entries in `docs/rules-interpretations.md` against the authoritative spec, implementation and fixtures. Pay particular attention to conveyor displacement, robot/crate conveyor interactions, cumulative damage tie-breaks and pit-disable crate drops. Fill test gaps without changing gameplay; flag any normative disagreement for decision.

**Evidence:** Commit: _pending_ · Tests: _pending_ · Notes: _pending_

### [ ] M0-H5 — Final M0 acceptance

Run complete tests, ASan/UBSan and deterministic full-match simulations (2–4 players, multiple seeds, missed submissions/retries and save/restore). Check all 14 acceptance categories in spec §11 and invariants beyond hash equality. Summarise pass/fail evidence, known M0 blockers versus deferred work, rules compatibility and whether M0 can be accepted. Update README/status documentation accurately.

**Evidence:** Commit: _pending_ · Tests: _pending_ · Verdict: _pending_

## Deferred — M1 (do not implement under this tracker)

- **M1-01: Unattended planning deadlines.** The blocking stdio host only calls `poll()` when input arrives; the real host needs scheduling independent of requests.
- **M1-02: Full server recovery.** `Match::save()/restore()` is not persistence for rooms, sessions, deadlines or **request-ID idempotency records** (approved decision D8; the contract's records are 56 B and cheap to checkpoint). Define and implement the required host-level recovery during M1.
- **M1-03: Network transport and playable client.** NIO adapter, interactive ASCII reference client and real multi-process play belong to M1; retro ports to M2. The transport carries whichever wire representation M1-04 defines.
- **M1-04: Binary wire protocol for retro clients.** One authoritative engine, multiple wire representations: keep JSON for POSIX testing/debugging/reference tooling and add a compact binary encoding for production retro (6502-class) clients. Normative requirements, budgets, open design choices (O1-O13) and acceptance criteria: **`docs/binary-protocol-requirements.md`**. Must honour the approved H3 request-id contract (a per-player sequence number may replace ASCII ids only by mapping into the same keyspace and semantics), reuse existing FujiBus/SLIP/packet-link framing and integrity, and prove JSON/binary equivalence with golden fixtures, round-trip and cross-encoding tests. M0 changes nothing for it.

## Completion log

| Task | Commit | Verification | Decision / remaining issue |
|---|---|---|---|
| H1 | `159f7047` | 111 cases / 6109 assertions; ASan+UBSan clean; sim hashes unchanged | None blocking. Reachability and authentication are out of scope (see notes). |
| H2 | `e91271c8` | 129 cases / 17924 assertions; ASan+UBSan clean; sim hashes unchanged; 50 MB hostile stream at ~12 MB RSS | None blocking. New error codes and request-id memory window flagged for review (see H2 notes / H3). |
| H3 | `eacfb2c8` (contract `bc034136`, binary requirements `8c941c2f`) | 149 cases / 28073 assertions; ASan+UBSan clean; sim hashes unchanged and pinned; mutation-checked | None blocking. D6 resolved to exact records. Budget lock-out of a slot for one round and store-not-checkpointed are by design (D1, D8/M1-02). |
| H4 | | | |
| H5 | | | |

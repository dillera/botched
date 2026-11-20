# Request-ID idempotency contract (botched/1) — APPROVED

**Status:** APPROVED (decisions D1-D8, 2026-11-20); implemented under M0-H3.
**Authority:** spec section 9.2, `docs/protocol-limits.md`, `docs/rules-interpretations.md` #17-18.
**Scope:** `botched/1` gameplay and wire schema are unchanged. Every tightening or clarification
is listed in section 6.

## 1. What the spec requires

> `SUBMIT_PROGRAM` is accepted only for the currently connected, authenticated player's slot,
> while planning that round, unless `ready` is already true. **Repeated identical `request_id`
> MUST return the original response idempotently.** Revisions lower than an accepted revision
> MUST be rejected; equal revision with different contents MUST be rejected. **Commands
> received after planning closes MUST be rejected without changing results.**

Section 10 adds that clients must "submit/retry safely" and need neither a persistent connection nor
synchronised clocks. Retry policy and transport belong to NIO infrastructure.

## 2. Why H2's 64-entry cache was replaced (measured)

Scenarios run against the H2 `Match`:

| # | Scenario | H2 result | Verdict |
|---|---|---|---|
| S1 | Retry, same id and payload | original | correct |
| S2 | Same id, **different payload** | original response; the new payload **silently not applied** while the client believes it was | dangerous |
| S3 | Retry after planning closed | original `OK` (new id: `PLANNING_CLOSED`) | correct |
| S4 | Retry after the round advanced | original `OK` (new id: `WRONG_ROUND`) | correct |
| S5 | **Retry after >64 later ids** | `PROGRAM_LOCKED`: an *accepted* command reported as *failed* | **violated the MUST** |
| S5b | Same, accepted unready, round advanced | `WRONG_ROUND` | **violated the MUST** |
| S7 | "Too early" rejection retried with the same id | the stale rejection forever | spec-literal; now documented |

Eviction was reachable by cheap rejected junk and by a conforming client (one revision per slot
edit is about 48 ids/player/match against a cap of 64). Eviction means the guarantee fails *silently*.

## 3. Decision record

| # | Decision | Rationale and trade-off |
|---|---|---|
| **D1** | Replace the LRU with **match-lifetime records and a per-player, per-round budget of new ids** | The spec's MUST cannot be kept with eviction. Converting "forget an old id" into "refuse a new id" makes the failure explicit and harmless. Cost: a buggy or hostile client can exhaust *its own slot's* budget for one round (its robot then WAITs); it cannot touch other players or memory. |
| **D2** | **Same id, different payload -> `REQUEST_ID_REUSED`**, nothing applied, original record untouched | The spec says "identical `request_id`" but a retry means resending the same command. Replaying an unrelated response (S2) silently loses a programme. Conforming clients never see it. |
| **D3** | **Identical retries always replay the original**, including after planning closes, after the round advances and after the programme is locked | A retry is not a new command; replay changes no state, so "rejected without changing results" still holds. Rejecting it would tell a client whose `ready` acknowledgement was lost that its (applied) programme failed. Clients should read `GET_SNAPSHOT` after any `PLANNING_CLOSED`. |
| **D4** | **Accepted and rejected outcomes are both recorded**, for requests with a valid session, an existing slot, a non-empty valid `request_id` and valid fields (section 4.1) | Spec-literal ("the original response"). Cost: rejections consume budget. A client that regenerates an id per retry burns its own budget; a retry must reuse the id. |
| **D5** | **Budget N = 32 new ids per player per round**, configurable **8-128** (`MatchConfig::request_budget_per_round`, `ServerConfig::request_budget_per_round`; out-of-range values are clamped) | 32 is >=6x a conforming client (about 5 ids/round). 8 still allows one revision per slot edit plus `ready`; 128 is the memory ceiling in section 5. Not a wire field: clients cannot raise it. |
| **D6** | **Exact records: the `request_id` is stored verbatim and the payload fields exactly.** No digest comparison | See 3.1. Digest-only cannot guarantee that distinct requests are never conflated. Cost: about 56 B/record (+ id heap only for ids > 15 chars) instead of 32 B. |
| **D7** | New error codes **`REQUEST_ID_REUSED`**, **`REQUEST_LIMIT`** | Lets clients react (re-key / wait for the next round). They refuse input outside the conforming set. |
| **D8** | **Records are not checkpointed**; persistence deferred to **M1-02** | `Match::save()/restore()` stays a single-match checkpoint. After a restore the store is empty and budgets start fresh; documented limitation, see 6. |

### 3.1 D6: why digests are not enough

The proposal stored 64-bit digests of the id and the payload. The requirement is that distinct
requests are never silently treated as identical:

* A digest collision between two ids of one player makes the second request answer with the first's
  stored response (or, if the payload digests also collide, pass the conflict check).
  Probability for n <= 4096 ids/player is about n^2 / 2^65 = 4.5e-13 per match. Negligible, but
  *non-zero and undetectable*, so the guarantee would be probabilistic.
* An unkeyed 64-bit hash (FNV) can be forged on purpose. This could only confuse the forger's own
  slot (the key includes the authenticated player), but a guarantee that depends on "only the
  attacker is hurt" is weaker than one that is simply true.
* The payload is tiny: `round`, `revision`, `ready` and three order slots are 12 exact bytes. Digesting
  them saves nothing.
* A safe bounded alternative therefore exists: **store the id verbatim (<= 64 B, already bounded by
  H2) and the payload exactly.** Memory stays bounded by the same budget; only the constant changes.
* Keyed hashes or longer digests (128 bit) make collisions cryptographically unlikely but still not
  impossible, and add a key to manage; exact storage removes the question.

A lossy corner remains by design, and it is safe: an order token the rules do not recognise is
stored as a single "invalid" value. Two *different unrecognised* payloads are therefore equivalent
for idempotency. That is correct because the rules answer every such payload with the identical
rejection and no effect. For every payload that can have an effect, comparison is exact.

## 4. The contract (normative)

Applies to `SUBMIT_PROGRAM`. All other requests are reads or lobby operations and keep no history.

1. **Keyed request.** Non-empty `request_id` (H2 alphabet, <= 64), valid session for an existing
   slot, fields valid (H2). Only keyed requests read or write the store. Unkeyed, unauthenticated,
   unknown-player and malformed requests (`BAD_REQUEST`, structural `INVALID_ORDERS`) never do.
2. **Single-use ids.** The key is `(player, request_id)` and identifies one command. A retry
   resends the same command (same `round`, `revision`, `orders`, `ready`). A **new** command, including
   re-sending after a rejection or for a later round, uses a **fresh** id.
3. **Decision order for a keyed request:**
   1. record found, payload equal -> **replay** the original response (D3); no budget, state or event effect;
   2. record found, payload different -> `REQUEST_ID_REUSED` (D2); not recorded, no budget;
   3. no record and the player has used N new ids this round -> `REQUEST_LIMIT`; not recorded, no state change;
   4. otherwise decide by the rules (`PLANNING_CLOSED`, `WRONG_ROUND`, `PROGRAM_LOCKED`, `INVALID_ORDERS`,
      `INVALID_REVISION`, `STALE_REVISION`, `REVISION_CONFLICT`, or accept), **record the outcome** (D4)
      and count one id against the budget of the round in which it was first seen.
4. **Replay fidelity.** The replayed `ok`, `code`, `message`, `revision` and `state_version` are
   identical to the original; the server's JSON for a retry is byte-identical.
5. **Budget round.** The `Match` round at first sight. After the match finishes, round `rounds` keeps being
   charged, so memory stays bounded.
6. **No eviction.** Records live until the match ends (or the room is discarded).
7. **`REQUEST_LIMIT` is not a command outcome.** It is a refusal to process; the same id may be presented
   in a later round and is then processed as new.
8. **Persistence.** Not part of `save()`; `restore()` yields an empty store and fresh budgets (D8).

### 4.1 Budget bypass analysis

| Attempt | Outcome |
|---|---|
| Malformed requests (bad JSON, wrong types, oversize, bad session, unknown player, structural order errors) | rejected at the server boundary before `Match`; consume nothing and **cannot refill** anything |
| Repeating the same rejected submission | replay, free; cannot grow the store or burn budget |
| Rejected submissions with **fresh** ids | each costs one budget unit and one record; stops at N per round, then `REQUEST_LIMIT` (no record) |
| Same id with varying payloads | `REQUEST_ID_REUSED`: allocation-free, budget-free, original untouched |
| Ids differing only in spelling/case | distinct ids (exact comparison); each is charged |
| Another player's session or id | key includes the authenticated player: no sharing or interference |
| Arrival order | replay results never depend on order; which ids fit within N depends only on the order of *first sights by the same player*, a deterministic function of the call sequence, never of time or hash order. Players' budgets are independent |
| Forcing a budget refill | only by the round advancing (a game-state transition) |

## 5. Memory bounds

`Record` is 56 B (`id` 32 B incl. SSO for <= 15 characters, plus 24 B of exact payload and response
fields). An id over 15 characters adds one heap block (<= ~80 B). Bound = players x rounds x N x record.
"Short" = ids <= 15 chars (what a conforming client uses); "long" = hostile 64-char ids.

| Players | Rounds | N | Short ids | Long ids (hostile worst case) |
|---|---|---|---|---|
| 4 | 12 | 32 (default) | 86 KB | 209 KB |
| 4 | 12 | 8 (min) | 21 KB | 52 KB |
| 4 | 12 | 128 (max) | 344 KB | 836 KB |
| 4 | 99 | 32 | 710 KB | 1.7 MB |
| 4 | 12 | conforming client, ~5 ids/round | 11 KB | - |

The H2 cache (64/player, ~160-190 B each) cost up to ~49 KB per match *with the guarantee broken*. Hosts
for which these numbers are too large lower `request_budget_per_round` or `rounds`. Measured on the implementation: a record is 56 B; a 64-character id adds a 65 B heap block
(121 B/record before allocator overhead). Records live in a per-player `std::deque` so growth has no 2x
vector slack; container overhead is about 10% on top of the table. Lookup is a linear
scan of one player's records (<= N x rounds = 3 168 at the extremes): microseconds.

## 6. Protocol clarifications and tightenings (botched/1)

* New codes `REQUEST_ID_REUSED` and `REQUEST_LIMIT` (additive; refusals of input outside the conforming set).
* A reused id with a **different** payload used to replay the original; it is now refused (D2).
* A retry older than 64 ids is no longer processed afresh: it replays the original.
* Retry-after-close replays rather than `PLANNING_CLOSED` (this is today's behaviour while cached; now unconditional).
* New ids are rationed: at most N per player per round.
* A rejection is replayed too, including a stale "too early" `WRONG_ROUND`; the client must use a fresh id.
* After a checkpoint restore the store is empty (D8).
* Gameplay, resolution order, events, `state_version` and state hashes are unchanged. Replays emit nothing and bump nothing.

## 7. Alternatives considered (summary)

| Model | Late-retry guarantee | Memory | Verdict |
|---|---|---|---|
| LRU 64/player (H2) | broken after 64 | ~49 KB | rejected: violates the MUST (S5) |
| Unbounded cache | absolute | unbounded | rejected: memory DoS |
| Larger LRU (1024) | broken after 1024 | ~780 KB | fallback only |
| **No eviction + per-round budget + exact records** | **absolute** | **bounded constant (section 5)** | **adopted** |
| Keep only last K rounds | K rounds | small | weaker guarantee; not adopted |
| Success-only recording | absolute for accepted | smaller | deviates for rejections; not adopted (D4) |
| Per-player sequence numbers | strongest | tiny | changes the contract (botched/2); see `docs/binary-protocol-requirements.md` |

## 8. Relationship to the M1 binary protocol

The binary protocol may offer a compact per-player request sequence number instead of an ASCII id
(`docs/binary-protocol-requirements.md`). This contract is **not** changed to anticipate it: the
idempotency store keys on `(player, request_id)` and a binary adapter must map its sequence number to
the same keyspace (same single-use, replay, `REQUEST_ID_REUSED` and budget semantics). How that mapping
is spelled is an open M1 design choice.

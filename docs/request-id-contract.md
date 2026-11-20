# Request-ID idempotency contract — proposal for approval (M0-H3)

**Status:** PROPOSAL. Nothing in this document is implemented. `botched/1` rules and wire
format are unchanged; the code still behaves as described in "Observed today".
**Authority:** spec section 9.2 (quoted below), `docs/protocol-limits.md`, `docs/rules-interpretations.md` #17-18.

## 1. What the spec requires

> `SUBMIT_PROGRAM` is accepted only for the currently connected, authenticated player's slot,
> while planning that round, unless `ready` is already true. **Repeated identical `request_id`
> MUST return the original response idempotently.** Revisions lower than an accepted revision
> MUST be rejected; equal revision with different contents MUST be rejected. **Commands
> received after planning closes MUST be rejected without changing results.**

Also (section 10): clients must "submit/retry safely"; section 1: no persistent connection or
synchronised clocks are assumed. Retry policy and transport belong to NIO infrastructure.

Two readings need a decision, because the spec is silent or ambiguous:

* **"identical `request_id`"**: does a retry mean *same id*, or *same id and same payload*?
* **Retry after planning closes**: is a retry of an *accepted* request a "command received after
  planning closes" (reject), or a replay of the original answer?

## 2. Observed today (H2 state, measured)

Scenarios run against the current `Match` (scratch program; results reproduced verbatim):

| # | Scenario | Result today | Verdict |
|---|---|---|---|
| S1 | Retry, same id and payload | original response | correct |
| S2 | **Same id, different payload** (client bug) | original response returned; the new payload is **silently not applied**, client believes it was accepted | dangerous |
| S3 | Retry after planning closed (id still cached) | original `OK`; a *new* id gets `PLANNING_CLOSED` | correct (see 4.3) |
| S4 | Retry after the round advanced (id still cached) | original `OK`; a new id gets `WRONG_ROUND` | correct |
| S5 | **Retry after >64 later ids** from the same player | `PROGRAM_LOCKED` — an *accepted* command is now reported as *failed* | **violates the MUST** |
| S5b | Same, original accepted unready, round advanced | `WRONG_ROUND` | **violates the MUST** |
| S7 | Rejected "too early" (round+1), retried with the same id once the round is open | stale `WRONG_ROUND` forever; only a fresh id works | spec-literal, needs documenting |

Why S5 is not just an abuse case:

* The evicting entries can be **cheap rejected requests** (S5 used `WRONG_ROUND`/`LOCKED` junk),
  so a buggy client evicts its own valid history.
* A *conforming* client can reach 64: one revision per slot edit is about 4 ids a round, 48
  over 12 rounds, plus any additional retries under fresh ids. 64 is not a comfortable margin.
* The harm is the worst kind: the server tells a client that a command which **succeeded**
  failed, so the client may re-plan against a wrong belief.

Cost today: `SubmitResult` is 88 B; a cache node is 128 B payload plus about 32 B of tree
header, about 160-190 B per entry. The cap of 64/player is about 49 KB worst case per 4-player match.

## 3. Requirements for a FujiNet-appropriate model

1. **Never break the MUST for a request the server accepted for processing.** If the server
   cannot honour the guarantee it must *refuse the new id up front*, never *forget an old one*.
2. **Memory has an absolute, small, configuration-derived bound**; independent of how many
   requests an attacker sends. Unauthenticated or malformed traffic must not touch the store.
3. **Cheap for constrained hosts.** Match lifetime is at most `rounds` rounds (default 12, max 99).
4. **No `botched/1` change.** Conforming clients must see exactly the same behaviour as today.
5. **A client bug must fail loudly, not silently drop a programme** (S2).

## 4. Alternatives

Memory columns assume 4 players; "legit" is a conforming client using 2-5 ids/round
(about 100-240 entries per match); "worst" is a hostile or buggy client of its own slot.

| | Model | Late retry guarantee | Memory worst (12 rounds / 99 rounds) | `botched/1` compatible | Abuse resistance | Verdict |
|---|---|---|---|---|---|---|
| **A** | LRU, 64 per player (H2, current) | **broken** after 64 ids | ~49 KB (fixed) | yes | memory ok; evicts valid entries | rejected: violates MUST (S5) |
| **B** | Unbounded full-response cache (pre-H2) | absolute | unbounded | yes | **none** (memory DoS) | rejected |
| **C** | LRU, larger (e.g. 1024 per player) | broken after 1024; practically unreachable by conforming clients | ~780 KB per match | yes | ok but still a silent failure mode | fallback only |
| **D** | **No eviction; per-player *per-round* budget of new ids; compact records** | **absolute for the match** | **~48 KB / ~400 KB** at N=32 (24 / 200 KB at N=16); legit about 6 KB | yes (additive refusal) | memory bound is a constant of `players x rounds x N`; a spammer only locks out *its own slot for the rest of that round* | **recommended** |
| **E** | Retain only the last K rounds | only for K rounds, then fresh processing (`WRONG_ROUND`) or an `EXPIRED` code | ~8 KB (K=2) | yes (new code) | good | acceptable if memory is critical; guarantee weaker |
| **F** | Cache successes only (D, but rejections are not recorded or budgeted) | absolute for accepted commands; rejected ones re-evaluated | smaller than D | **deviates** from "original response" for rejections | rejection spam is free | alternative to D, see 5.4 |
| **G** | Replace ids by per-player sequence numbers | strongest | tiny | **no: changes the contract** (botched/2 material) | best | out of scope |

### 4.1 Why D beats the others

* A, C: eviction is inherently a silent failure. D converts it into an explicit, harmless refusal
  (`REQUEST_LIMIT`), which a client can see and handle, *before* the id is accepted.
* B: D is B with a hard cap, expressed as a budget instead of a cache size.
* E, F: weaker guarantee for little memory saved; both remain available if the budget/memory
  trade-off in 6 is rejected.

### 4.2 Compact records (what makes D cheap)

A replay is fully reconstructable from a small record because the server response is a pure
function of `(outcome code, accepted-vs-unchanged flag, revision, state_version)` plus the retry's
own echoed fields:

| Field | Bytes |
|---|---|
| key digest (64-bit hash of `request_id`; per player) | 8 |
| payload digest (64-bit hash of canonical `round,revision,orders,ready`) | 8 |
| `state_version` of the original response | 4 |
| `revision` | 4 |
| outcome code + flag | 2 |
| padding | 6 |
| **Total** | **32** |

vs about 160-190 B today. Verbatim ids (+up to 64 B each) are possible if collision-proofing is
preferred: 3x cost. A 64-bit digest collision can only confuse requests *within one authenticated
slot*, i.e. harm the colliding client itself, never another player.

### 4.3 The "after planning closes" question

Recommended reading: a retry of an *already-accepted* request is **not a new command**; it is a
replay of an answer, with no state change, events or version bump. It therefore satisfies
"rejected without changing results" *and* the idempotency MUST. A **new** id after close is
rejected `PLANNING_CLOSED` exactly as today.

The alternative reading (a retry after close also gets `PLANNING_CLOSED`) makes a client whose
`ready=true` acknowledgement was lost conclude its programme failed. It was in fact applied (S3).
Both readings leave the game state identical; the recommended one avoids the false failure. The
guidance for clients in either case: after `PLANNING_CLOSED`, fetch `GET_SNAPSHOT` for the truth.

## 5. Recommended contract (for approval)

Normative text if approved (applies to `SUBMIT_PROGRAM`; other requests are naturally idempotent
reads or lobby operations and keep no history):

1. **Scope.** A request is *keyed* if it has a non-empty `request_id`, a valid session for an
   existing slot, and passes the H2 field validation. Unkeyed, unauthenticated or malformed
   requests never read or write the idempotency store.
2. **Single-use ids.** The key is `(player, request_id)`. An id identifies one command. A retry
   resends the same command; a *new* command (including re-sending after a rejection, or for a
   later round) uses a **fresh** id.
3. **First sight.** The command is decided by the rules. Its outcome, accepted or rejected, is
   recorded for the rest of the match.
4. **Retry** (same key, same payload digest): returns the **original** response, byte-for-byte
   (`ok`, `code`, `message`, `revision`, `state_version`). No state change. Checked *before*
   the phase, round and lock rules, so it holds after planning closes, after the round advances
   and after the programme is locked.
5. **Same id, different payload** -> `REQUEST_ID_REUSED`, `ok:false`, no state change; the
   original record is untouched and still replayable. *(Decision D2 below.)*
6. **Budget.** At most **N = 32** new keys per player per round (the round in which the key was
   first seen). A 33rd new key in a round -> `REQUEST_LIMIT`, `ok:false`, **not recorded**, no
   state change; the same id may be presented again in a later round and is then processed
   as new. Replays and rejections 4/5 do not consume budget.
7. **No eviction.** Records live until the match ends. Absolute bound: `players x rounds x N x 32 B`
   (48 KB default, 396 KB at the 99-round maximum).
8. **Persistence.** The store is not part of `Match::save()` yet. After a host restart a retry
   is processed as a new command (still safe: identical content is an accepted no-op while
   planning, a `PROGRAM_LOCKED`/`WRONG_ROUND` answer otherwise). Recording the compact store in the
   checkpoint is cheap (32 B entries) and belongs to M1-02.

### 5.1 Behaviour delta from H2 state, and compatibility

| Case | H2 today | Proposed |
|---|---|---|
| retry within the cache | original | original |
| retry after eviction (S5) | **wrong failure** | original (no eviction) |
| same id, new payload (S2) | original, new payload silently dropped | `REQUEST_ID_REUSED` |
| flood of unique ids | oldest evicted | refused beyond 32/round; nothing forgotten |
| new codes | none | `REQUEST_ID_REUSED`, `REQUEST_LIMIT` |

`botched/1` compatibility: a client that follows 9.2 (unique ids, retries with an unchanged
payload) sees **no difference** except that the S5 defect disappears. The two new codes are
refusals of input outside that conforming set. If new codes are unwelcome, both can be reported
as `BAD_REQUEST` with distinct messages.

### 5.2 Test plan on approval

Verify the replay is byte-identical to the original for every outcome code; retry after
close/advance/lock/finish; 10 000 unique ids -> memory flat after the budget; eviction can no
longer happen (the S5 test inverted); budget resets next round; `REQUEST_LIMIT` leaves state
and records unchanged; digest sensitivity (each of round/revision/orders/ready differing -> `REQUEST_ID_REUSED`);
rejections recorded and replayed; unauthenticated/malformed traffic leaves the store untouched;
cross-player isolation of identical ids; a seeded fuzz that every retry equals its original.
Update `protocol-limits.md` (the 64/player row) and `rules-interpretations.md` #17.

### 5.3 Size of the change

Small and local: `Match` request store (replace the map+deque), response reconstruction,
two codes, server mapping and docs. No rules, engine, wire-schema or state-hash change.

### 5.4 Variant F (success-only recording), for comparison

Recording only accepted outcomes makes rejection spam free and lets a client retry a "too early"
rejection (S7) with the same id. The cost is a literal deviation: a retry of a *rejected* request
could return something different from its original response. Given the spec's unconditional
wording, D (record everything) is the safer reading; F is the fallback if lock-out of a buggy
client's own slot (budget exhaustion) is considered worse than the deviation.

## 6. Decisions requested

| # | Decision | Recommendation |
|---|---|---|
| **D1** | Adopt model D (no eviction, per-round budget, compact records) in place of the H2 LRU | **Yes** |
| **D2** | Reused id with a different payload: reject (`REQUEST_ID_REUSED`) or return the original | **Reject**: fail loudly (S2) |
| **D3** | Retry of an accepted request after planning closes: replay the original, or `PLANNING_CLOSED` | **Replay** (4.3) |
| **D4** | Record rejections as well as acceptances | **Yes** (spec-literal); F is the fallback |
| **D5** | Budget N per player per round | **32** (>=6x a conforming client); a `MatchConfig` setting |
| **D6** | 64-bit digests (32 B records) vs verbatim ids (about 96 B) | **Digests**; collisions can only affect the colliding slot |
| **D7** | New codes `REQUEST_ID_REUSED`, `REQUEST_LIMIT` vs reuse `BAD_REQUEST` | **New codes** (clients can react) |
| **D8** | Checkpoint the store | **Defer to M1-02**; documented limitation |

## 7. Out of scope here

Anything in H4 (interpretations freeze) or M1 (host recovery, transport, rate limiting beyond
this budget); changing `request_id` to sequence numbers (botched/2).

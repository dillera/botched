# Protocol input limits (botched/1)

All limits apply at the **untrusted boundary**: `GameServer::handle_text()` and the stdio
host. They are explicit constants (`server/server.hpp`, `include/botched/match.hpp`) and are
sized for constrained FujiNet clients: the largest legal request, `SUBMIT_PROGRAM` with a
64-character `request_id` and a 64-character `session`, is about 350 bytes.

None of them changes gameplay or the `botched/1` rules. Requests that were valid before are
still valid; limits only refuse input that could not have come from a conforming client or
that previously wrapped, defaulted silently or grew memory without bound.

## Request text

| Limit | Value | On violation |
|---|---|---|
| Request size (`kMaxRequestBytes`) | 2048 bytes | `REQUEST_TOO_LARGE` (checked before parsing) |
| Stdio line length | 2050 bytes (+CRLF); the rest of an over-long line is discarded, never stored | `REQUEST_TOO_LARGE`, next line is read normally |
| JSON nesting | 4 containers | `BAD_REQUEST` |
| Members per object | 16 | `BAD_REQUEST` |
| Elements per array | 8 | `BAD_REQUEST` |
| String length | 256 bytes | `BAD_REQUEST` |
| Total JSON values | 64 | `BAD_REQUEST` |

The JSON dialect is strict: integers only with `|n| <= 2^53` and no leading zeros; strings are
printable ASCII (raw control bytes, DEL and non-ASCII are refused, as are `\u` escapes above
`0x7f`); **duplicate object keys are refused** (no last-key-wins ambiguity).

`Json::parse()` called without limits (maps, checkpoints, responses) uses generous defaults
(4 MiB, depth 32, 65 536 elements, 4096 members, 4096-byte strings, 1 Mi values). Those
documents are trusted-size; the defaults still stop pathological input from exhausting memory.

## Fields

A field that is present must have the right type and range. It is never replaced by a default
and a 64-bit number is never narrowed into a valid-looking 32-bit one. Violations give
`BAD_REQUEST` and change nothing.

| Field | Rule |
|---|---|
| `type` | 1-32 chars of `[A-Za-z0-9._:-]` |
| `match_id` | 1-16 chars, same alphabet |
| `map_id` | optional, 1-32 chars, same alphabet |
| `request_id` | optional, 1-64 chars, same alphabet (`""` = no idempotency key) |
| `session` | string; compared without early exit; mismatch is `UNAUTHORIZED` |
| `player_count` | optional integer 2-4 |
| `rounds` | optional integer 1-99 |
| `planning_seconds` | optional integer 0-86400 (0 = no timer) |
| `player_id`, `round`, `revision`, `since_version` | integers that fit in 32 bits; in-range but meaningless values keep their rules-level answers (`UNKNOWN_PLAYER`, `WRONG_ROUND`, `INVALID_REVISION`, `SNAPSHOT_REQUIRED`) |
| `ready` | optional boolean |
| `orders` | array of exactly 3 strings (`INVALID_ORDERS` otherwise; unknown tokens as before) |

Error responses echo `request_type` and `request_id` only when they are well-formed
identifiers; arbitrary client text is never reflected.

## Server and match memory

| Limit | Value | Notes |
|---|---|---|
| Hosted matches (`ServerConfig::max_matches`) | 16 | `TOO_MANY_MATCHES`. Spec 14 lists this as a deployment setting. Rooms are not yet reclaimed (M1). |
| Players per match | 4 | |
| Request ids remembered per player (`kRequestCachePerPlayer`) | 64 | Oldest forgotten first. A retry that old is handled as a new request and is normally refused (`WRONG_ROUND`, `PLANNING_CLOSED`, `STALE_REVISION`). Requests for non-existent players are never remembered. |
| Retained event history | 512 state versions | Older: `SNAPSHOT_REQUIRED` (unchanged). |

## Not covered here (deliberately)

- Network-level concerns: connection counts, rate limiting, slow-loris, authentication of the
  lobby operations (`CREATE_MATCH`, `JOIN_MATCH`, `START_MATCH`). The stdio host is a reference
  harness, not a production server; these belong to the NIO transport (M1).
- Reclaiming finished matches (M1).

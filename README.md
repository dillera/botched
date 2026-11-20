# Botched!

Server-authoritative, deterministic multiplayer factory game for FujiNet-NIO clients.
Rules: [`docs/botched-game-spec-v1.md`](docs/botched-game-spec-v1.md) (authoritative).
Where the spec was silent: [`docs/rules-interpretations.md`](docs/rules-interpretations.md).

## Layout

| Path | Layer | Depends on |
|------|-------|------------|
| `include/botched/`, `src/` | **Rules**: map, state, engine, match lifecycle, canonical JSON. No clocks, sockets, rendering or platform code. | C++20 stdlib |
| `server/` | **Orchestration**: rooms, sessions, planning deadlines, the logical API of spec 9.2. JSON in, JSON out. | rules |
| `apps/botched_server_main.cpp` | Host: JSON lines over stdin/stdout. The seam where a NIO channel adapter attaches later. | server |
| `apps/botched_sim.cpp`, `bot.*`, `ascii_render.*`, `sim_runner.*` | **POSIX reference app**: four simulated players play a full match through the public API. | server |
| `tests/` | doctest suites mapped to spec section 11. | all |

Transport (SLIP/FujiBus/NIO) and platform clients are *not* part of this repository's current
scope; they are milestone M1/M2 and sit above `GameServer::handle()`.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure     # or ./build/botched_tests
```

## Run a match with no clients connected

```sh
./build/botched-sim                      # 4 players, 12 rounds, seed 1
./build/botched-sim --verbose --seed 3   # ASCII board after every round
./build/botched-sim --events             # full event stream, one JSON object per line
./build/botched-sim --flaky 25 --check   # simulate missed rounds; verify byte-identical re-run
```

## Headless server

```sh
./build/botched-server --planning-seconds 60
{"type":"CREATE_MATCH","player_count":2}
{"type":"JOIN_MATCH","match_id":"m1"}          # repeat; the last join starts the match
{"type":"SUBMIT_PROGRAM","match_id":"m1","player_id":1,"session":"<from JOINED>","round":1,
 "revision":1,"orders":["FORWARD_2","TURN_RIGHT","TOGGLE"],"ready":true,"request_id":"c-1"}
{"type":"GET_SNAPSHOT","match_id":"m1"}   {"type":"GET_EVENTS","match_id":"m1","since_version":0}
{"type":"GET_MAP","match_id":"m1"}        {"type":"GET_MATCH_STATUS","match_id":"m1"}
```

`CREATE_MATCH`, `JOIN_MATCH`, `START_MATCH` are lobby operations outside the spec's section 9.2
list. Session tokens here are reference-grade; real authentication belongs to NIO infrastructure.

## Request ids and idempotency

`SUBMIT_PROGRAM` retries are idempotent for the life of a match, with a per-player per-round budget of new
ids instead of any eviction. Contract, rationale and memory bounds: [`docs/request-id-contract.md`](docs/request-id-contract.md).
The retro-client binary wire protocol (M1) is specified as requirements in
[`docs/binary-protocol-requirements.md`](docs/binary-protocol-requirements.md).

## Untrusted input

Request size, JSON shape, field types/ranges, identifier alphabets and memory bounds are
explicit and documented in [`docs/protocol-limits.md`](docs/protocol-limits.md).

## Checkpoints

`Match::save()` / `Match::restore()` serialise a match including secret programmes. `restore()`
treats the checkpoint as untrusted: it validates the configuration, header (`protocol`,
`rules_version`, `match_id`, board), the world (identities, positions/occupancy, carrying
relationships, groups, phase/round/tick), programme records, version and event cursor, all
*before* building anything, and rejects with a reason on any failure. A rejected checkpoint
leaves no partially restored `Match`. This is a single-match checkpoint, not host persistence
(rooms, sessions, deadlines and request-id history are M1).

## Determinism contract

`Match` takes commands and produces canonical state plus events; nothing in it reads a clock or
depends on arrival order. The server decides *when* planning closes. The tests assert that
identical programmes give identical `state_hash`/event streams however they arrive, however the
ticks are paced, and across save/restore.

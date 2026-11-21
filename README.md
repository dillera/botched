# Botched!

Server-authoritative, deterministic multiplayer factory game for FujiNet-NIO clients.
Rules: [`docs/botched-game-spec-v1.md`](docs/botched-game-spec-v1.md) (authoritative).
Where the spec was silent: [`docs/rules-interpretations.md`](docs/rules-interpretations.md).

## Status

**M0 (pure rules engine) is complete and accepted** (see `docs/m0-hardening.md`): a deterministic,
headless C++20 rules library, a transport-agnostic `GameServer`, a stdio JSON host and a POSIX
reference simulator, covered by tests for all 14 acceptance categories of the spec (clean under
ASan/UBSan; identical results in Debug and Release).

**M1-A (minimal playable POSIX multiplayer) is built:** a TCP server and an interactive terminal
client play a complete 2-4 player match (see "Play a match" below). **Not built yet:** the NIO
transport, retro clients and the binary wire protocol (`docs/binary-protocol-requirements.md`),
reconnection, authentication beyond per-seat session tokens, and host persistence.

## Play a match (2-4 players, separate terminals)

Build once (`cmake -S . -B build && cmake --build build -j`), then:

```sh
# Terminal 1: the server (127.0.0.1:7878, 60 s planning per round)
./build/botched-tcp-server
#   --port N  --planning-seconds N  --bind 0.0.0.0   (LAN play: see the warning below)

# Terminal 2: create a match for 3 players and join it as P1
./build/botched-client --create --players 3
#   prints:  Match m1 created ...   Others join with:  botched-client --join m1 ...

# Terminals 3 and 4: join (use --host and --port when the server is on another machine)
./build/botched-client --join m1
```

The match starts when all seats are filled (or type `start` in the creating client to begin with the
players who have joined; at least 2). Each round, every player has the planning time to enter **three
orders** and lock them in:

```
F2 R T        type three orders separated by spaces, then Enter (type again to change them)
go            lock in: the round runs as soon as everyone is locked in, or when the timer ends
```

| Code | Order | Code | Order |
|---|---|---|---|
| `F` / `F2` | forward 1 / 2 | `U` | turn around |
| `B` | back 1 | `D` | drop the crate |
| `L` / `R` | turn left / right | `T` | toggle the console next to you |
| `W` | wait | | |

Other commands: `help`, `show` (redraw), `quit`. Players who do not lock in before the timer simply
`WAIT` (the programme you saved but did not lock is used). The screen shows the factory, robots, crates,
scores, who is ready, the time left, and what every robot did last round. When round 12 ends the client
prints the final scores and the winner (ties are shared victories, per the spec).

`botched-client --bot ...` plays automatically through the same request path (useful for filling seats,
demos and tests); `--plain` disables screen redraws (also the default when output is not a terminal).

**Reading the board.** Every cell is 3 characters wide. A robot (`1>` = player 1 facing east, a trailing
block/`*` if it carries a crate) *replaces* the tile it stands on; the status line under the board names
the tile under your robot (for example "conveyor heading E ... it will move you at the end of every
tick"). Loaded dispensers are `G1`, empty ones `g1`; the legend lists belt groups (normal/reversed) and
which dispensers hold a crate. The "what happened" log narrates each tick in the server's own order:
`P1 F2: drives ...` for your orders, `belt moves P2 ... (they ordered WAIT)` for the conveyor phase,
`pushes`, `is blocked`, turns, pickups and console toggles. Colour and Unicode blocks are used on a
terminal; `--ascii` forces the plain ASCII board (the default when the locale is not UTF-8) and
`--no-color` (or `NO_COLOR`) disables colour.

### Graphical client (SDL2)

`botched-client --ui ...` takes the same `--create` / `--join` arguments as the terminal client but opens a
window: the factory board with drawn sprites (player-coloured robots, docks and spawn pads; animated belts
coloured by group; dispensers that show whether they hold a crate; consoles, pits, walls), a scoreboard,
a "your robot" panel, order entry and a **scrollable** "what happened" log, each in its own panel so a long
log never pushes the board off screen. Click an order (or press `F` `2`=F2 `B` `L` `R` `U` `D` `T` `W`) to
fill the three slots; they save when complete; `Enter` or LOCK IN commits. Hover a tile for its description.
Scroll the log with the mouse wheel, `PgUp`/`PgDn`, `Home`/`End` or by dragging its bar; `Esc` quits.
`--bot` makes the window play automatically (`--think-ms N` slows it down for demos). It needs SDL2 and
SDL2_ttf at build time (found automatically; disable with `-DBOTCHED_SDL=OFF`) and a TrueType font
(DejaVu Sans, or set `BOTCHED_FONT`). Headless check: `SDL_VIDEODRIVER=dummy botched-client --ui --frames 300
--screenshot out.bmp ...`.

**Limits of this MVP:** there is no authentication beyond the per-seat session token, so keep the
server on `127.0.0.1` or a trusted LAN; a client that quits or crashes cannot rejoin (its robot waits
every round); one server hosts up to 16 matches; no TLS.

Tests: `ctest --test-dir build` (the TCP host and a full match over real sockets are in `tests/test_tcp.cpp`).

## Layout

| Path | Layer | Depends on |
|------|-------|------------|
| `include/botched/`, `src/` | **Rules**: map, state, engine, match lifecycle, canonical JSON. No clocks, sockets, rendering or platform code. | C++20 stdlib |
| `server/` | **Orchestration**: rooms, sessions, planning deadlines, the logical API of spec 9.2. JSON in, JSON out. | rules |
| `apps/botched_server_main.cpp` | Host: JSON lines over stdin/stdout. The seam where a NIO channel adapter attaches later. | server |
| `server/tcp_host.*`, `apps/botched_tcpd_main.cpp` | **TCP host**: single-threaded `poll()` loop around `GameServer`; advances planning deadlines without requests. | server |
| `apps/client_core.*`, `apps/botched_client_main.cpp` | **Terminal client** (and `--bot` auto-play) over the JSON API. | server, sim lib |
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

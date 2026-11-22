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

### Maps

Maps are plain files in `resources/maps/*.map` (shipped: `training-01`, the spec's example, `factory-01`, `crossroads-01` the G2-A playtest map `trapworks-01` and the laser map `laserworks-01` and the G2-B turret map `turretworks-01`).
The server loads and validates every `.map` file at startup and reports problems per file on stderr; valid
maps stay available when another file is bad, and duplicate ids are rejected. Adding a map needs no rebuild:
drop a file in the directory and restart the server.

```
; id: my-map                 required, unique, [A-Za-z0-9._:-]
; name: My Map               required, shown to players
; description: One line.     optional
; players: 2-4               optional (N or A-B), default 2-4
#  #  #  ...                 then the tile rows exactly as in the spec: # . O S1 D1 G1 C> C<B T TA L> X1 K1 ...
```

Where the server looks (first match wins): `--maps-dir DIR`; `$BOTCHED_MAPS_DIR`; next to the executable
(`<exe dir>/resources/maps`, the installed layout `<exe dir>/../share/botched/maps`, or `<exe dir>/../resources/maps`
for a build tree inside the source tree); the source tree's `resources/maps` baked in at build time; `./resources/maps`.
So a build tree, an installed tree (`cmake --install build --prefix /opt/botched`) and `--maps-dir` all work from any
working directory.

```sh
./build/botched-tcp-server --maps-dir /srv/botched/maps       # optional
./build/botched-client --list-maps                            # id, name, players, description
./build/botched-client --create --map-id factory-01 --players 3
./build/botched-client --join m1                              # joiners need no local map files (GET_MAP)
```

`LIST_MAPS` (no fields) returns `{"type":"MAPS","ok":true,"maps":[{"map_id","name","description","min_players",
"max_players"}...]}` in ascending id order; `CREATE_MATCH` takes an optional `"map_id"` (default `training-01`).
Maps are immutable once registered and every match holds its own reference, which is what a future runtime
"register a map" feature would build on (see the backlog, B-12); none of that exists yet.

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

**Bots.** `--create --players 4 --bots 3` fills three seats with server-managed bots (no extra processes);
any mix works as long as at least one seat is human (e.g. `--players 4 --bots 2`, the other human runs
`--join ID`). Bots plan from the public snapshot with a short look-ahead on the real rules engine, make
occasional mistakes, and are deterministic for a given server `bot_seed`.

### Proximity traps (G2-A)

Maps may contain `X<n>` (a trap device, solid like a wall) and `K<n>` (its control console). A trap starts
**armed and neutral**. In each tick's machinery phase (after orders, belts and lasers) an armed trap with at
least one robot on its eight surrounding tiles fires once: every robot there takes 1 damage, the trap's owner
included, and the trap disarms. A robot that is next to a `K<n>` console and executes `T` arms (or re-arms)
trap `n` and becomes its **owner**; re-arming an armed trap only changes the owner. Damage an owned trap deals
to an *opponent* scores for the owner (+1 per damaged opponent, +3 more when the hit disables them); neutral
and self-inflicted damage scores nothing. `score = 5 x deliveries + sabotage`, and sabotage counts toward a
score target. Maps without `X`/`K` tokens, and `botched/1` itself, are unchanged: the new state (`traps`,
per-player `sabotage`) and events (`TRAP_ARMED`, `TRAP_TRIGGERED`, `TRAP_HIT`, `SCORE_AWARDED`) only appear on
maps with traps, and `ROBOT_DISABLED` gains `cause: "TRAP"`. In the SDL client an armed trap tints its eight
tiles in the owner's colour, fires with a flash, shows `-1` on each victim and a gold `+N` for the owner, and
the scoreboard lists `sabotage +N`. Server bots do not use traps yet (that is G4). Try it:

```sh
./build/botched-client --ui --create --players 2 --map-id trapworks-01
```

### Charging laser turrets (G2-B)

Maps may contain `Y<n>` (a turret, solid) and `P<n>` (its control pad). A turret is **neutral** at the start
and charges on its own for two ticks, after which it is **READY**. A READY turret fires once, on the first
machinery phase in which a robot is within two tiles north, east, south or west of it (a wall stops the shot; the
nearest robot wins, ties go N, E, S, W), for 1 damage, and then recharges from 0. With nobody in range it simply
stays READY, across rounds. A robot that **enters** a `P<n>` pad claims the turret; a rival
can take it by entering a pad later, and neither affects the charge. Scoring matches the traps: +1 for hitting an
opponent of the owner, +3 more for a disabling hit, nothing for self-inflicted or neutral damage. These are new
tokens only: the existing `L>` lasers are unchanged and still neutral, and `turrets` (state) and the
`TURRET_*` events only appear on maps with turrets. The SDL client shows two charge pips and a pulsing READY label on the turret, a faint cross over the tiles in range, the owner's colour, a beam to the
victim with `-1`, a `CAPTURED` ring and the gold `+N`. Try it (server bots do not use turrets deliberately):

```sh
./build/botched-client --ui --create --players 2 --map-id turretworks-01
```

### Score-target matches

`CREATE_MATCH` also takes an optional `"target_score"` (1-1000). With a target the match ends at the end of
the first *complete* round (all three ticks plus round-end effects) in which a player has at least that many
points, and `"rounds"` becomes the maximum-round safety cap (default 100, up to 200). If nobody reaches the
target by the cap the highest score wins. Ties use the existing deliveries/damage tie-breakers, and several
players over the line in the same round are ranked by score, so rotating tick priority never decides it.
Without a target nothing changes: 12 fixed rounds and identical hashes. `MATCH_ENDED` carries
`"reason":"target"|"cap"` in target mode, and snapshots/status carry `target_score`.

```sh
./build/botched-client --ui --create --players 3 --bots 2 --map-id crossroads-01 --target-score 30
./build/botched-sim --players 3 --map-id crossroads-01 --target-score 30 --check
```

### Graphical client (SDL2)

`botched-client --ui ...` takes the same `--create` / `--join` arguments as the terminal client but opens a
window: the factory board with drawn sprites (player-coloured robots, docks and spawn pads; animated belts
coloured by group; dispensers that show whether they hold a crate; consoles, pits, walls), a scoreboard,
a "your robot" panel, order entry and a **scrollable** "what happened" log, each in its own panel so a long
log never pushes the board off screen. Click an order (or press `F` `2`=F2 `B` `L` `R` `U` `D` `T` `W`) to
fill the three slots; they save when complete; `Enter` or LOCK IN commits. Hover a tile for its description.
When a round resolves the window **replays it**: an execution queue under the board (`P1(1) P2(1) ⚙ P2(2) ...`,
⚙ = conveyors/hazards phase) highlights the live instruction while the board animates the server's own events
(moves, turns, pushes, belt rides, blocked moves, pickups, deliveries, pit falls, respawns). `Space` or SKIP jumps
to the final state, `[` / `]` change the speed, `--anim-speed X` / `--no-anim` set it at start. Scroll the log with the mouse wheel, `PgUp`/`PgDn`, `Home`/`End` or by dragging its bar; `Esc` quits.
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

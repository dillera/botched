# Botched! — Server-managed match pool and lifecycle

**Status:** Reviewed and approved for **S1 only**; S2–S4 require separate approval

**Scope:** Botched server orchestration and deployment configuration

**Baseline:** Current `GameServer` / `botched-tcp-server` through Amiga A1c (`f29a959c`)

**Protocol:** Existing JSON and frozen Botched binary v1 remain compatible

## 1. Purpose

A Botched server should start with a configured collection of **joinable game offerings**, rather than requiring a Linux client to issue `CREATE_MATCH` first. The server decides which maps, seat counts, named bot opponents and match rules are offered. A retro client connects to a known server and **joins** one of those matches; it is not responsible for creating it.

An offering persists conceptually across successive match instances. After a game finishes, its results remain inspectable briefly, then the server replaces it with a fresh instance subject to its capacity and retention policy. Eventually, a lobby will present these offerings and coordinate entry, but neither the lobby nor a game-specific client needs to implement match creation.

This specification intentionally separates the immediate **startup provisioning** work from **recycling/presence**, **bot takeover**, and **lobby discovery**. Implementing the first phase must not implicitly undertake the later phases.

## 2. Existing behaviour (verified against current server source)

- `ServerConfig::max_matches` defaults to **16** and limits retained rooms; finished rooms are not currently retired.
- `CREATE_MATCH` creates a room but not a running `Match`. `JOIN_MATCH` admits a human until the available human seats (`capacity - bots`) are filled; the last required human causes `start_room()` to execute.
- A room with three bots and four total seats starts when its first human joins. Bot seats are assigned after the human seats and submit at the opening of a planning round.
- `JOIN_MATCH` rejects an already started match (`ALREADY_STARTED`). Human takeover of a bot's existing seat is not implemented.
- Map metadata already supports `; players: N` and `; players: A-B`; supported counts are checked against map/spawn/dock validation. Thus real **two-player-only maps** need no four-player placeholder spawns.
- `LIST_MAPS` lists map definitions, **not current matches or joinable offerings**. Neither `LIST_MATCHES` nor a server-owned configuration of matches exists.
- There is no explicit leave/presence lease, automatic retirement/replacement, host persistence, or restart recovery for room state.
- Both the JSON and binary listeners use the same `GameServer`; the binary v1 layouts and opcodes are frozen.

## 3. Terms and ownership

| Term | Meaning |
|---|---|
| **Offering / pool slot** | Stable, server-configured definition of a game available to join, e.g. `bots-3-foundry`. Not itself a gameplay state. |
| **Match instance** | Concrete room/match with a unique `match_id`, current players, state and event history. An offering may produce successive instances. |
| **Capacity** | Total participating seats in an instance, humans and bots together, **2–4**, validated for its map. |
| **Human seat** | Seat authenticated by its existing session credential. |
| **Bot seat** | Seat driven by a configured named AI opponent with a fixed personality blend; subject to future takeover policy. |
| **Retention** | Interval after `FINISHED` during which results and history remain available for viewing. |
| **Replenishment** | Retirement of an old instance and creation of a **new ID** from the same offering. |

**Server owns** offerings, instance creation, map selection, admission policy, bot scheduling, retention and replenishment. **Clients own** presentation, input, polling, and their own stored seat credentials. A future **lobby** is a discovery/selection interface to server-owned offerings, not an independent authority over matches.

## 4. Functional requirements

### 4.1 Configured match pool — **Phase S1: implement first**

**S1-01 — Configuration file.** Add an optional server configuration file (suggested CLI: `--config path`). The first implementation may use **JSON** to avoid introducing an unrelated YAML dependency. Preserve existing command-line operation without a config file. Document which CLI settings override file settings, and ensure explicitly supplied CLI options win for overlapping settings.

**S1-02 — Pool declarations.** Configuration defines a server-wide `max_concurrent_matches` and zero or more offerings with:

- Unique machine-readable `id` (stable within a server configuration) and human-readable `name` (e.g. `BOTS 3`).
- Existing `map_id`.
- Number of concurrently provisioned instances (`instances`, positive integer).
- `bots`: **an ordered array of named bot definitions**, length `0 .. player_count-1`. The array length determines the bot count; do not also configure a numeric bot count for a managed offering. Each entry has `name` and a `personality` object (see S1-09).
- `player_count`: inferred when the map declares a **single** allowed count (`; players: 2`); **required** when map metadata admits a range (`2-4`). Do not silently use four seats just because the map supports four.
- Existing match rules: `rounds`, `target_score`, and `planning_seconds`, using the same semantics and limits as `CREATE_MATCH`.

**S1-03 — Provision at startup.** Once maps and configuration validate, create the declared number of **unstarted, joinable rooms** before the TCP listeners advertise readiness. The current `JOIN_MATCH` / BIND / GET_MAP / GET_SNAPSHOT / SUBMIT behaviour must remain compatible. An offering with bots does **not** run an unattended match before a human joins in S1.

**S1-04 — Start policy.** For Phase S1, reuse existing join rules: an empty `bots` array starts when all seats have joined; a nonempty `bots` array starts when `player_count - len(bots)` **human** seats have joined, with the remaining seats controlled by the configured bots in array order. Do not add mid-game bot takeover in S1.

**S1-05 — Publish identifiers.** On successful startup, print each offering's ID, label, map, capacity, bot count, configured bot names, and provisioned **match ID** in a clearly parseable human-readable startup report. These IDs allow `botched-amiga tcp://server:7879 mN` to join without Linux creating the match. Future discovery APIs may replace manual ID entry; do not require them for S1.

**S1-06 — Capacity and errors.** The sum of `instances` cannot exceed configured server match capacity. Validate *all* declarations, map references, counts and rule limits before publishing listeners or creating a partial pool. Fail startup with useful diagnostics rather than silently omitting requested games. Existing `CREATE_MATCH` may remain enabled for development, but it must obey the same global cap and cannot evict configured pool rooms. Document the interaction.

**S1-07 — Backward compatibility.** Without `--config`, existing CLI, `CREATE_MATCH`, Linux JSON/binary clients, Amiga A1, Atari prototypes and map loading must behave exactly as before. S1 introduces no new binary wire records, opcodes, error codes or changes to client-side CREATE behaviour.

**S1-08 — No implicit recycling.** In S1, finished matches are **retained**, as today. This is a deliberately finite development stage: after enough games, pool instances will be consumed. Include a visible warning/diagnostic and cover the limitation in documentation. Do not silently reset a finished room and invalidate its clients.

**S1-09 — Named bot definitions.** Each configured `bots` entry has a nonempty `name` (suggested maximum: 32 printable ASCII characters, unique within its offering), and a `personality` object containing exactly `hauler`, `opportunist`, and `saboteur` weights. Each weight is a finite number in `[0.0, 1.0]`, and their sum must equal `1.0` within a small, explicitly documented floating-point tolerance (suggested `1e-6`); reject absent, negative, NaN, infinite, unknown or non-normalised values. Reject duplicate bot names within one offering. Names may repeat in separate offerings: this permits familiar recurring opponents across matches. Preserve the configured spelling and ordering. Bots occupy the existing highest-numbered player seats, in array order, after human seats.

**S1-10 — Personality blending.** Blend the three **existing** `server/bot.cpp` personality archetypes (not three new independent decision modes) into a fixed set of per-bot behavioural traits, computed once at construction. Blend the existing goal-family weights (`haul`, `mixed/contest`, `hunt`), risk, sabotage reward, blunder, fumble and goal patience. Use explicitly specified deterministic rounding and range rules for integer traits; ensure goal-family probabilities sum to exactly 100 after rounding. A pure `1.0/0.0/0.0`, `0.0/1.0/0.0`, or `0.0/0.0/1.0` must preserve the corresponding existing archetype's decisions for identical map, seed and game inputs. Mixed traits must influence decisions throughout a match, without randomly resampling an archetype each round. Retain deterministic per-instance/per-seat seeding and the established persistent-goal behaviour. No engine or gameplay-rule change.

Existing archetype trait baselines (source: `server/bot.cpp`, current G4 implementation):

| Trait | Hauler | Opportunist | Saboteur |
|---|---:|---:|---:|
| Goal-family weights (haul / mixed / hunt) | 80 / 15 / 5 | 50 / 30 / 20 | 20 / 25 / 55 |
| Risk weighting | 140 | 100 | 70 |
| Sabotage value | 60 | 100 | 180 |
| Blunder / fumble (%) | 6 / 6 | 10 / 8 | 14 / 10 |
| Goal patience (rounds) | 8 | 7 | 5 |

These numbers are AI implementation traits, **not new wire fields**. For example, a `0.2/0.3/0.5` blend yields a haul-goal target of `41%` before deterministic rounding/allocation of all three goal-family percentages.

**S1-11 — Bot identity and compatibility.** A configured bot's name belongs to the server-side opponent identity (logs, traces, startup status, and future lobby display) and stays unchanged for the match. The current frozen binary v1 reports numeric bot seats and legacy archetype metadata, not arbitrary bot names or blended weights. **Do not extend the frozen protocol in S1.** Do not incorrectly label a blended bot as a pure archetype: document how existing `GET_MATCH_STATUS` / binary bot-personality fields are handled for blended bots without inventing wire enum values, changing lengths, or misleading clients. If faithfully representing mixed traits through the existing status contract is impossible, surface names/blends in server-side diagnostics and defer client-visible detailed identity to a separately reviewed metadata/discovery feature. Existing numeric `CREATE_MATCH` `bots` counts and their automatic personality selection must remain backward compatible; the array format applies to configured offerings only.

### 4.2 Finished-instance lifecycle — **Phase S2: follow-up, not part of S1**

**S2-01 — Stable offering, new instance.** Each completed/retired instance is replaced with a **fresh `match_id`**, session/seat state, event history and bot state (while retaining the offering’s configured bot names and personality blends). Never recycle the match ID or its credentials. The configured offering ID persists.

**S2-02 — Results retention.** A finished instance remains readable (status, map, snapshot, event history) until all participating humans have explicitly departed **or** a configurable maximum retention deadline expires. A timeout must prevent an AFK client from holding a pool slot indefinitely. Make the precise clock starts and default times explicit before implementation.

**S2-03 — Presence versus TCP connectivity.** A socket disconnect is **not** automatically a player leave: FujiNet clients may reconnect or reboot. Define explicit leave/release and presence/lease semantics and distinguish transient disconnection from abandonment. Ordinary unauthenticated `GET_SNAPSHOT`/`GET_EVENTS` must not be treated as proof that a seat owner is present.

**S2-04 — Capacity accounting.** Retained finished instances still occupy memory and the global match limit. Replenishment must not exceed the limit; define whether a newly provisioned replacement waits until the old instance is retired (recommended initial policy) versus an explicitly budgeted overlap. The server must remain usable after many successive completions.

**S2-05 — Crash/restart policy.** In-memory instance state is currently lost on process restart. Full server persistence is out of scope unless separately specified; S2 must document that restart creates a fresh pool and invalidates prior match IDs/credentials.

**S2-06 — Protocol boundary.** Define a safe way to release seats and discover the replacement instance. Existing binary v1 has no `LEAVE_MATCH` or `LIST_MATCHES`. Prefer server-side/lobby control APIs where possible. Any proposed on-wire extension must receive **separate explicit review**, not an implicit edit to frozen v1.

### 4.3 Mid-game bot replacement — **Phase S3: separate approval**

**S3-01 — Join a running game.** A newly joining human may claim an eligible bot-controlled seat **without resetting its robot**. Preserve its position, heading, carried crate, score, damage and history.

**S3-02 — Planning boundary.** Bot takeover becomes effective at the **next planning boundary**; bots currently submit their three orders immediately at round opening. Define how outstanding bot submissions and any already-closed round are handled, and prevent simultaneous human/bot control.

**S3-03 — Seat credential and authority.** Issue a proper seat credential and atomically revoke bot ownership for future planning. Preserve H3 idempotency and correct JSON/binary authentication. A transiently disconnected human must not unintentionally lose their seat to another player.

**S3-04 — Human departure.** Specify a policy for voluntary departure, timed-out abandonment and return to bot control. Recommended default: the bot resumes **at a planning boundary**, preserving the robot's state. This requires an explicit seat-presence model and is not a simple modification to `Room::bots`.

**S3-05 — Game policy.** Offering configuration eventually chooses whether bot takeover is allowed and whether human-only games ever begin with substitute bots. S1's named `bots` array merely reserves fixed, named bot seats and must not imply takeover support.

### 4.4 Lobby and discovery — **Phase S4: future**

A future lobby should list server-controlled offerings and the current joinable instance(s), showing labels such as `BOTS 3`, available seats, map, rules, and whether play is in progress. It should request an admission/launch instruction from the server. Game clients then receive only the endpoint and binding information needed to join/play.

For Atari, a standalone `lobby.xex` might launch `botched.xex`, potentially using FujiNet AppStore/AppKey storage to hand over server/match information across reboot. This is **not yet designed or authorised**. Neither Amiga nor Atari should have to implement `CREATE_MATCH` as part of normal play.

## 5. Proposed S1 configuration example

Illustrative JSON — exact field names should be frozen during the S1 review, not extended opportunistically:

```json
{
  "server": {
    "max_concurrent_matches": 8,
    "default_planning_seconds": 60
  },
  "offerings": [
    {
      "id": "bots3-foundry",
      "name": "BOTS 3",
      "map_id": "foundry-01",
      "player_count": 4,
      "bots": [
        {
          "name": "Nasty",
          "personality": {
            "hauler": 0.0,
            "opportunist": 0.0,
            "saboteur": 1.0
          }
        },
        {
          "name": "Sneaky Pete",
          "personality": {
            "hauler": 0.2,
            "opportunist": 0.3,
            "saboteur": 0.5
          }
        },
        {
          "name": "Bob",
          "personality": {
            "hauler": 0.9,
            "opportunist": 0.1,
            "saboteur": 0.0
          }
        }
      ],
      "instances": 2,
      "rounds": 30,
      "target_score": 30,
      "planning_seconds": 60
    },
    {
      "id": "duel",
      "name": "HUMAN DUEL",
      "map_id": "duel-factory",
      "bots": [],
      "instances": 2,
      "rounds": 30,
      "target_score": 0,
      "planning_seconds": 60
    }
  ]
}
```

The first offering intentionally gives each bot a familiar identity rather than selecting anonymous archetypes randomly. The `personality` weights blend existing **hauler**, **opportunist** and **saboteur** behaviours; they are not independent odds of choosing one archetype anew every round. Bot count is `len(bots)` and the remaining seats belong to humans.

The second offering assumes a separately supplied valid map containing `; players: 2`; this map is **not** currently shipped. For an existing map with `; players: 2-4`, set `player_count` explicitly.

For clarity: `target_score = 0` means fixed-length play with `rounds` rounds. Positive target uses `rounds` as its safety cap, matching today's server.

## 6. S1 startup and interaction example

1. Operator starts `botched-tcp-server --config docs/examples/server-pool.json --binary-port 7879 --bind 0.0.0.0` (example only; choose production path outside the repo as appropriate).
2. Server loads maps, validates *all* offerings and prints entries such as `offering=bots3-foundry instance=1 match=m1 map=foundry-01 seats=4 bots=3 names="Nasty,Sneaky Pete,Bob" state=LOBBY`.
3. Amiga connects with `botched-amiga tcp://192.168.1.10:7879 m1`, using the existing A1 application and binary protocol.
4. Server assigns the sole human seat and begins a four-player game with the three configured named server bots.
5. Game finishes. In S1 its match is retained; **S2** later governs retire/replenish.

## 7. Explicit non-goals for S1

No bot takeover, hot-join, auto-replenishment, explicit seat departure, persistence, matchmaking/lobby, discovery wire operation, Atari launcher, Amiga UI changes, new maps/artwork, rule changes, or FujiNet-NIO firmware/library modifications. Do not alter the binary v1 frozen grammar or B1 fixtures. Do not refactor unrelated transport or engine code.

## 8. S1 acceptance criteria

1. **No-config regression:** Existing CLI-driven CREATE/JOIN flows, mixed JSON/binary matches, Amiga A1 acceptance and current test suites remain unchanged.
2. **Pool startup:** An example config provisions multiple valid waiting matches on different maps, with stable offering labels and distinct match IDs shown at startup. `max_concurrent_matches` is enforced.
3. **Human-only waiting:** A two-seat, zero-bot configured offering stays in `LOBBY` after the first JOIN and starts after the second. The server does not start unattended.
4. **Bot match:** A four-seat, three-bot configured offering starts on its first JOIN, accepts the existing Amiga binary client, and completes a match.
5. **Two-player-only map:** A fixture with `; players: 2`, exactly two spawns/docks, and no third/fourth spawns is loaded and provisioned using inferred capacity. Configurations with incompatible player counts fail before listener startup.
6. **Validation:** Unknown map, duplicate offering ID, invalid `instances`, invalid bots array, missing/duplicate/oversized bot name, invalid personality weights or sum, incompatible player count, missing player count for variable-capacity maps, invalid rules, and pool-capacity overflow all fail deterministically with diagnostics and **without partial provisioning**.
7. **Isolation:** Configured matches remain independent: different seat tokens, IDs, events, scores, bot identities/trait state and game states. No finished match is silently reset in S1.
8. **Compatibility:** Both JSON and binary listeners see the same preprovisioned matches; legacy numeric `CREATE_MATCH` bot selection still behaves as before; pure-blend personality choices match existing archetypes for identical seeds. No changed binary layouts, opcodes or server-created hidden client requirements.
9. **Bot identity/behaviour tests:** Verify names/order/seat assignments, pure-weight archetype equivalence (including deterministic replays), several mixed blends with documented trait-rounding results, and identical named opponents in replacement instances when S2 is later implemented. Verify diagnostics do not misrepresent mixed personalities. Include tests for all invalid weights/names and numeric `CREATE_MATCH` compatibility.
10. **Tests:** Add focused deterministic server tests and a real mixed-client integration test (Amiga join via NIO if feasible, otherwise use existing tested binary client plus a justified coverage statement). Verify startup diagnostics and failure exit codes.
11. **Documentation:** Provide a concise operator guide and a documented S1 limitations section. Keep S2–S4 out of the S1 commit.

## 9. Design questions to settle during review (not implementation licence)

- **How is `max_concurrent_matches` defined once finished instances overlap with replacements?** Recommended: cap all resident instances; replenish only after old instance is retired.
- **Should a configured pool permit standalone ad-hoc `CREATE_MATCH`?** Recommended: preserve it for development, subject to the global cap; optional production disabling can be a later setting.
- **Should offerings have stable human-facing IDs or only generated match IDs?** Recommended: both; fixed offering ID, fresh match ID for each instance.
- **If a map supports 2–4, which seat count should be offered?** Recommended: explicit `player_count` rather than guessing.
- **How will we detect finished-game viewers leaving?** Needs S2 presence/release design; never assume a closed TCP connection means leave.
- **What happens when there are no free slots but a replacement is due?** Recommended: postpone replenishment until sufficient capacity is available; report degraded pool status.
- **How will future clients discover current matches without changing binary v1?** Needs S4 separate architecture review.
- **S1 compatibility metadata:** Binary v1 requires an existing archetype value for every bot; it has no absent/mixed value. Retain the existing per-instance/per-seat legacy category in JSON and binary, without mapping blends to a dominant archetype. This frozen compatibility category is not a description of configured behaviour. Names, normalized blends and actual tuning remain server-side. Existing clients may still display the legacy category; faithful client-visible blended identity is deferred. No new fields, opcodes or enum values are introduced.
- **Should a familiar bot name appear identically in several offering instances?** Recommended: yes; uniqueness applies within one offering, while individual seat/instance IDs remain distinct.

## 10. Implementation boundaries and review checkpoints

**Next agent request should implement S1 only.** Keep the spec's later phases as recorded requirements, not permission to begin them. Commit the config parser, startup provisioning, named/weighted bot integration, bounded focused tests, example configuration, and concise operator documentation. Stop for review.

After S1 acceptance, review S2 separately (retention, leave/presence, replenishment), then S3 (bot takeover), then S4 (lobby integration). Each phase should retain and extend the existing functional and resource tests.

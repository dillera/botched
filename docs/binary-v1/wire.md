# Botched binary wire v1 — B1 review candidate

This is the normative B1 layout companion to [the requirements](../binary-protocol-requirements.md).
It freezes a **design for review**, not an implemented protocol. Rules are `botched/1`, the initial
unreleased G1–G4 ruleset, by the owner's decision. Wire major 1, minor 0. No NIO firmware changes.
B2 must implement these bytes or propose a documented revision; it must not fill gaps by guessing.
[Examples](examples.md) and [resource accounting](resources.md) are part of this contract.

## 1. Scalars, transport and validation

All integers are little-endian; signed integers use two's complement. `B`=u8, `H`=u16, `I`=u32,
`J`=i32, `Q`=i64, `b`=i8. A field listing is its byte order, with **no padding**. `S(n)` is a B
byte length followed by that many bytes, maximum n; length zero is allowed only where stated.
Identifiers use the existing ASCII identifier alphabet; credentials are opaque printable ASCII.
Text is printable ASCII as in the existing JSON/map boundary. Booleans are B 0/1. Reserved fields,
unused flag bits and absent fixed slots MUST be zero. Known layouts have exact lengths.

A TCP frame is `H payload_length; payload`. Payload is 3..P bytes, P negotiated in 128..240.
Every payload begins `B opcode; H exchange_id`, offsets 0 and 1. Body starts at offset 3.
The length prefix is not part of payload or codec input. No sync, checksum, varints or escaping.
Unknown opcode/enum/flag/trailing bytes are rejected. Bad framing length closes the connection;
a structurally invalid bounded payload returns adapter BAD_MESSAGE without game dispatch.
A page is structurally validated completely before applying any record. This requires one bounded
scan plus deferred record callbacks (offsets or a second pass over the same packet), **no second
packet or heap**. This explicitly refines the original strict single-pass aspiration: atomic
malformed-packet rejection takes precedence over invoking callbacks as bytes arrive.

Direct TCP and NIO NetworkDevice TCP carry identical frames. NIO uses existing Open/Read/Write/
Info/Close, binary translation disabled. Independent network cursors advance by accepted byte
counts, not requested counts. Prefix/body can cross any read boundary. No unsolicited messages;
one outstanding game exchange, one response frame at a time. A separate configurable binary
listener shares the JSON listener's GameServer. No autodetection on the existing JSON port.

## 2. Negotiation, resumable adapter sessions and seat binding

HELLO is the only request before negotiation, exchange_id=0. Its body is:

| Offset in body | Field |
|---|---|
| 0,1 | B min_major, B max_major (both 1 for this client) |
| 2 | B max_minor (0) |
| 3 | H max_payload (128..240) |
| 5 | B profile (1=K1, 2=K4, 3=K8; section 8) |
| 6 | I features (bits 0..5 all required, value 0x3f) |
| 10 | B resume (0=new, 1=existing) |
| 11 | 16 bytes recovery_secret (all zero for new) |

HELLO_OK body: `B major; B minor; H payload; B profile; I features; 16-byte server_epoch;
16-byte recovery_secret; H last_exchange; H idle_seconds`. Body 45, payload 48 bytes.
Server picks 1.0 and min(client_max,240); profiles/features must be supported exactly. Feature
bits 0=target/cap, 1=traps/sabotage/multi-toggle, 2=turrets, 3=tar, 4=SHUNT,
5=bot labels. All six are mandatory capabilities, not switches that disable gameplay. Other bits
are reserved zero in v1. Encoding is binary by listener selection; JSON needs no new handshake.
No supported intersection returns adapter UNSUPPORTED, with no session or game mutation.

A recovery_secret is 128 random bits from the **server**, not a weak client nonce. It protects
recovery of a lost JOIN/CREATE response. It does not by itself authenticate an arbitrary seat.
At most 64 adapter sessions exist. Sessions expire after 1800 seconds with no valid request;
requests on a live connection renew this idle lease. Expiry/disconnect never removes a room or
seat. A reconnect with resume=1 retains negotiated P/profile/features (must be compatible),
restores binding, last exchange and transfer. Resume requires the identical profile/features and
a client maximum payload at least the previously negotiated P; otherwise UNSUPPORTED with no takeover. It replaces the old connection atomically. Secrets
are compared without early exit. A stale secret returns SESSION_EXPIRED, never silently creates
another session. A lost initial HELLO may leave an unused session until expiry, but cannot leave
a game seat. Server restart changes server_epoch and loses sessions/rooms, as today.

After HELLO, exchange IDs start at 1 and increase by one per **new** request through 65535.
Exactly the last exchange may be retransmitted with byte-identical payload and replayed without
dispatch. Different bytes at that ID give EXCHANGE_REUSED; other IDs give BAD_SEQUENCE. Reserve
reply/cache capacity before any mutation. Keep the last request (<=240 bytes), last response
(<=240), and the associated transfer until replaced/expired. An application response, including
an error, consumes the new exchange; sequence/framing refusals do not. A complete request at the expected ID that receives BAD_MESSAGE
consumes that ID and caches its FAULT; it does not dispatch or discard an existing transfer. HELLO
returns the last processed ID so a client can resend its uncertain request; the client retains
that exact request until answered. This transport-adapter retry rule is independent of H3 and
adds no changes to JSON or Match. To rotate at 65535, first settle the last response, open a new
session, then BIND using the seat credential; never wrap. Lost JOIN/CREATE is recoverable only
within the lease. After lease loss do not repeat an ambiguous lobby mutation: report uncertainty
and require explicit user recovery. No promise of restart persistence is made.

BIND body: `B role; S(16) match_id; J player_id; S(64) credential`. Only role=1 (player) is
implemented. Role=2 is reserved for a future spectator and returns UNSUPPORTED with no binding.
Authenticate the existing human seat, including lobby seats, without allocating a seat or
submitting a programme. Failed BIND leaves the old binding intact. Successful BIND replaces it
and cancels the old transfer. JOIN automatically binds its newly allocated seat. Allow multiple
connections with the same valid credential, matching JSON; H3 resolves conflicting submissions.
Queries still allow explicit public match scope, matching the existing API; this is **not** a
spectator subscription, lobby seat or implementation of role 2. No secret programmes are exposed.

Credential verification needs a narrow server orchestration accessor in B2: never fake it with
an invalid SUBMIT_PROGRAM, since that would poll/consume budget/change responses. No engine changes.
BIND and JOIN return a BOUND record with the per-seat numeric-ID high-water hint. Server retains room-lifetime (not adapter-lease-lifetime) per-seat high-water bookkeeping for the
maximum numeric `b:N` key observed by the adapter (including unrecorded refusals), and scans H3
records for canonical `b:N` JSON keys; hint=max+1 (0 if none observed), or exhausted=1 and hint=0 at UINT32_MAX. New clients use
that hint; reconnecting clients retain pending IDs and their next counter. Concurrent writers
must coordinate fresh IDs; a collision is reported by H3, never translated into a new command.

## 3. Request and transport-control opcodes

All numbers here are hexadecimal. `scope` is `B selector` then S(16) match_id only for selector=1;
selector=0 uses the connection binding, otherwise BAD_BINDING. It does not grant submission rights.
`key` is `B mode`: 0=no request_id (canonicalizes absent/empty); 1=I numeric ID; 2=S(64) exact ASCII
ID (length 1..64). Numeric mode maps server-side to `b:<unsigned decimal without leading zeros>`.
Zero is a valid numeric key. No requirement for monotonic acceptance; retry the exact key/payload.
All keyed attempts use the unchanged H3 store and budget. Numeric key exhaustion requires mode 2
with fresh IDs or stopping new submissions, never resetting/reusing a number. Only mode 1 needs
numeric high-water tracking. Arbitrary ASCII spelling is preserved in mode 2.

| Op | Request | Body in order | Payload maximum |
|---|---|---|---:|
| 01 | HELLO | section 2 | 30 |
| 02 | BIND | section 2 | 90 |
| 03 | FETCH | I transfer; I offset | 11 |
| 04 | CANCEL | I transfer | 7 |
| 10 | LIST_MAPS | empty | 3 |
| 11 | CREATE_MATCH | B present; S(32) map_id; B players; B bots; B rounds; H target; I planning_seconds | 46 |
| 12 | JOIN_MATCH | S(16) match_id | 20 |
| 13 | START_MATCH | scope | 21 |
| 14 | SUBMIT_PROGRAM | B identity_mode; [J player if mode=1]; J round; J revision; B order[3]; B ready; key | 86 |
| 15 | GET_SNAPSHOT | scope | 21 |
| 16 | GET_EVENTS | scope; J since_version; H max_versions | 27 |
| 17 | GET_MAP | scope | 21 |
| 18 | GET_MATCH_STATUS | scope | 21 |
| 81 | HELLO_OK (reply) | section 2 | 48 |
| 82 | PAGE (reply) | section 4 | P |
| 83 | CANCELLED (reply) | I transfer | 7 |
| ff | FAULT (reply) | B adapter_code | 4 |

CREATE present bits 0=map,1=players,2=bots,3=rounds,4=target,5=planning. Absent slots zero,
including map_id length zero; defaults come from the JSON server. Numeric domains as in requirements
10.1. Present map_id cannot be empty. Invalid structure/domain gives BAD_MESSAGE; syntactically
valid semantic requests retain the existing game answers. SUBMIT identity_mode=0 infers bound
player; mode=1 supplies a signed player for compatibility/error tests using the bound credential.
Neither mode can control a different seat. Require a player binding before dispatch; use a wrong
credential on BIND to test authentication, not per-message credential strings. Round/revision/
since_version keep full i32 domains. Orders 0..8 are valid; ff is the **defined** unknown-order
sentinel mapping to the existing unknown-token INVALID_ORDERS outcome; 09..fe are malformed wire.
Structural wrong-order-count JSON requests are outside the fixed three-slot binary request domain,
but their server error variant remains representable. No byte envelope is a universal JSON tunnel.

## 4. Response paging and finite resource ownership

Every game response and BIND result is a finite ordered **record stream**. PAGE body:

| Payload offset | Field |
|---|---|
| 3 | I transfer_id (nonzero, session-local monotonic; no reuse) |
| 7 | I offset (byte offset in serialized record stream) |
| 11 | I total_bytes |
| 15 | I high_water_version (0 if no match publication applies) |
| 19 | B origin_request_opcode |
| 20 | B flags: bit0=final, bit1=ERROR response; other bits zero |
| 21 | B data_length |
| 22 | data_length bytes, comprising whole records |

One record is `B tag; B body_length; body`. Maximum record is 101 bytes (ECHO);
minimum P=128 leaves 106 data bytes, so records never fragment across pages. Greedily include
as many whole records as fit (<=P-22), without splitting or reordering. Text and tile data have
explicit smaller fragment records. FETCH offset must be the start of a previously returned page
or exactly the preceding page's end. Arbitrary/interior offsets fail BAD_CURSOR. The final flag
is set iff offset+data_length=total_bytes. Empty EVENTS still has its EVENTS_META record.

GET_EVENTS captures a bounded high-water H (section 8), selects retained events with since_version < version <=H,
and produces all records in engine order. Events from one version may cross pages. The ordinal
is the byte cursor in this frozen stream, **not a version**. Commit the event watermark to H only
after the final page. Snapshot pages capture one immutable state and deadline; a deadline may
change without a version increment, so version alone is not transfer identity. Failed/expired
snapshot recovery invalidates the incomplete mirror. A client can rebuild in place, but may not
submit from partial state. Record counts/ordering and final total must match before validation
of a complete logical object. Status/results/errors are frozen by the same rule.

One transfer per adapter session; a newly dispatched game request replaces the previous transfer.
A successful BIND also replaces it; failed BIND and adapter-level refusals leave it intact.
Transfer IDs increase without wrap; exhaust => LIMIT, rotate session. CANCEL releases the current
transfer; cancelling it twice with a fresh exchange returns BAD_CURSOR. Duplicate exchange replay
still returns the cached CANCELLED. Final transfers remain available for retry until replaced or
idle expiry. Pin **only serialized response bytes**, never unlimited engine history. Total serialized
transfer limit is 4 MiB, global session transfer storage cap 64 MiB, 64 sessions, same 1800-second
idle lease as section 2. Admission failure gives LIMIT before game mutation. Mutation replies
reserve a 4096-byte result slot plus fixed caches before dispatch; their known success shapes fit.
Read-only responses exceeding a cap give LIMIT without partial transfer (JSON is unaffected).
Do not build unbounded temporary JSON/byte arrays just to discover a limit: count/stream into the
capped slot, or walk existing retained records. Retained event history remains 512 versions.
SNAPSHOT_REQUIRED is the existing game code when history is missing, distinct from transfer expiry.

Expiration makes FETCH return TRANSFER_EXPIRED, not a newer snapshot. Session expiry returns
SESSION_EXPIRED at HELLO. A discarded ID in a live session gives TRANSFER_EXPIRED; invalid cursor
in its current transfer gives BAD_CURSOR. No timer-driven push and no accumulated response queue.
Backpressure cannot delay GameServer deadline polling. PAGE replay copies original bytes, with
only exchange_id changed when a FETCH is a new exchange. Duplicate exchanges replay identical frames.
EOF mid-frame discards incomplete incoming data; a request dispatches only after complete validation.

## 5. Numeric vocabulary

Values in each row below are decimal **starting at 0 in the listed order**, unless specified.
They are independent namespaces, never C++ enum memory layouts. Unlisted values are invalid.

| Namespace | Values |
|---|---|
| orders | FORWARD_1, FORWARD_2, BACK_1, TURN_LEFT, TURN_RIGHT, TURN_AROUND, DROP, TOGGLE, WAIT; ff=invalid token |
| headings | N,E,S,W |
| phase | PLANNING,EXECUTING,FINISHED,LOBBY |
| terrain | Floor,Wall,Spawn,Dock,Dispenser,Conveyor,Console,LaserEmitter,Pit,Trap,TarGenerator,Turret,TurretPad,TrapConsole |
| move cause | ORDER,CONVEYOR |
| disable cause | PIT,LASER,TRAP,TURRET |
| no-op reason | NOT_CARRYING,TILE_OCCUPIED,NO_CONSOLE,TAR |
| destroy reason | DROP_BLOCKED,PIT |
| score reason | TRAP_DAMAGE,TRAP_DISABLE,TURRET_DAMAGE,TURRET_DISABLE,SHUNT |
| match reason | 0=absent,1=target,2=cap |
| personality | hauler,opportunist,saboteur |
| device tag | 0=none,1=trap,2=turret |
| group | A..Z encoded 0..25 |

Game code bytes: 00 OK; 01 REQUEST_TOO_LARGE; 02 BAD_REQUEST; 03 UNKNOWN_REQUEST;
04 UNKNOWN_MATCH; 05 UNKNOWN_MAP; 06 TOO_MANY_MATCHES; 07 INVALID_MAP; 08 ALREADY_STARTED;
09 MATCH_FULL; 0a START_FAILED; 0b NOT_STARTED; 0c UNKNOWN_PLAYER; 0d UNAUTHORIZED;
0e INVALID_ORDERS; 0f SNAPSHOT_REQUIRED; 10 PLANNING_CLOSED; 11 WRONG_ROUND;
12 PROGRAM_LOCKED; 13 INVALID_REVISION; 14 STALE_REVISION; 15 REVISION_CONFLICT;
16 REQUEST_ID_REUSED; 17 REQUEST_LIMIT.

SUBMITTED detail: 0=accepted,1=unchanged,2=invalid order count,3=unknown order token,
4=other. Accepted/unchanged require code OK; details 2/3 require INVALID_ORDERS. Other errors use 4.
Exact `message` is carried as TEXT diagnostic, including errors with dynamic map/field details;
the detail byte never replaces message fidelity. ERROR and SUBMITTED remain distinct shapes.

Adapter FAULT bytes (separate from game codes): 01 UNSUPPORTED; 02 BAD_MESSAGE;
03 BAD_BINDING; 04 SESSION_EXPIRED; 05 EXCHANGE_REUSED; 06 BAD_SEQUENCE;
07 BAD_CURSOR; 08 TRANSFER_EXPIRED; 09 LIMIT; 0a PROFILE_LIMIT.
FAULT echoes request exchange ID (zero before HELLO). NetworkDevice status stays below this layer.

## 6. Record schemas and logical response grammar

Hex tags below. B player masks use bit (player-1), bits 4..7 zero; decoding yields ascending
player arrays, matching all current producers. No record fields are omitted unless explicitly
variable; absent values use flags and zero slots. All counts include only the current response.
`state_version` in a stream is the PAGE high-water unless explicitly stored (e.g. individual events).
`protocol` and `rules_version` expand to `botched/1` from negotiation, not repeated strings.

| Tag | Name | Body fields in order | Body max |
|---|---|---|---:|
| 01 | CONTEXT | S(16) match_id | 17 |
| 02 | CREATED | B player_count,bots,rounds; H target_score | 5 |
| 03 | JOINED | B player_id,started; S(64) session credential | 67 |
| 04 | STARTED | B player_count | 1 |
| 05 | SUBMITTED | J player_id,round,revision; I response_version; B ok,code,detail; key | 85 |
| 06 | ERROR | B game_code | 1 |
| 07 | ECHO | S(32) request_type; key | 99 |
| 08 | BOUND | B role,player_id; I next_numeric_key; B exhausted | 7 |
| 09 | SNAP_META | B phase,round,tick,rounds_total; H target_score; I next_crate_id,map_handle; B flags,players; H crates; B groups,traps,turrets,submitted_mask,ready_mask; Q deadline_seconds | 31 |
| 0a | PLAYER | B id,flags; b x,y; B heading,damage; I carrying,score,deliveries,damage_taken,sabotage; B pushed_by | 27 |
| 0b | CRATE | I id; b x,y; B carrier | 7 |
| 0c | GROUP | B id,reversed | 2 |
| 0d | HAZARD | B kind,id,value,owner | 4 |
| 0e | STATUS | B phase,round,tick,rounds_total; H target; I version; B player_count,joined,capacity,bots,submitted,ready,winners,bot_count; (B player,B personality)[bot_count] | 26 |
| 0f | TEXT | B field; I total,offset; B n; byte[n] | 74 |
| 10 | MAP_META | I map_handle; S(32) map_id; B width,height; H tiles,attributes | 43 |
| 11 | TILES | H first_tile; B tile_count; byte[ceil(tile_count/2)] | 67 |
| 12 | ATTRIBUTE | B x,y; H value; B aux | 5 |
| 13 | MAP_LIST | H entry_count | 2 |
| 14 | MAP_ENTRY | I map_handle; S(32) map_id; B min_players,max_players | 39 |
| 15 | EVENT | B event_code; I version; B round,tick; I sequence; params per section 7 | 66 |
| 16 | EVENTS_META | J since_version; I event_count | 8 |

Response grammar (concatenation, * means count from metadata, not arbitrary repetition):

* CREATE: CONTEXT CREATED. JOIN: CONTEXT JOINED BOUND. START: CONTEXT STARTED.
* BIND: CONTEXT BOUND. SUBMIT: CONTEXT SUBMITTED TEXT(field=3)*.
* SNAPSHOT: CONTEXT MAP_META SNAP_META PLAYER* CRATE* GROUP* HAZARD(trap)* HAZARD(turret)*.
  MAP_META supplies identity/dimensions; it does not transfer tiles here.
* EVENTS: CONTEXT EVENTS_META EVENT*. GET_MAP: CONTEXT MAP_META TILES* ATTRIBUTE*.
* STATUS: CONTEXT STATUS. LIST_MAPS: MAP_LIST then (MAP_ENTRY TEXT(field=1)* TEXT(field=2)*) per entry.
* Any game ERROR: ERROR ECHO TEXT(field=3)*. Set PAGE error flag. ECHO empty type means no echoed
  request_type; key mode 0 means no echoed request_id. Game errors preserve exact diagnostics.
  No ERROR may masquerade as a SUBMITTED response. Adapter FAULT has no transfer.

TEXT field=1 map name,2 map description,3 diagnostic message. For each string emit at least
one record (including n=0,total=0,offset=0 for empty), contiguous offsets, fragments of 64 bytes
except last. Maximum logical string length 65535; metadata from normal map files is already
limited to name 48/description 120. Oversized programmatic metadata gives LIMIT, not truncation
or a new JSON restriction. Each MAP_ENTRY owns its following text. At most 65535 map entries;
4 MiB overall transfer cap still applies. Consumers can stream/discard text without storing it.

SNAP_META flags: bit0 target present, bit1 deadline present; target zero iff bit0 clear.
Deadline zero when absent; signed i64 absolute UTC seconds otherwise. Mirror stores it losslessly;
clients need not perform 64-bit arithmetic. PAGE high-water is snapshot version. PLAYER flags:
bit0 respawn_pending (x/y zero and semantically null), bit1 carrying present (otherwise zero/null),
bit2 sabotage present (otherwise zero/omitted), bit3 pushed_by present (otherwise zero/omitted).
CRATE carrier=0 means loose with meaningful coordinates; carrier 1..4 means carried with zero
coordinate slots decoding as null. HAZARD kind=1 trap/value=armed, kind=2 turret/value=charge;
owner 0 decodes null. Traps/turrets collections absent when count zero. All collections in current
canonical order. Crate IDs/next ID are **u32 in v1**, deliberately superseding the u16 proposal:
restored valid IDs above 65535 must round-trip with no extension profile. Semantic upper bounds
are the existing signed-int/restore validation domains, not all UINT32 values.

STATUS lobby phase: meaningful fields are phase,rounds_total,target,joined,capacity,bots;
other slots zero. Running/finished: joined/capacity zero; all other fields meaningful, winners
zero unless FINISHED (nonzero then), bot_count=bots, pairs ascending player ID. Status does not
invent a deadline or result reason. The corresponding version is also in PAGE high-water.

Map handles are nonzero u32 monotonically allocated for immutable registered maps within the
128-bit server_epoch, never reused there. Cache key=(epoch,handle), not just text ID. No hash
collisions or new content hash implementation. New epoch invalidates cache. Handle exhaustion
refuses registration in the binary adapter, not engine mutation. MAP_META tiles=width*height,
attributes equals the number of tiles requiring attributes below. Terrain nibbles are row-major,
first tile in low nibble; high nibble zero for an odd final count. TILES chunks cover 0..tiles-1
contiguously, 128 tiles per record except last. The odd last byte does not add a tile.

ATTRIBUTE records are in row-major order, exactly one for each listed terrain, no duplicates:
spawn value=player/aux=heading; dock value=player/aux=0; dispenser value=ID/aux=0;
conveyor value=group/aux=heading; console value=group/aux=0; laser value=0/aux=heading;
trap/console/pad/turret value=device ID/aux=0. Floor/wall/pit/tar generator have no attribute.
Consoles reconstruct `T001` etc. by their row-major ordinal; no textual IDs on the client path.
Groups 0..25, player IDs 1..4, hazard IDs 1..9, dispenser IDs 1..999. Cache stores the nibble
plane plus five-byte attribute entries, without C struct padding. Deriving tar influence needs
only the generator tiles; no persistent tar bitmap is required.

## 7. Event parameter layouts

EVENT common body is 11 bytes (section 6). Params immediately follow in the order below; no
implicit fields. IDs/positions are the actual authoritative values, not animation deltas.
`rows` means `B count` then count (2..4) repetitions of `B player; I score,deliveries,damage_taken`,
13 bytes per row. Player arrays in existing events are ascending, so masks preserve their order.
Event IDs are hexadecimal and stable. Params sizes include their count/variant slots.

| ID | Event | Params in order | Params max |
|---|---|---|---:|
| 01 | PLANNING_CLOSED | B missing_mask | 1 |
| 02 | ROUND_STARTED | B round | 1 |
| 03 | ROUND_ENDED | B round; rows | 54 |
| 04 | MATCH_ENDED | B winners_mask,reason; rows | 55 |
| 05 | ORDER_EXECUTED | B player,order | 2 |
| 06 | ORDER_NO_OP | B player,order,reason | 3 |
| 07 | ORDER_SKIPPED_DISABLED | B player,order | 2 |
| 08 | ROBOT_MOVED | B player; b from_x,from_y,x,y; B cause | 6 |
| 09 | ROBOT_PUSHED | B player,by; b from_x,from_y,x,y | 6 |
| 0a | MOVE_BLOCKED | B player,dir; b x,y; B cause | 5 |
| 0b | ROBOT_ROTATED | B player,from,to | 3 |
| 0c | CRATE_SPAWNED | I crate; H dispenser; b x,y | 8 |
| 0d | CRATE_PICKED_UP | B player; I crate; b x,y | 7 |
| 0e | CRATE_DROPPED | B player; I crate; b x,y | 7 |
| 0f | CRATE_DELIVERED | B player; I crate; b x,y; I score | 11 |
| 10 | CRATE_MOVED | I crate; b from_x,from_y,x,y | 8 |
| 11 | CRATE_DESTROYED | I crate; b x,y; B reason | 7 |
| 12 | GROUP_TOGGLED | B player,group; H console_ordinal; B reversed | 5 |
| 13 | LASER_HIT | B player; b emitter_x,emitter_y; B damage | 4 |
| 14 | ROBOT_DISABLED | B player,cause; b x,y; B flags,by,device_tag,device_id | 8 |
| 15 | RESPAWN_REDIRECTED | B player; b spawn_x,spawn_y,x,y | 5 |
| 16 | ROBOT_RESPAWNED | B player; b x,y; B heading | 4 |
| 17 | TRAP_ARMED | B player,trap; b x,y; B was_armed,previous_owner,owner | 7 |
| 18 | TRAP_TRIGGERED | B trap; b x,y; B owner,victims_mask | 5 |
| 19 | TRAP_HIT | B trap,player,damage,owner,points | 5 |
| 1a | TURRET_CAPTURED | B player,turret; b x,y; B previous_owner,owner | 6 |
| 1b | TURRET_CHARGING | B turret; b x,y; B charge,owner | 5 |
| 1c | TURRET_FIRED | B turret; b x,y; B owner,player,dir,distance,damage,points | 9 |
| 1d | TAR_SLOWED | B player,order; b x,y | 4 |
| 1e | SCORE_AWARDED | B player,points,reason,victim; I score; B device_tag,device_id | 10 |

ROBOT_DISABLED flags bit0 means `by` present; all other bits zero. Without bit0 by=0; with it
by=0 is **present neutral attribution**. Trap/turret tags require bit0 and matching cause; PIT
may have by without device; LASER has neither. Tag none requires device_id=0. SCORE_AWARDED
uses device tag 1 for trap reasons, 2 for turret reasons, 0 for SHUNT; SHUNT device_id=0. Neutral
owners are 0 in events, not null. Reason absence for fixed-round MATCH_ENDED is encoded 0.
No current event variants are omitted or synthesized. Reconstruct group strings and console IDs
only on the host/reference decoder, not on the 8-bit path.

## 8. Profiles, immutable admission and bounded event windows

Profiles define **client storage ceilings**, not different game rules: K1=1024, K4=4096,
K8=8192 bytes for the protocol/world mirror accounting in resources.md. The adapter computes
required storage from the immutable map using the conservative whole-match formula there.
Before CREATE/JOIN/BIND success, and before returning a scoped SNAPSHOT/EVENTS, refuse PROFILE_LIMIT
if the chosen profile cannot hold that map's worst-case state. No seat or programme is changed.
LIST_MAPS/GET_MAP/STATUS may stream metadata even for unsupported maps, but a client must not cache
more than its profile allows. GET_MAP on an unsupported map is inspection-only and its tile/attribute
records must be streamed/discarded; do not represent it as a playable binding. Four-player capacity
is budgeted even for two-player matches, so later lobby joins cannot invalidate admission.
Map selection is immutable for a match. K8 covers every valid current map under the packed worksheet;
K1 is intentionally selective. Actual measured C implementation costs must pass the selected
ceiling in B5; the worksheet is not a claim of a working 6502 decoder or stack measurement.

GET_EVENTS ends with `H max_versions`, range 1..16; maximum payload is **27** bytes.
For nonnegative since <=current choose H=min(current,since+max_versions), using checked arithmetic.
Return exactly the retained events in (since,H], and PAGE high_water=H. A future since cursor gives
H=current and an empty list, as JSON does. Negative cursors retain existing SNAPSHOT_REQUIRED behavior.
Requests older than retained history fail, never silently start at retention boundary. The client
repeats after final page until caught up. This bound permits exact equivalents of JSON's complete
result by concatenating windows without an unbounded transfer, with no partial-version watermark.
A window can span eventless versions; high-water still advances. The original since is in EVENTS_META.
No changes to JSON GET_EVENTS or engine retention are implied. An event window frozen before further
history eviction remains valid until transfer expiry.

## 9. Implementation limits fixed by B1

All unlisted operations/tags/values are reserved and rejected in v1. No spectator role, firmware
service, text parser on a retro client, engine rule change, secret-programme recovery or codec is
part of B1. Implementing server accessors, resumable adapter sessions and serialization belongs to
B2; direct/NIO I/O to B3/B4. B5 proves the measured implementation fits profiles. Test every listed
variant plus truncation at every byte, unknown flags, bad counts, noncanonical absent slots,
profile refusal, page retry/expiry, exchange rollover, numeric-ID exhaustion, stale credentials,
neutral attribution, intermediate snapshots and target/cap ties. These are future tests, not passes
claimed by this design. CPU aspirations remain the historical requirements §5.3 until measured.

## 10. Canonical limits and exceptional values

The field listings define offsets by addition from body offset zero; variable fields always
carry their length/count. A decoder must not read a later field before validating that length.
All string bounds count bytes, not a trailing NUL. S(16) match_id and S(32) map_id must be nonempty
except explicitly absent slots. Credentials in BIND may have length 0..64 to represent failed
authentication; JOINED credentials are nonempty. All state and map references are validated
against the matching map/profile. Generic i8 event positions range -1..20 in x, -1..16 in y only
for off-board MOVE_BLOCKED; other event coordinates are in-board. Coordinates of a pit disable
are its actual pit tile, not the safe crate-drop tile. Positive IDs and flags are not silently clamped.

Game error diagnostic fragments are exact bytes of the server-produced message; the adapter may
stream those strings but must not use them to select logic on a retro client. ERROR code 00 is
invalid. SUBMITTED ok=1 iff code=00; rules-level error codes and detail/message combinations
must match the current server outcome. The normal game ERROR envelope has only the current
server-error subset of codes; the numeric namespace is shared with SUBMITTED, not permission to
change response shapes. ECHO canonical numeric mode may stand for a JSON key `b:N` only when its
spelling is canonical; otherwise mode2 preserves spelling. Empty request_id is canonicalized to
absent because the server neither echoes nor records it. No other absent/null distinction is erased.

Page high-water rules: SNAPSHOT/EVENTS use their captured version; STATUS uses its version (0
in lobby); SUBMITTED uses response_version, including replayed old versions. CREATE/JOIN/START/
BIND/MAP/MAPS/ERROR use 0 even when a side effect starts a match. Their result is not an event ACK.
The envelope's ERROR flag is set only for the ERROR record grammar, not SUBMITTED ok=false.
Map identity is recovered through MAP_META/GET_MAP, not fabricated from a hash. Epoch and handles
are protocol metadata only and never change canonical JSON hashes.

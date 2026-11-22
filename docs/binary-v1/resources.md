# B1 resource budget and admission worksheet

These measurements count bytes in the frozen packed design and cells/attributes in the checked-in
map files. They are **not runtime heap, stack, cycle or network benchmarks**. No codec exists in B1.
B2/B5 must measure the C implementation; neither passing these arithmetic checks nor a compiler
build is a claim of measured 6502 performance. All profiles use the same wire semantics and P<=240.

## Fixed client allocation target

| Allocation | Bytes | Lifetime / rule |
|---|---:|---|
| Game receive/page buffer | 256 | Includes 2-byte TCP prefix; one page at a time |
| NIO raw scratch | 64 | Stream data in chunks <=32 bytes; see transport accounting below |
| Pending request buffer | 96 | Largest request90 + prefix2; retain until answered, including reconnect |
| Seat credential | 64 | Length stored in bookkeeping; retain across adapter-session expiry |
| Bound match ID | 17 | Length plus16 characters |
| Recovery secret | 16 | Server-issued, retained across connection loss |
| Server epoch | 16 | Cache identity; map handle in header |
| World/header/status storage | 40 | Packed: map handle4, version4, nextcrate4, deadline8; phase/round/tick/rounds4, target2, dims2, cratecount2, attribute count2, player/group/trap/turret counts4, submitted/ready/winners/bot mask4 |
| Protocol bookkeeping | 32 | Two NIO cursors8, transfer/offset/total12, exchange2, next numeric key4, NIO handle2, flags/credential length/profile/P4 |
| Four player records | 108 | 4*27, fixed regardless of actual seats |
| Decode stack/scratch allowance | 32 | Target, not measured; no recursion, event history or animation queue |
| Implementation reserve | 100 | Covers additional parser/cursor state and stack beyond the explicit minima; do not count it twice |
| **Fixed subtotal** | **841** | Sum checked arithmetically |

Bot personalities can occupy reserved packed bits in header's bot mask byte (2 bits/seat);
bot membership comes from STATUS pairs and client stores a separate mask in bookkeeping flags.
UI strings are streamed; renderer-owned copies, OS/NIO library internal allocations, fonts and
animation queues are outside this worksheet and must be reported separately by platform ports.
If a platform library requires another packet buffer, count it before claiming a total machine-RAM
figure. Stack/scratch exceeding 32 bytes uses the explicit 100-byte reserve first; excess beyond that
requires higher actual profile admission and must be reported. The wire profile ceiling is a maximum, not a promise
that every ABI or compiler achieves this packed worksheet.

## Variable storage and safe bound

Let N=width*height, A=attribute-bearing tiles, W=walkable tiles (neither blocked nor pits),
G=distinct conveyor groups, T=trap devices, U=turret devices. C=W+4 bounds all simultaneous loose
and carried crates for any round count and valid restored state. Do not use the number of initial
spawns or a random simulation's observed maximum. No per-crate history or renumbering is required.

`R = 841 + ceil(N/2) + 5*A + 7*C + 2*G + 4*(T+U)` bytes.

Map nibble padding is included. ATTRIBUTE cache is exactly 5 bytes each; CRATE mirror 7; group 2;
hazard 4. Iterate attribute entries without an extra lookup index; do not allocate a tar bitmap.
Admission is R<=profile ceiling. Four players are budgeted even for a two-player match. K1=1024,
K4=4096,K8=8192. Counts are immutable-map properties, so admission cannot fail later merely
because crates accumulate. An implementation may use a tighter proven bound, but may not use
observed play traces in place of a proof. All clients must preserve the full semantics of admitted
matches. Profile refusal has no game/seat mutation, no downgrade of hazards and no map truncation.

Global conservative bound: N<=320,A<=320,W<=320,C<=324,G<=26,T<=9,U<=9. Therefore
R<=841+160+1600+2268+52+72=4993 bytes. These independent maxima overestimate possible maps;
K8 covers them all. The earlier universal 1 KB aspiration is not attainable for the shipped maps
with this lossless mirror; **all shipped maps below require K4**, so a K1-only client must refuse
them. K1 remains an admission profile for sufficiently small/sparse legal boards, not a claim
that training-01 fits. No new map is introduced to make the measurement look better.

## Source-resource census (four-player worst-case allocation)

| Map file | Board | Attributes | Walkable | Crate bound | Groups | Traps | Turrets | R bytes | K1 | K4 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| crossroads-01.map | 12x10 | 19 | 73 | 77 | 2 | 0 | 0 | 1539 | no | yes |
| factory-01.map | 14x11 | 26 | 96 | 100 | 2 | 0 | 0 | 1752 | no | yes |
| foundry-01.map | 11x13 | 31 | 77 | 81 | 2 | 2 | 2 | 1655 | no | yes |
| laserworks-01.map | 12x10 | 13 | 70 | 74 | 0 | 0 | 0 | 1484 | no | yes |
| shuntworks-01.map | 12x10 | 11 | 73 | 77 | 0 | 0 | 0 | 1495 | no | yes |
| tarworks-01.map | 12x10 | 11 | 73 | 77 | 0 | 0 | 0 | 1495 | no | yes |
| training-01.map | 12x10 | 19 | 73 | 77 | 1 | 0 | 0 | 1537 | no | yes |
| trapworks-01.map | 12x10 | 14 | 73 | 77 | 0 | 1 | 0 | 1514 | no | yes |
| turretworks-01.map | 12x10 | 14 | 74 | 78 | 0 | 0 | 1 | 1521 | no | yes |

## Per-message sizes and large logical results

HELLO30, HELLO_OK48, BIND<=90, FETCH11,CANCEL7; LIST_MAPS3,CREATE<=46,JOIN<=20,
START<=21,SUBMIT<=86,SNAPSHOT/MAP/STATUS queries<=21,EVENTS<=27; PAGE22..240,
CANCELLED7,FAULT4. Add2 for the TCP prefix. All requests fit96 bytes; all single records fit
106 bytes of page data at P128. Largest record is ECHO101; largest EVENT68; largest TEXT76.
The old <=10-byte submission, <=8-byte acknowledgement and2–6-byte event targets are superseded.
A normal bound numeric-key submission is 21 bytes, or 23 on the TCP stream.

A full world snapshot's record-stream upper bound (max-length context/map IDs) is:
19 CONTEXT +45 MAP_META +33 SNAP_META +116 PLAYERs +9*C +4*G +6*(T+U)
=213+9*C+4*G+6*(T+U), at most 3341 bytes with global counts above.
It is paged, not buffered as a second full snapshot. A map stream <=19+45+3*69+7*A=2511 bytes
using at most 3 TILES records at 320 cells and 320 attributes. MAP_LIST entry maximum is41 entry
+60 name (48 bytes plus12 overhead)+144 description (120 bytes plus24 overhead)=245 bytes;
list total <=4+245*entry_count, also subject to the 4 MiB transfer cap.

New-match crate IDs: at most 320 dispensers*200 rounds=64000 spawns, next ID<=64001.
Wire u32 also preserves restored states accepted up to 1000000 by the world reader. Console
ordinals<=320, dispenser IDs<=999. Version/sequence retain current nonnegative signed-int domain;
no narrowing and no wrap. Request revisions retain i32, including negative rule refusals.
All new-match deliveries together are bounded by the 64000 crates ever spawned. Robots may
be pushed repeatedly in a tick, so a one-delivery-per-order assumption is unsafe. v1 keeps all
score/delivery/damage/sabotage counters u32, preserving valid restore values up to 1000000 as
well as live values. A conservative fresh-match aggregate score bound is 331200:
5*64000 delivery points +8*800 possible SHUNT disables +2400 scored damage hits
+3*800 attributed disables. This deliberately counts mutually exclusive disables twice.
Per-player values cannot exceed the aggregate; cumulative damage per player <=3*200=600.
Restored counters may start at 1000000 and continue growing, so those fresh-match bounds are
not a narrowing rule. u32 preserves both. Target 1000 is not a cap on final score. No saturation.

A conservative tick event upper bound is 2048: <=4 order markers/no-ops; eight voluntary moves
with chains<=4 each and at most move + pickup + delivery + disable + drop/destroy + award per
robot gives<=192; belt chains<=96; 324 loose crates with move plus pickup+delivery/destroy<=972;
20 adjacent-console activations<=20; lasers<=4; traps<=9*(1+4 hits+4 scores)=81;
turrets<=9*3=27; final disable handling<=12; respawns/round results <=10; an additional 16 covers start-of-order delivery, turns and tar
notifications. Sum 1434 < 2048.
Mutually exclusive alternatives are deliberately double-counted. Start-of-round spawns<=320,
respawn events<=8 and ROUND_STARTED1 also fit2048. Accepted programmes create eventless versions.
Sixteen published versions*2048 events*68 bytes=2228224 bytes plus context/EVENTS_META<4 MiB.
Thus bounded GET_EVENTS windows fit a transfer without reducing retention or omitting events.
A fresh GET_EVENTS request after each final page advances through long history windows.

Server: 64 sessions*(request240+reply240+fixed metadata budget256)=47104 bytes;
one 4 MiB transfer/session, with global64 MiB cap. Allocate lazily. Mutation result reservation 4096
per dispatch is included in the global cap; overflow refuses before mutation. State/event JSON
already owned by GameServer is not duplicated indefinitely; capped serialization is required.
The limits cover adapter-owned bytes, excluding existing engine/H3 room storage and allocator overhead.
Per-seat numeric high-water bookkeeping adds 5 bytes per human seat (at most 320 bytes at the
default 16-room/four-seat bound); store it with the room adapter, not with expiring connections.

## NIO overhead and CPU acceptance

For 32 stream bytes: NetworkDevice Write request9+32=41 bytes, plus raw FujiBus header6 =>47
when no parameters; Read response12+32=44 plus raw header6+U8 status parameter1 =>51.
Both fit 64 raw scratch. The 32-byte chunk includes any TCP length-prefix bytes; a 242-byte framed
maximum game payload needs at least 8 such transfers (more with partial I/O). Extra device parameters
must reduce the requested chunk to maintain 64; never assume the game payload fits a raw packet.
SLIP worst-case expansion is2*51+2=104 bytes **on the wire**, streamed, not another 104-byte client
buffer. Packet-link framing similarly remains in its transport implementation. Open URL/setup
uses the receive buffer before gameplay; limit the selected endpoint's encoded Open to 256 bytes
or report a local configuration error, never truncate the hostname. OS/library-owned raw buffers
must be counted separately in a platform total; no NIO firmware modification is needed.

Actual CPU, no-allocation proof and stack high-water are deferred until an implementation exists.
B5 must report 1 MHz cycles for the smallest and largest events, full P240 page, maximal admitted
snapshot and SUBMIT, including validation and mirror update separately from socket I/O. Historical
150 cycles/event,3000/snapshot,400/submission are aspirations, not measured acceptance claims.
Use ca65/soft65c02 traces or equivalent on-target measurement; C-only host timings are not 6502 cycles.
If measured RAM exceeds the selected ceiling, refuse that profile or reduce actual storage with
reviewed packing; never alter the engine or silently exceed the advertised budget.

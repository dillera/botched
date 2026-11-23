# B5 constrained-client acceptance and size guide

B5 follows accepted B4 (`52e6b03a`). The frozen protocol, codec, engine and existing
NIO firmware/library are unchanged. This milestone supplies executable resource
acceptance, not a renderer or a platform gameplay client. **Several historical
resource targets fail.** Those failures are results, not permission to drop game
fields, narrow counters, change wire layouts or advertise an unsupported profile.

The checked-in [measurements](b5-measurements.json) record the compiler, linked
library hash, per-map storage and CPU results. `tests/b5/measure.py` regenerates
these results, linker maps, input vectors and full logs under `build/b5`.

## Acceptance verdict

| Requirement | Result |
|---|---|
| Frozen packet bounds and lossless 32-bit fields | Pass on host and NMOS 6502 |
| Codec/snapshot-storage allocation-free operation | Pass: allocation tripwires, no allocator linked on 6502 |
| K4 codec/storage working RAM for shipped maps | Pass: 2770–3038 bytes including caller storage and reserved stacks; excludes NIO library |
| K4 real NIO negotiation and semantic equivalence | Pass across all nine maps, target/cap/fixed endings, paging and recovery |
| K4 complete general-NIO working RAM with current default stack | **Fail:** 5406–5674 bytes for shipped maps |
| K8 complete general-NIO working RAM for shipped maps | Fits the measured allocation, including the configured 2048-byte software stack |
| Universal K8 complete general-NIO guarantee | **Not established:** conservative global allocation is 8915 bytes |
| Historical 150 cycles/event, 3000/snapshot, 400/submission | **Fail:** see measured costs below |
| Actual retro-device / interrupt / renderer acceptance | Not performed; outside this measurement milestone |

The K4 NIO executable still uses a host JSON collector for equivalence. Its
successful HELLO is **protocol/profile-admission evidence, not a 4 KB port**.
The distinct cc65 executable proves the codec/storage K4 result without that
collector or a C++ runtime. No full NIO K4 claim is made by combining those tests.

## What is actually allocated

The acceptance mirror (`tests/b5/mirror.h`) retains snapshot and map metadata,
status, four full player records, nibble terrain, attributes, all possible crate
slots, group states and hazard states. It stores wire bytes without host struct
padding inside records, keeps full u32 IDs/counters and the eight deadline bytes,
and does not allocate another snapshot during replacement. Display text can be
streamed. It is a snapshot-storage acceptance model, not an event-driven gameplay
mirror or map-consistency checker. The measured snapshot path fills every crate
slot at once, including four carried crates and restored IDs starting at 70000.
Map terrain/attribute capacity is allocated from the actual immutable map census;
the snapshot timing does not include downloading or applying a separate map.

Its size on cc65 is **225 + V**, where:

`V = ceil(width*height/2) + 5*attributes + 7*(walkable+4) + 2*groups + 4*(traps+turrets)`.

The 225 bytes are map metadata43, snapshot31, status26, players108, eight u16
counters/offsets16 and a failure flag1. This deliberately keeps more straightforward
metadata than B1's tightly packed 40-byte header proposal. Attributes are scanned;
there is no extra tile lookup index or tar bitmap. Counts come from source maps,
not observed crate counts in short matches. The global case reserves all B1
independent maxima; it is an upper envelope, not a claim those maxima coexist in
a legal map. Its 3341-byte snapshot stream also uses maximum-length context/map IDs.

`bn_client` is **537 bytes** under cc65 (560 on x86-64), including the 96-byte
pending frame, 256-byte reply, 32-byte HELLO, secret/epoch and 70-byte stream
validator. The separate caller allocation is **407 bytes**: request construction96,
credential65 including length, match ID17, maximum URL225 including terminator,
and numeric request key4. That construction buffer is counted in addition to the
retained request. Reader/writer structs are 8 bytes each on cc65 and live on the
measured call stack. Put `bn_client` in static storage or a caller arena on cc65:
the original host test's large automatic object hit cc65's local-variable limit;
the test now uses static storage, with identical transport behaviour.

### Full general-NIO allocation, factory-01

The link-only probe uses **the actual existing Atari compact/general archive**
and B4's concrete `fn_open/info/read/write/close` binding, not small-stream APIs,
stubs or a hypothetical 64-byte replacement. It is not run as an Atari client.
The linker brings in the real dependency graph and reports all writable storage.

| Allocation | Bytes |
|---|---:|
| B4 client object | 537 |
| Mirror fixed / factory variable | 225 / 911 |
| Caller storage | 407 |
| NIO request and response buffers | 512 + 512 |
| Other NIO BSS / DATA / zero page | 174 / 7 / 10 |
| cc65 runtime BSS / DATA / zero page | 5 / 44 / 26 |
| Configured software stack reservation | 2048 |
| Full hardware stack page reservation | 256 |
| **Complete working allocation** | **5674** |
| Resident startup, code, constants | 30961 |
| **Occupied application RAM including code** | **36635** |

The link has BSS3283, DATA51 and zero-page36. The default Atari software stack
reservation is counted in full; a codec-only high-water measurement is not used
to shrink the NIO call stack. The hardware page is counted conservatively even
though it is shared with platform code. The Atari loader's temporary SYSCHK
segment overlaps later-loaded code and is not added twice. File headers are not
resident memory. At the default `$2000` load address, the image-to-stack-top span
plus zero page/hardware stack is 40260 bytes; 3625 of those bytes are the unused
gap between the factory image and reserved stack, not an additional live buffer.

These figures exclude existing OS/display storage and future renderer, input,
menu, audio and animation queues. Those are not implemented allocations hidden
from this model. K4/K8 are **protocol/world working-storage ceilings**, not total
executable sizes: an application whose code is loaded into RAM must also pay the
code column. The model is a useful headless baseline, not a complete machine-RAM
or future Amiga-client budget. Amiga compiler/OS costs were not measured here.

### Map-by-map profile result

The core column counts **all** writable segments of the runnable measurement
harness, its complete mirror/caller/client object, 512 reserved software-stack
bytes and the full hardware page. This includes test bookkeeping and cc65 printf
state; fixture input is read-only test data, not a second production page buffer.
The NIO column instead counts the linked production dependency graph and its
unchanged 2048-byte software-stack reservation. Do not add the two columns.

| Map | Core harness working RAM | Full general NIO working RAM | NIO resident RAM incl. code |
|---|---:|---:|---:|
| crossroads-01 | 2825 | 5461 | 36422 |
| factory-01 | 3038 | 5674 | 36635 |
| foundry-01 | 2941 | 5577 | 36538 |
| laserworks-01 | 2770 | 5406 | 36367 |
| shuntworks-01 | 2781 | 5417 | 36378 |
| tarworks-01 | 2781 | 5417 | 36378 |
| training-01 | 2823 | 5459 | 36420 |
| trapworks-01 | 2800 | 5436 | 36397 |
| turretworks-01 | 2807 | 5443 | 36404 |
| Independent global envelope | 6279 | 8915 | 39876 |

For these measured builds, the totals are `R_core = 2127 + V` and
`R_NIO = 4763 + V`. Compared with B1's `841 + V`, that is an explicit
**1286-byte core / 3922-byte full-NIO increase**, not an uncounted use of the
100-byte reserve.

All shipped maps fit core K4 and the measured complete NIO K8 configuration.
None fits K1. None fits complete NIO K4 **with this default configuration**;
that is not a proof that a future measured, smaller stack/library configuration
cannot fit. The global envelope fits core K8 but does not prove complete NIO K8.
A future port must check its actual allocation before selecting/entering a map;
server admission still uses the frozen B1 packed worksheet and cannot infer a
client ABI, library or stack configuration. Do not silently enter a larger map
merely because the server permits it. No admission/layout/game limit is changed.

## Measured 6502 costs

Toolchain: cc65/cl65/sim65 V2.19, Git `37d4bb24`, `-O`, NMOS 6502, no wait states.
The simulator's latched CPU-cycle counter measures only the bracketed operation.
Output, stack painting/scanning and startup are outside it. The empty bracket is
62 cycles; table values are raw, reproducible measurements including that small
constant overhead. At 1 MHz, one million cycles is one second.

| Operation (factory fixture image) | Cycles | Approx. time at 1 MHz |
|---|---:|---:|
| Smallest event, PLANNING_CLOSED14, structural validation | 13531 | 13.5 ms |
| Largest event, MATCH_ENDED68 with four rows, validation | 50068 | 50.1 ms |
| Full P240 PAGE, structural validation | 196884 | 197 ms |
| Bound numeric SUBMIT21, transactional encoding | 21768 | 21.8 ms |
| Full P240 PAGE, transactional encoding (API stress case) | 207110 | 207 ms |
| factory maximum snapshot, stream validation alone | 2233566 | 2.23 s |
| factory maximum snapshot, B4 validation path plus storage | 5497188 | 5.50 s |
| Same snapshot, only storage of already validated records | 236218 | 236 ms |

The exact smallest-event figure is in the JSON artifact; instruction placement
can change branch/page-crossing costs slightly between images. All 162 valid B1
payload/record examples plus the 68-byte event and exact 240-byte page are executed,
round-tripped through encoders, and rejected at every truncated prefix. Oversize
P241 and a P240 packet under a P128 cap are rejected. The snapshot exercises the
full proven crate capacity of each map, not the initial few dispensers.

The B4 completion path validates the packet, then `bw_stream_accept` validates it
again; a consumer's `bw_visit_page` validates a third time. The combined number
includes those three passes and actual mirror copies. It excludes NetworkDevice
calls, TCP framing-state execution, serial/SIO waits and retries. The separate
storage measurement skips validation only because its input was already validated;
it is not a proposed unchecked production path. Per-event world mutation,
rendering and animation are **not measured**; the event values above are structural
decode/validation costs, not complete presentation costs.

The shipped-map maximum snapshot combined costs range from about **4.14 to 5.50
million cycles**. The independent global envelope costs about **17.64 million**.
No combination meets the historical 3000-cycle snapshot aspiration; the 150-cycle
event and 400-cycle submission aspirations also fail. A 1 MHz implementation using
this reference codec cannot assume immediate whole-snapshot reconstruction. Further
optimization would need a separate, reviewed implementation effort with equivalent
validation and frozen semantics; no unchecked parsing or code refactor was used to
make the numbers pass.

### Stack and allocation evidence

The harness paints unused cc65 software stack and the hardware page, then scans
after each operation. Maximum observed depth below the main measurement baseline
is **369 software bytes / 23 hardware bytes** across the codec cases. SUBMIT uses
309 software bytes; snapshot stream/visitor processing uses 204; raw mirror storage
uses 16. The test reserves 512 software bytes and asserts the measured peak fits.
That is codec/consumer evidence, **not** a measured worst-case for every platform
NIO/OS/interrupt call chain. The full NIO link retains and counts its normal 2 KB
reservation; physical stack/interrupt high-water remains a port acceptance task.

The transactional encoder's 240-byte local scratch still exists. B2's host stack
measurements and B4's host adapter measurements remain valid for those ABIs but
are not added to cc65 sizes. The 32-byte scratch and bookkeeping proposals from
B1 are not met: cc65 `bw_stream` alone is 70 bytes. B1's 64-byte NIO scratch also
is not the existing general library's footprint: it owns two 512-byte buffers
here (two 1024-byte buffers in the B4 Linux build). The link totals replace these
aspirations rather than spending B1's 100-byte reserve repeatedly.

Host runs use strict C99, `-Werror -pedantic-errors -Wvla`, and wrappers that abort
on malloc/calloc/realloc/free. The actual Atari link maps are checked for allocator
entry points, including transitive NIO dependencies. All nine maximum snapshots
and the global envelope pass on host and sim65. The existing B4 C99 partial-I/O
adapter test also runs successfully on sim65; it is not physical NIO acceptance.

## Packet and real-NIO evidence

No packet-size divergence: P<=240, TCP frame<=242, retained request<=96.
An exact 240-byte page and maximum 68-byte event now have executable 6502 tests.
The independent maximum snapshot has **3341 record bytes**, **16 P240 pages**,
and **3693 aggregate payload bytes** including PAGE headers. Factory's four-seat,
100-crate snapshot uses six pages/1217 aggregate payload bytes. All generated
snapshots are also packed at P128 and checked against its cap. Records never split.

NIO chunks remain <=32: raw Write<=47 and Read<=51 bytes for the existing header
forms; worst SLIP expansion of that Read is104 on-wire bytes, not a new RAM buffer.
The frozen 240-byte payload requires at least eight chunks including TCP framing.
The 224-character URL allowance and larger existing library buffers are included
in caller/library storage above. No firmware or small-stream API change is involved.

The real POSIX NIO suite now explicitly requests **P128/K4** for every NIO seat.
All nine shipped maps and target/deadline scenarios pass direct TCP/JSON equivalence,
H3, immutable paging, lost Read reply replay, ambiguous JOIN Write recovery,
nonregressing acknowledgement and disconnected planning deadlines. The run retains
B4's 616 pages/50026 response payload bytes/largest127-byte payload, with <=32-byte
reads/writes. NotReady occurs on the real service; DeviceBusy/short service counts
remain deterministic injected tests. This is real configured POSIX NIO, not
physical retro-device validation or a 4 KB host process.

## Reproduction and verification

From the repository, source the workspace environment first. The existing Atari
compact archive must be present; the script accepts `--nio-library PATH` to select
it explicitly and records its SHA256. It does not build/change NIO sources.

```sh
source "$NIO_WORKSPACE/scripts/env.sh"
python3 tests/b5/measure.py
# Optional CTest registration (cc65, sim65 and the existing archive required):
cmake -S . -B build -DBOTCHED_B5_MEASURE=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Real NIO registration still needs the B4 `BOTCHED_NIO_INCLUDE`,
`BOTCHED_NIO_LIBRARY` and `BOTCHED_NIO_EXECUTABLE` CMake settings. Without those,
a green local CTest run is not evidence of real connectivity. `build/b5/summary.json`
is the review artifact; full per-case logs, host runs and linker maps stay in the
build tree. Fixture arrays are generated from the frozen JSON and source census;
no generated source or binary is checked in.

Verification on 2026-11-23: the six existing CTest targets pass (325 C++ cases,
real P128/K4 NIO, C codec/adapter, binding and SDL/CLI); the new resource CTest also
passes. ASan/UBSan with leak detection passes the factory and global host resource
harnesses, including allocation tripwires. cc65 emits its existing signed-long
constant warnings in unchanged codec/adapter source; no layout or integer-domain
change was made to suppress them. `git diff --check` passes.

B5 stops here for review. Memory and CPU failures above remain explicit limits
for future clients, not unrecorded work or claims of a completed Amiga renderer.

# PXA ABI 1.0 migration design

This document defines the next breaking Core revision. The 0.x JSON specs and
golden vectors remain authoritative for the existing services. A deliberately
`pxa.core.v1` preview now runs signed Log, Device, Window, Permission,
Storage, private FS, IPC, Net, Audio, Sensor, Work, Surface, Clock,
GameRender, UI, the privileged Store Installer and WASI capability packages.
The signed GameRender benchmark, WASI/UI lab, games, Weather, Store and the
redesigned six-probe ABI Lab have migrated. The old ABI Lab's 18 interactive
v0 modules are retired; focused v1 apps and SDK tests cover their services.
The complete generated Service codec set is still pending.

The current v0 runtime already reserves completion storage for accepted
requests and uses 64-bit Host-only event lease tokens. The v0 Guest still uses
32-bit request IDs and Handles with the 12-byte envelope. The v1 preview binds
`pxa_submit` and `pxa_io` under `pxa.core.v1`, checks the signed package Core
major against imported modules, accepts one-way Log, Device runtime-info and
scoped MAC, Window configure/snapshot/toast, Permission check/acquire,
Storage get/set/remove/list, private FS, IPC, Net, Audio, Sensor, Work,
Surface, Clock, UI, GameRender and Store Installer requests, and widens Host events to the v1 envelope before the
Guest callback. A bounded WAMR map restores the Guest's full 64-bit token.
GameRender, Permission acquire and FS open use native Host 64-bit Handles and result
layouts; typed I/O and close operate directly on those Handles. Permission
`resolve64` validates full generation and declaration scope in the Host.
Device MAC uses the acquired native Handle, checks its exact scope and returns
the existing MAC result records. Permission revocation posts a token-zero
event with name and scope records; the Guest SDK exposes views into that event.
Storage v1 exercises 2048-byte values on the desktop Host through a bounded
Guest-memory copy. The adapter dispatches a decoded Host message view, avoiding
the intermediate v0 envelope encoding and decode; the copy is bounded by the
Core control-message limit and lives in one engine-owned scratch buffer.
FS v1 opens native Host 64-bit file and directory Handles, accepts 64-bit
seek/read-directory inputs, and uses Core typed I/O and close. The desktop
product simulator now mounts a publisher-and-App-scoped private POSIX root.
IPC v1 preserves full 64-bit request tokens for call/reply completions. Its
broker notifications carry the existing 32-bit call ID, zero-extended in the
v1 token field. A two-Component WAMR fixture and a signed product-simulator
package verify lazy provider activation, request delivery and reply routing.
The signed package also cancels a second pending call by its full 64-bit
request token before the lazy provider is activated.
Net v1 uses a native 64-bit Permission Handle for exact-origin authorization
and returns native 64-bit response Stream Handles. A simulated asynchronous
backend test covers GET, POST, Stream I/O, close, cancellation and rejection of
the old four-byte Permission Handle; a WAMR fixture checks v1 Net dispatch.
Audio v1 uses native 64-bit Permission and Session Handles for open, graph,
state, flush and PCM/tone/asset I/O. Host tests cover session lifecycle and
reject old four-byte Permission Handles; a WAMR fixture checks dispatch.
Sensor v1 uses native 64-bit Permission and subscription Handles in Subscribe
results and coalescible Sample events. Host and WAMR tests cover discovery,
subscription, sampling, close and rejection of old Handle widths.
Core Lease v1 returns native 64-bit Handles and reports those Handles in
revocation events. Host and WAMR tests cover expiry and dispatch.
Surface v1 uses native 64-bit Handles in all control requests, create results,
typed I/O and buffer-release events. A one-way queue-frame command keeps
the per-frame path free of request and completion allocations. The WAMR
adapter enforces the signed pinned-memory declaration for mapped buffers.
Clock v1 keeps periodic ticks coalescible and makes `now` a reserved
completion keyed by the full 64-bit token. The ESP Host and desktop simulator
now use the same Core-major split for that request.
UI v1 preserves logical Surface/node IDs and transaction payloads, but widens
Canvas stream Handles to native 64 bits. Theme get is a reserved completion;
Host and WAMR tests cover theme dispatch, stream open, typed I/O and stale
Handle rejection.
Work v1 uses full 64-bit request tokens with the existing bounded enqueue,
cancel, complete and worker-start records. Work IDs remain `u32` identifiers,
not resource Handles. A WAMR fixture covers v1 Work dispatch and the Guest SDK
provides allocation-free request and event helpers.
The preview rejects packages requesting other services before activation.
The preview holds at most 16 outstanding v1 requests per Component; a full
table rejects before Host dispatch. Core `cancel-request` now accepts a zero
envelope token and an eight-byte target token. The adapter resolves that token
to the Host request before calling the common cancellation path. A deferred
WAMR fixture verifies one `cancelled` completion before commit; the signed
package verifies cancelling a queued completion leaves its original result.
This applies to migrated v1 requests. App-wide cancellation coverage remains
to be expanded.
The signed simulator smoke package exercises 16 concurrent 64-bit tokens,
duplicate and full-table rejection across Device and Window, slot reuse,
17 successful Device completions, a Window snapshot completion, and Permission
check/acquire with native Handle close, scoped Device MAC and stale-generation
rejection, plus Storage set/get/list/remove and missing-key results. The FS
smoke path creates a directory and file, writes and reads bytes, seeks, stats,
iterates, renames, removes, and rejects a stale Handle. It then requests Clock
`now` and UI theme and checks both reserved completions in the signed Guest.
The Guest SDK headers are freestanding and compile with the bare Wasm toolchain.
The Host resource table now supports 32-bit generations and native 64-bit
open/get/I/O/close in parallel with v0. A slot retires at generation overflow;
v0 allocation skips slots beyond its 16-bit range, while native v1 allocation
can continue using them. The validated Core major is recorded on the Component,
so GameRender chooses the proper result layout and allocation width.
Most generated service codecs and application migrations are future work.
The Host and Guest provide
matching v1 envelope builders, with a checked golden vector in
`abi-1.0-envelope.json`. The v0 and v1 envelope codecs now come from these
two envelope schemas and the Core record schema as identical Host and
freestanding Guest headers. A checked-in generator and byte-for-byte tests
cover all 38 v0 message vectors, 13 record lists and the v1 envelope.
Device runtime-info and Window snapshot now have schema-generated Host/Guest
result codecs, checked golden vectors and malformed-input tests. Other service
payload codecs remain handwritten.

## Core contract

The Guest imports `pxa_submit(ptr:u32, len:u32) -> status:i32` and
`pxa_io(handle:u64, operation:u32, ptr:u32, len:u32) -> count_or_status:i32`
from `pxa.core.v1`. There is one bounded control envelope and one typed Handle
data plane. `pxa_submit` accepts a request only after its completion event
capacity is reserved; an immediate rejection never causes a backend effect or
later completion. The event callback receives the same envelope. Guest memory
ranges are validated and copied before dispatch, and Host workers never retain
Guest pointers.

The v1 envelope is 20 bytes, little-endian:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Service ID |
| 2 | 2 | Opcode |
| 4 | 8 | Request token |
| 12 | 4 | Payload length |
| 16 | 4 | Flags, zero in v1.0 |

The total length must equal `20 + payload length`. A nonzero request token is
unique among outstanding requests of one Component instance. Every accepted
request gets exactly one completion, beginning with `status:i32`; zero means a
one-way command. The Host exposes a distinct `commit` transition before an
irreversible side effect. Cancellation before commit stops work and completes
with `cancelled`; cancellation after commit does not imply rollback, but a
completion is still guaranteed while the Component is alive.

All Guest-visible Handles use `u64`: the low 32 bits are one-based slot index
and the high 32 bits are generation. Zero is invalid. A slot is retired when
its generation would wrap, so stale Handles cannot regain authority. The Host
still checks Component ownership, resource type and Permission authority on
every operation. Request tokens are chosen by the Guest and are not Handles.
The Host-only mailbox lease token has already been widened independently; it
is never serialized into this envelope.

## Dispatch and memory ownership

The runtime owns bounded request, completion and resource tables. An accepted
request holds the completion capacity until it is delivered or its Component
stops. Backends receive a Host-owned request snapshot and return a Host-owned
result. They may yield with `would-block`, but they may not call Guest code or
publish a result through another path. The runtime queues one completion and
wakes the Component. No service allocates a result event after performing a
mutation.

Large data stays on typed Handles. Control results carry descriptors and
small metadata; streams, Surface buffers and raster DrawLists have explicit
size limits and `pxa_io` operations scoped by resource type. Operation numbers
can coincide across different resource types because the runtime dispatches
by the resolved Handle type first. A backend may retain Guest memory only
through a separately negotiated pinned-memory lease, never through ordinary
`pxa_io`.

## Guest SDK

The C11 SDK exposes stack-allocated builders and an application-owned request
table. A builder validates the service payload and writes one exact-size
envelope; it never calls `pxa_submit` as a side effect. `pxa_send(builder)`
returns the immediate status. A typed completion decoder validates service,
opcode, token, status and result length before returning a view into the event
buffer. The event view expires after the callback. Apps can use a small
`pxa_dispatch` helper with a caller-supplied array of pending entries, or
switch on tokens directly; neither path allocates.

Service builders and decoders come from the machine-readable schema. The
schema defines field widths, ordering, required and optional records, bounds,
status payload rules and golden vectors. CI generates Host and Guest codecs
from one source, compiles both C and C++ consumers, and runs round-trip,
truncation, unknown-required-field and malformed-length tests. Handwritten
validation remains only for domain rules such as path safety and URL policy.
UI builders should send each small command record in one `TX_WRITE` packet;
large values may span bounded packets without changing the transaction wire
format. The Host applies the batch only at `TX_COMMIT`.

## Migration sequence

1. Generate v0 envelope and record codecs and verify byte-for-byte equivalence
   with the existing golden vectors before changing Guest-visible layouts.
   This is done for 38 Core messages and 13 record lists. Device runtime-info
   and Window snapshot are the first generated Service payloads; the others remain.
2. Add v1 token and envelope primitives beside v0, including generation-wrap,
   stale-Handle, cancellation and queue-pressure tests. The current migrated
   requests cover these paths; expand cross-service tests as services move.
3. Move Core services to the common reserved-completion dispatch; migrate IPC
   and Net workers first, then synchronous services, while retaining their
   existing result schemas.
4. Build v1 Guest SDK adapters and migrate every app and simulator fixture.
   Packages declare exactly one required Core major version. A Host rejects an
   unsupported major before starting the Component. The package tool also
   checks Wasm Core imports against the declared major before AOT compilation
   and signing. The signed simulator has run `jump-jump-3d` through native
   64-bit GameRender and Audio Handles, UI input, Storage persistence, and
   bounded Clock catch-up; other existing apps still need migration.
5. Remove v0 imports, 32-bit Guest Handles and duplicate handwritten codecs
   only after all app packages and firmware targets build and pass simulation.
   The Guest SDK half is done: `pxa_v0.h` and every `PXA_GUEST_LEGACY_V0`
   branch are gone, the SDK head and service headers carry no version suffix,
   and the SDK, app and simulator fixtures compile and run against a single
   surface. The Host half is still pending: `libpxa`/WAMR keep binding
   `pxa.core.v0` with 32-bit Handles for packages signed against the earlier
   generation, and the Host still advertises Core major 0 as primary. Remove
   that binding only once device and store inventories hold no major-0
   packages.

The release gate includes ESP32-S3 AOT build, package verification, Host unit
tests, desktop Guest execution, and hardware measurements of frame latency,
peak internal RAM, peak PSRAM and request completion loss under pressure.
Desktop simulation cannot establish the hardware latency or memory figures.

# Draft 0.1 Status

## Established in this milestone

- App, Component, Artifact, Service and Handle ownership boundaries.
- Separation between PXA ABI, service ABI, package format and engine ABI.
- Single-message control plane and non-blocking handle data plane.
- Twelve-byte message envelope and length-delimited extensible records.
- Asynchronous request acceptance, completion and cancellation rules.
- Opaque, generation-protected, Component-owned Handle semantics.
- Candidate Core statuses, IO operations, readiness flags and lease kinds.
- Reserved top-level service namespace and defined Window, UI/Canvas and Clock
  v1 candidates.
- Reproducible Core golden-vector generation and validation.

These items remain Draft 0.1 and may change before Core 1.0.

## Intentionally not defined yet

Except for the Draft Window, UI/Canvas, Clock, private FS, Storage, IPC, Sensor, Network and Permission v1 extensions,
reserved services have no published opcode assignments yet. In particular,
this milestone does not define network sockets or the audio graph.

The remaining reserved service protocols are pending. The Package manifest,
detached signature envelope, engine compatibility identifier, portable-Wasm
feature negotiation and signed `.pxa` container serialization are now defined
as Draft 0.1. Container tooling and Host ingestion are implemented for the
POSIX/simulator and ESP adapters; device validation remains pending.

## Completed implementation milestone: Core runtime skeleton

The active `libpxa` implementation remains independent of LVGL, WAMR and the
existing PXA prototype. It provides:

1. A bounded decoder validates one complete control envelope and record list.
2. A request table enforces nonzero uniqueness and exactly-one completion.
3. A typed Handle table rejects stale generations and cross-Component access.
4. A bounded event mailbox distinguishes coalescible and reliable events.
5. Component termination atomically invalidates requests, handles and leases.
6. Host tests consume the checked-in golden vectors.
7. Deterministic malformed-input pressure tests, also runnable with address and
   undefined-behavior sanitizers.

The implementation remains a Draft candidate rather than the stable PXA ABI.

## Completed milestone: private FS v1

This milestone defines and tests:

1. private App identity roots, canonical UTF-8 relative paths and logical-byte
   quotas;
2. asynchronous file and directory Handle operations with bounded result
   records;
3. generation-protected Core Handle ownership, explicit close and lifecycle
   cleanup;
4. no-follow POSIX reference storage and an ESP LittleFS backend with matching
   service semantics;
5. Guest SDK request/result helpers, malformed-path pressure tests and a
   simulator service bridge.

The ESP LittleFS backend is source-integrated. Device build, flash and
persistence verification remain pending explicit device-build approval.

## Completed milestone: Permission v1 policy and management

This milestone defines and tests signed Manifest declarations, exact-scope
allow/deny policy, required-permission activation gating, Component-local
authority Handles and authority revocation. Both ESP NVS and the simulator
persist grants by signed App identity and route Permission v1 requests and
revocation events. The application manager displays every declared permission,
its required flag and current grant; it can change grants only while the App is
not running.

No product permission is granted by default merely because it appears in a
Manifest. Device build, flash and interactive persistence verification remain
pending explicit device-build approval.

## Completed milestone: private Storage v1

Storage v1 defines a bounded, identity-scoped App key/value service with
asynchronous `get`, `set`, `remove` and ordered `list`. The candidate Host
stores full snapshots in two CRC-protected Host-reserved slots under the same
private-data root as FS v1. This preserves App data-clear semantics while
keeping recovery files out of the Guest FS namespace and its quota.

The ESP and simulator bridges advertise service 6, and the Guest SDK plus a
`pxa-kv-counter` test App exercise the protocol. Device build, interactive
restart and power-loss validation remain pending explicit approval. Secrets is
still intentionally reserved: Storage makes no confidentiality, key-management
or backup claim.

## Completed milestone: IPC v1 core candidate

IPC v1 reserves a small asynchronous request/reply contract for Components in
one activated signed App. Its bounded Broker registers endpoint names to running
Components, delivers only reliable request/result events, rejects replies from
other Components and cancels calls when the provider stops. It never invokes a
Guest reentrantly and does not expose shared linear memory.

The ESP and simulator Hosts activate signed endpoint provider Components after
the foreground `main` UI Component, then route each endpoint to its provider
instance. The simulator keeps distinct WAMR module instances and execution
environments per Component, and routes Host imports through module custom data.
The IPC Echo Lab contains an independent `responder` service Component to
exercise that route. Interactive runtime verification remains pending.
Cross-App routing, stream transport, timeout and ACL semantics remain
intentionally undefined. Secrets also remains reserved until the product
supplies a reviewed protected-key-storage authority.

## Completed milestone: Execution Lease v1 candidate

Core service 1 now implements asynchronous `acquire-lease` requests with a
Component-owned lease Handle and a reliable `lease-revoked` event on expiry or
Host policy revocation. The service is intentionally narrow: it grants only
scheduling eligibility, while Audio, Net and Sensor remain separately
permissioned services. The core tests cover acceptance, limits, Handle
ownership and expiry; the Guest SDK supplies bounded encoders and parsers.

Both Hosts route the low-level Core request for specialized resource owners.
The simulator checks expiration on its LVGL timer; ESP serializes it through
the runtime command queue so no Guest callback is re-entered from the ESP timer
task. Lease is not the application background-work API; Work owns worker
lifetime and deadline handling.

## Completed milestone: Sensor v1 candidate

Sensor v1 defines semantic Host descriptors, exact-scope `sensor.read`
Permission Handles and bounded, coalescible signed sample events. It deliberately
does not define board buses, vendor register maps or a persistent physical-device
identity. Core tests cover discovery, interval validation, scoped grants and
authority revocation, including a later `ambient.light` semantic descriptor and
new nonzero unit registry value without a Core ABI change. The implementation
now treats units as opaque nonzero registry values, so later entries require no
Core implementation change. The simulator exposes synthetic ambient-temperature
and ambient-light Providers for validation; the current ESP target exposes an
empty Provider until a reviewed board integration exists. `pxa-sensor-lab` and
`pxa-light-lab` exercise the full permission, discovery and subscription
sequence. Interactive validation remains tracked separately.

## Completed milestone: Network v1.1 candidate

Network v1.1 defines bounded asynchronous HTTP(S) GET, HEAD, POST, PUT, PATCH and
DELETE with filtered request/response headers, inline request bodies, timeouts
and explicit response body metadata. Every request and response Stream remains
bound to an exact-origin `net.client` authority; DNS/TLS/CA policy stays in the
Host and Guest imports never perform blocking network work. Core tests cover
validation, asynchronous completion, limits and Stream reads. The simulator's
deterministic `pxa-net-lab` suite mirrors 12 real Postman Echo success,
compatibility and failure scenarios. The ESP Provider uses `esp_http_client`
with the ESP CA bundle and
active default route, or the board NetworkInterface when only an AT HTTP
transport is available. Hardware validation remains required for each
supported transport. Sockets and WebSocket remain reserved; redirects are
surfaced as HTTP responses rather than followed automatically.

## Decisions made in this milestone

- Reliable events use reserved mailbox capacity. A full mailbox returns `busy`;
  request completion remains pending until admission succeeds.
- Handle bit layout is not public ABI. The candidate Host uses 16-bit slot and
  16-bit generation fields and permanently retires a slot when its generation
  is exhausted.
- Closing a Handle is single-success: stale, repeated and cross-Component close
  attempts return `not-found`.
- Failed asynchronous results contain only the common leading status for now.
  A diagnostic record namespace will be added only if at least two services
  demonstrate a shared need.

## Completed milestone: lifecycle and first extension review

This milestone defines and tests:

1. Explicit `created -> starting -> running -> stop-requested -> stopping ->
   stopped` transitions and non-reentrant Guest callbacks.
2. Core imports during start/event, with imports forbidden during stop.
3. Cancellation commit points, retained FIFO completions and atomic authority
   revocation for uncommitted requests and Handles.
4. Window 0.1.0 configuration, complete metric snapshots, Insets, system-bar
   modes, version negotiation and system-back default action.

Window remains backend-neutral. It contains no LVGL object, App header, toolbar
or page-navigation concept.

## Completed milestone: Package and Artifact selection

This milestone defines and host-tests:

1. A bounded canonical binary manifest and detached P-256 signature envelope.
2. Complete SHA-256 payload inventory and safe package path rules.
3. Multiple Components, portable Wasm and multiple target/engine-specific AOT
   Artifacts in one Package.
4. Deterministic AOT selection with exact engine ABI matching and portable Wasm
   fallback.
5. Core/Service activation requirement checking before Artifact loading.
6. App identity and private-data ownership bound to publisher lineage root plus
   App ID, invariant across version upgrades and authorized key rotations.

Cryptographic verification is exposed through a Host adapter. The Core
candidate constructs the signed domain and enforces identity, but does not bind
ESP32 to a specific crypto implementation.

## Completed milestone: `.pxa` container and managed updates

This milestone adds a canonical single-file container with independently
authenticated LZ4 blocks, bounded-memory two-pass installation and atomic slot
commit on both Hosts. Manifest 0.2 adds signed release ordering and planned
publisher-key lineage. Downgrades require an explicit warning instead of being
unconditionally rejected; revoked signer rollback remains denied.

## Completed milestone: installer and multi-Component activation

The transaction and activation semantics are specified in `installer.md` and
`lifecycle.md`. The independent candidate and its fault-injection coverage now
provide:

1. directory reader with no-follow traversal, bounded reads and streaming
   SHA-256;
2. ESP32 mbedTLS and simulator OpenSSL signature-verifier adapters;
3. identity-scoped Package update, recovery and rollback without private-data
   mutation;
4. on-demand activation of multiple independent Components through the Core
   lifecycle.

## Completed milestone: platform storage adapters

1. Separate the three-slot transaction state machine from POSIX file APIs.
2. Exhaustively test recovery states and injected transition failures in
   memory.
3. Keep the simulator POSIX adapter's no-follow and directory durability
   guarantees.
4. Provide an ESP32 LittleFS VFS adapter with an in-process identity lock,
   file synchronization and atomic rename, without pretending arbitrary VFS
   filesystems have LittleFS semantics.

The shared state machine is tested across all 81 absent/verified/corrupt slot
combinations and injected install/rollback storage failures. The POSIX suite
passes under address and undefined-behavior sanitizers. The ESP adapters compile
as isolated translation units with the configured ESP-IDF 5.5.4 target compiler
and flags.

## Completed milestone: ESP integration foundation

1. The reviewed Draft Core, Package, activation and LittleFS sources have stable
   component registration behind `CONFIG_PXA_ENABLED`.
2. The ESP trust store accepts only product-supplied canonical DER P-256 SPKI
   keys, derives key IDs locally and denies all publishers by default.
3. A bounded built-in source manager recovers installed identities and performs
   verified, transactional, idempotent synchronization on the mounted `assets`
   LittleFS partition.
4. Built-in source, user inbox and managed roots are canonical and
   non-overlapping. The old
   JSON/AOT AppStore path has been removed.
5. Slot verification binds every retained Package to the identity encoded by
   its directory. Trust removal revokes future verification without deleting
   code or private data.

Target syntax checks cover both experimental switch states. No ESP-IDF build,
flash, monitor or device mutation was performed in this milestone.

## Completed milestone: ESP activation bridge (source integration)

1. The WAMR `ComponentEngine` adapter owns an independent module,
   instance, execution-environment and resource ownership per Component.
2. The concrete ESP `HostProfile` and `ActivationProfile` use an
   exact WAMR AOT engine ABI identifier and portable Wasm fallback behavior.
3. Launches use the complete publisher/App identity and reverify the `current`
   root before activation through `ActivationCoordinator`.
4. Canvas commits are validated off the LVGL task and applied asynchronously;
   touch, Clock and system-back events are serialized on the runtime pthread.
5. The generic App catalog loads signed Package metadata and bounded PNG icons.
6. Guest callbacks have a watchdog deadline, while transient tick and pointer
   state use bounded/coalescible queues.

No ESP-IDF build, flash, monitor or hardware mutation was performed during
this source-integration milestone.

## Completed milestone: App management foundation

1. Separate product-owned built-in sources, user staging inboxes, verified
   executable slots, enable policy and private-data ownership.
2. Add identity-locked executable uninstall without implicit data deletion.
3. Add install, uninstall, clear-data, enable and disable Host actions with
   active-Component guards and launch-time policy revalidation.
4. Add a generic App manager page, destructive-action confirmation, MCP tools
   and a persistent simulator behavior self-test.
5. Keep mutable state in `/assets/pxa-state`, outside the system-assets
   transaction roots, so space is shared while normal online asset updates
   retain installed slots, the user inbox and private files.

This management contract adds no Guest imports, Core opcodes or Service ABI
assignments.

## Next milestone: device validation and service expansion

1. Run the target build and board smoke tests only after explicit approval.
2. Exercise reset/fault injection between every slot transition on real flash,
   then test multi-Component Wasm/AOT lifecycle isolation on hardware.
3. Replace the fixed board Window snapshot with a display-owned metrics source.
4. Define the next reviewed service protocol, beginning with private files and
   quota policy rather than exposing the raw device filesystem.

## In-progress milestone: private files FS v1

The Draft FS v1 service now has machine-readable assignments, golden control
vectors, a Guest request encoder and a host-only POSIX reference backend.
The reference backend validates UTF-8 private-relative paths, performs
descriptor-relative no-follow traversal, accounts logical regular-file bytes
against a per-App quota, and maps resources into existing Component-owned file
and directory Handles. Its tests cover path escape attempts, symbolic links,
quota, directory enumeration, cross-Component Handle rejection and teardown.

The ESP LittleFS backend and PXA ESP runtime bridge now use the same service
contract and bind each active App to `/assets/pxa-state/data/<app-id>`.
The host suite includes deterministic malformed-path pressure coverage. Device
build, flash and LittleFS persistence verification remain explicitly pending.

## In-progress milestone: Work v1

Work service 13 has one enqueue/cancel/complete contract with no legacy Job
wire path. A worker receives its Work ID, attempt, input and monotonic deadline;
it reports success, retry or failure. Both Hosts send a deadline stop request,
allow 500 ms for cooperative completion, then stop and retry within the attempt
limit. Queued Work uses the per-App `work.v1` store. The simulator and ESP have
matching resident-Package source integration and the two-Component
`pxa-lab` includes the end-to-end Background Work module. Calendar scheduling, trusted
reboot recovery and concurrent background execution from multiple Packages are
not claimed. ESP target build, flash and device timing validation remain
pending explicit approval.

## In-progress milestone: Audio playback v1.1

Audio service 10 now defines a narrow, permission-bound media session and
atomic App-owned speaker graph snapshot: gain plus bounded parametric EQ. The
Core service owns session Handles and closes them on Component cleanup or
authority revocation. `pxa-audio-lab` exercises permission acquisition, session
creation and graph commit. Minor 1 adds bounded signed-16-bit PCM writes using
`pxa_io(handle, PXA_IO_WRITE, ...)`; a write is at most one negotiated frame and
returns `would-block` for queue pressure. The simulator remains a deterministic
silent Provider. The ESP bridge copies Guest PCM into a bounded queue, then
mixes up to three sessions through the device's Game input streams without
entering Wasm from an audio task. Its 16 kHz mono, 20 ms format, volume and
focus policy remain Host-owned. Capture and encoded packet formats remain
reserved; target build and device audio validation remain pending approval.

## In-progress milestone: Simulator protocol trace

The simulator now has a bounded, metadata-only trace recorder at the decoded
Guest-control and Host-event boundaries. `PXA_SIMULATOR_TRACE` exports JSONL
on orderly shutdown for reproducible message-order diagnostics. It records no
raw payload bytes, so it is not a packet archive and cannot be used to recover
private App content. The self-test verifies Guest-control export; interactive
and Clock Host-event export; broader interactive coverage remains tracked in
`UNVERIFIED.md`.

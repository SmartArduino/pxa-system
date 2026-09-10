# libpxa

PXA means **Portable eXecutable Application**. The PXA Platform is the Host,
service contracts, and tools for that portable execution and packaging model.

`libpxa` is the platform-neutral C99 implementation of the PXA Host Core. It
contains no ESP-IDF, RTOS, filesystem, crypto, graphics, Wasm-engine or C++
dependency.

The compatibility policy is documented in `ABI.md`; internal dependency and
ownership rules are documented in `ARCHITECTURE.md`; the architecture audit
disposition is recorded in `AUDIT.md`. The library is distributed under the
repository's MIT license.

The library is deliberately single-owner-thread. Platform interrupt handlers
and worker threads must enqueue commands to their Host runtime instead of
calling `libpxa` directly. This keeps Core operations lock-free and preserves
the PXA non-reentrancy contract.

Runtime memory is supplied by the caller. After `pxa_runtime_init()` the Core
does not allocate memory. Capacities and event-buffer block size are selected
through `pxa_runtime_limits_t`; `pxa_runtime_workspace_size()` reports the exact
required allocation, including alignment allowance.

Registered service objects and their workspaces are borrowed by the Runtime.
They must remain valid until after `pxa_runtime_deinit()` returns. The Runtime
notifies each registered service exactly once when a live Component reaches the
stopped state.

Implemented modules:

- bounded wire and record decoding plus bounded direct encoding;
- Component lifecycle and Guest-import legality;
- generation-protected Component references and resource Handles;
- asynchronous request, cancellation, commit and authority-revocation rules;
- bounded reliable/coalescible event mailboxes backed by fixed block pools;
- atomic Component cleanup with resource callbacks after Core state cleanup;
- Window, Lease, Permission, IPC, Storage, Filesystem, Sensor, Scheduler,
  Network and Audio services with C backend tables;
- UI 0.3 with atomic patch/subtree/surface transactions, a dynamically sized
  structural node registry, lazy Canvas storage, VirtualList ranges, Surfaces,
  environment changes and semantic resource-pressure events;
- zero-copy package manifest parsing, signature verification dispatch,
  inventory/requirement validation and deterministic artifact selection;
- fixed-capacity activation planning/coordinating and recoverable package-slot
  transactions.

Core, Package and non-UI Guest wire compatibility is defined by
`spec/draft-0.1`. UI service 0.3 is defined separately by
`spec/draft-0.2`; it is a deliberate breaking replacement for
UI 0.1.

## Layer 2 adapters

`adapters/` holds the reference platform adapters that implement the Core
backend tables. They are optional: the Core library itself has no dependency
on them. Enable with `PXA_BUILD_ADAPTERS` (default when building libpxa
standalone). Built targets: `pxa::adapters_openssl`, `pxa::adapters_posix`,
plus `pxa::adapters_wamr` and `pxa::adapters_lvgl` when the corresponding
source trees are supplied.

The POSIX adapter requires OpenSSL and LZ4 development packages. If a bundled
LZ4 source tree is preferred, pass
`-DPXA_LZ4_SOURCE_DIR=<directory-containing-lz4.c-and-lz4.h>`; an in-repository
`components/lz4` checkout is detected automatically.

- `openssl/` — SHA-256 (one-shot and streaming) plus ECDSA P-256 / SHA-256
  package-signature verification (`pxa_openssl_p256_verify`, usable directly
  as the `pxa_package_signature_verify_fn` callback). Trusted publishers are
  supplied by the host as canonical DER SubjectPublicKeyInfo entries.
- `posix/` — Linux/POSIX backends:
  - `pxa_posix_fs` — FS service backend with quota accounting, no-follow
    openat walks, link-count checks and reserved `.pxa-` name handling;
  - `pxa_posix_storage` — Storage service backend, one file per key with
    fsynced writes and cursor-ordered listing;
  - `pxa_posix_scheduler_store` — durable Scheduler store encoded in one
    Storage-backend record, with version and corruption checks;
  - `pxa_posix_installer` — recoverable signed Package installer using the
    Core three-slot transaction (`slot_transaction.h`), flock identity locks
    and fsync barriers. Mirrors the legacy C++ `PosixPackageInstaller`
    semantics.
- `wamr/` — Component engine over WAMR (`pxa_wamr_engine`): loads one
  Artifact per Component through a host `read_artifact` callback, binds the
  `pxa.core.v0` natives to `pxa_runtime_control` / `pxa_runtime_io`, calls the
  guest exports `pxa_app_start` / `pxa_app_on_event` / `pxa_app_stop`, and
  delivers events through transient `module_malloc`
  buffers. The optional `prepare_start` callback lets the host bind
  per-component services (e.g. Window) before `pxa_app_start`, mirroring the
  former C++ host's start ordering. Optional guest-call deadline aborts overdue
  calls with `wasm_runtime_terminate` when the host polls
  `pxa_wamr_engine_poll_deadlines`.

- `lvgl/` — the direct LVGL backend for UI 0.3 (`pxa_lvgl_ui`). LVGL owns the
  property-complete widget tree; Core retains only structural metadata for
  validation, ownership and event routing. The backend applies atomic
  patch/subtree/surface replacements on the LVGL thread, supports lazy Canvas
  display lists and VirtualList visible ranges, retains each Canvas image once
  per committed frame instead of resolving it during every draw, and translates
  LVGL input to generation-tagged PXA events. Asset callbacks use explicit
  acquire/release ownership so Hosts can implement bounded reference-counted
  LRU caches. Optional lock callbacks cover hosts with
  separate UI and runtime threads; a single-owner-thread host leaves them
  NULL. A parent project should pass its configured LVGL target with
  `-DPXA_LVGL_TARGET=<target>`; standalone adapter tests can instead use the
  source-tree fallback `-DPXA_LVGL_SOURCE_DIR=<path>`. The host test runs a real
  LVGL instance with a virtual display and a simulated pointer input device.

The WAMR adapter defaults to the bundled upstream checkout at `../wamr`.
Override it with `-DPXA_WAMR_SOURCE_DIR=<path>` when embedding libpxa elsewhere.
WAMR's interpreter installs its own signal
handlers, so the third-party `vmlib` is compiled unsanitized while the
adapter and tests remain fully covered.

Adapters follow the Core single-owner-thread contract. Services that need
dynamic state use explicit caller-selected byte budgets and allocator
callbacks; the UI service never imposes fixed node, depth, Canvas or Surface
counts. Tests:
`adapters/tests/adapters_test.c` covers the POSIX/OpenSSL backends, including
interrupted-install recovery through the `pxa_slot_faults_t` checkpoint
injector and scheduler-store save/recreate/load/corruption cases;
`adapters/tests/wamr_engine_test.c` runs a freestanding wasm guest
(clang `--target=wasm32`, compiled as `wamr_guest`) end to end: FS open +
`pxa_io` write + handle close, then Storage SET/GET round trips, through the
full runtime, services, activation coordinator and engine;
`adapters/tests/lvgl_ui_test.c` covers direct tree creation, partial patch,
subtree replacement, Canvas draw/input, VirtualList visible ranges and cleanup
against a real LVGL instance;
`adapters/tests/ipc_engine_test.c` is the multi-component integration: an IPC
provider (service component) is activated lazily when the caller's first
request reaches its declared endpoint. They then talk through the C broker,
with the caller persisting the verified reply into Storage for the host to
observe. A resolver failure completes only that IPC request and leaves the
caller running. The Host flushes unresolved calls after the current Guest
callback returns, so provider activation never re-enters WAMR. The engine
caches guest entry points at instantiate time
(`start_fn`/`event_fn`/`stop_fn`), which keeps behavior stable with several
modules loaded and avoids repeated lookups.
`adapters/tests/sensor_engine_test.c` covers the permission + sensor chain:
the guest acquires a `sensor.read` permission (pre-granted through the
in-memory permission store), lists sensor descriptors, subscribes to the
temperature channel with the permission handle, and a host `pxa_sensor_poll`
produces a sample the guest verifies before persisting the result through
Storage.
`adapters/tests/scheduler_engine_test.c` covers lease + scheduler: the UI
guest acquires a foreground lease on the fake clock and schedules a job; the
host takes the due job and activates the job component with the schedule
config (pre-registered through `pxa_wamr_engine_set_config`), then revokes
the expired lease, which the guest observes.
`adapters/tests/net_audio_engine_test.c` covers net + audio: the guest sends a
v1.1 HTTPS POST with a request header, inline body and selected response header
through the simulated net backend, verifies response metadata, reads the body
with `pxa_io`, opens an audio session and commits a speaker graph through the
counting audio backend.
## Simulator on the C stack

The desktop simulator (`simulator/`) no longer uses the C++ core. Its host is
`simulator/pxa_c_host.c`, a pure-C file implementing the `simulator_pxa_*`
interface on libpxa: the C installer installs the signed built-in apps, all C
services run with simulated backends, the C WAMR engine executes the apps and
the C LVGL UI backend renders them, with a maintenance timer driving lease
expiry, sensor samples, net polls, scheduler due jobs and clock ticks. Both
simulator self-tests pass:

```sh
cmake -S simulator -B /tmp/sim-build
cmake --build /tmp/sim-build --target app_pages_simulator -j
SDL_VIDEODRIVER=dummy /tmp/sim-build/app_pages_simulator --pxa-management-self-test
SDL_VIDEODRIVER=dummy /tmp/sim-build/app_pages_simulator --pxa-trace-self-test
```

## Platform contract

`libpxa` does not open files, create threads, read clocks, verify signatures,
perform network I/O, render UI or load Wasm. The embedding Host supplies those
operations through callback tables. Callbacks execute synchronously on the
Core owner thread unless an API explicitly starts an asynchronous provider
operation. Provider completion must be posted back to that owner thread.

Manifest and activation-plan fields are borrowed views. The encoded manifest,
manifest workspace and activation-plan workspace must outlive every activation
that references them. Registered service workspaces must outlive the Runtime.

## Runtime permission prompts

`pxa_permission_service_t` can defer an `ACQUIRE` for a declared optional
permission through `pxa_permission_prompt_fn`. The callback only queues the
platform's user interaction; it must not block the Core owner thread. The
platform posts the decision back on that owner thread with
`pxa_permission_prompt_complete()`, which persists an allow decision and
then completes the original guest request with a scoped capability Handle.
Undeclared permissions and required permissions never enter this prompt path.
`pxa_permission_revoke()` invalidates existing Handles and emits the
permission-revoked event before dependent services can continue to use them.

Engine cleanup has a strict rollback contract: `destroy` is called after every
attempted `instantiate`, including failure; after a failed `start`, `stop` is
called with `PXA_STOP_FAULT` before `destroy`. During `destroy` the Core
component still exists in `PXA_COMPONENT_STOPPED`; it is removed immediately
after the callback returns.

## Integration

Use the aggregate header when the whole API is needed:

```c
#include <pxa/pxa.h>
```

As a CMake subdirectory, link `pxa::pxa`. Installed packages export the same
target through `find_package(pxa CONFIG REQUIRED)` plus the installable
`pxa::adapters_openssl` and `pxa::adapters_posix` targets. WAMR and LVGL bind
external build-tree targets and are not installed as standalone binaries.
`BUILD_SHARED_LIBS=ON` is supported for host use; embedded builds default to a
static library.

The ESP platform uses `components/pxa/src/pxa_esp_host.c` to run the C Core,
services, WAMR engine adapter and LVGL UI adapter. Its public
`pxa_host_*` facade and bundled development trust provider are C; the legacy
C++ Core is not linked into the ESP component. Neither is a dependency of the
standalone library.

Host-only tests can be run without ESP-IDF:

```sh
cmake -S pxa-system/libpxa -B /tmp/libpxa-build
cmake --build /tmp/libpxa-build
ctest --test-dir /tmp/libpxa-build --output-on-failure
```

All libpxa sources, tests and golden data required by these commands live under
`libpxa`; the directory can be copied and built outside this repository once
the documented OpenSSL and LZ4 development dependencies are installed.
The cross-repository container-tool integration test is enabled when
`PXA_PACKAGE_TOOLS_DIR` points to the canonical package generators; the
monorepo checkout is detected automatically.
With sanitizers:

```sh
cmake -S libpxa -B /tmp/libpxa-build-asan \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build /tmp/libpxa-build-asan
ctest --test-dir /tmp/libpxa-build-asan --output-on-failure
```

## Performance

Run the portable C runtime microbenchmark with:

```sh
PXA_BENCH_RUNS=3 pxa-system/libpxa/bench/run.sh
```

The benchmark measures snapshot, event delivery and request round trips for the
portable C runtime. Results depend on the compiler, CPU and configured runtime
limits. Native integrations choose their exact capacity and byte-pool tradeoff
with `pxa_runtime_limits_t`.

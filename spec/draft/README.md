# PXA Current Draft

[简体中文](README.zh-CN.md)

This directory is the single current draft. Current Hosts and Packages use UI
service 0.3; no UI 0.1 translation or compatibility implementation exists.
Each machine-readable specification carries its real `protocol_version`.

PXA is a small, capability-based application runtime for resource-constrained
interactive devices. This draft replaces the prototype's app-specific ABI with
a platform-neutral Core and independently versioned services.

Status: **design draft; no compatibility guarantee**.

## Documents and sources

- [architecture.md](architecture.md) defines ownership and execution rules.
- [core-wire.md](core-wire.md) defines the Core message, request, handle and IO
  semantics.
- [lifecycle.md](lifecycle.md) defines Component callbacks, stop and
  cancellation commit points.
- [window.md](window.md) defines the first independently versioned service.
- [ui.md](ui.md) defines UI service 0.3, retained transactions and the
  backend-neutral Canvas display-list protocol.
- [surface.md](surface.md) defines Surface ownership and composition.
- [clock.md](clock.md) defines bounded periodic monotonic wake-ups.
- [fs.md](fs.md) defines private App files, path safety and quota semantics.
- [storage.md](storage.md) defines bounded private App key/value storage.
- [ipc.md](ipc.md) defines asynchronous intra-App Component calls and replies.
- [sensor.md](sensor.md) defines semantic, permission-bound sensor subscriptions.
- [device.md](device.md) defines permission-bound interface MAC identifiers.
- [net.md](net.md) defines permission-bound asynchronous HTTP(S) fetches.
- [audio.md](audio.md) defines permission-bound media sessions and atomic graph control.
- [work.md](work.md) defines bounded, retryable background Work.
- [permission.md](permission.md) defines signed declarations, policy grants,
  runtime authorities and revocation.
- [package.md](package.md) defines signed Packages, Component Artifacts and
  deterministic Wasm/AOT selection.
- [container.md](container.md) defines the canonical signed `.pxa` single-file
  transport, bounded LZ4 encoding and update profile.
- [installer.md](installer.md) defines secure directory ingestion, atomic
  update, crash recovery and rollback without touching private data.
- [app-management.md](app-management.md) defines built-in and user sources,
  enable policy, uninstall and private-data lifecycle.
- [pxa-core.json](pxa-core.json) is the machine-readable source of truth for
  Core symbols and numeric assignments.
- [pxa-container.json](pxa-container.json) is the machine-readable source of
  truth for `.pxa` container layouts and numeric assignments.
- [pxa-window.json](pxa-window.json), [pxa-ui.json](pxa-ui.json),
  [pxa-clock.json](pxa-clock.json), [pxa-fs.json](pxa-fs.json),
  [pxa-storage.json](pxa-storage.json), [pxa-ipc.json](pxa-ipc.json),
  [pxa-sensor.json](pxa-sensor.json), [pxa-net.json](pxa-net.json),
  [pxa-audio.json](pxa-audio.json), [pxa-permission.json](pxa-permission.json),
  [pxa-device.json](pxa-device.json), and [pxa-work.json](pxa-work.json) define
  published Draft service assignments. [pxa-surface.json](pxa-surface.json)
  defines the bulk-pixel Surface service.
- `golden/` contains reproducible Core, Package and UI 0.3 wire examples.
- `tools/check_spec.py` validates the specification and golden vectors without
  third-party dependencies.

## Service namespace

The current draft reserves the following top-level services. Reserving a service ID is
not the same as publishing that service ABI.

| ID | Service | Responsibility |
| ---: | --- | --- |
| 1 | `core` | Lifecycle support, handles, cancellation, leases and readiness |
| 2 | `window` | Window 0.1.0: metrics, insets, system UI and navigation |
| 3 | `ui` | Declarative UI tree, Canvas node and input events |
| 4 | `clock` | Monotonic time, timers and animation-frame requests |
| 5 | `fs` | Package, private, cache, temporary and granted files |
| 6 | `storage` | Bounded private App key/value storage |
| 7 | `ipc` | Asynchronous intra-App Component request/reply brokerage |
| 8 | `sensor` | Semantic sensors and batched samples |
| 9 | `net` | HTTP, WebSocket, TCP and UDP capabilities |
| 10 | `audio` | Permission-bound media sessions and atomic graph control |
| 11 | `permission` | Grant state, requests and revocation events |
| 12 | `secrets` | Protected credentials, keys and certificates |
| 13 | `work` | Bounded, retryable activation of declared worker Components |
| 14 | `wasi` | WASI Preview 1 reactor and explicitly bounded ambient capabilities |
| 15 | `device` | Permission-bound physical interface identifiers |
| 16 | `surface` | Host-owned bulk-pixel BufferQueue and composition |

Canvas is not a top-level service. It is a UI node plus a length-delimited
display-list subprotocol, so a UI transaction can atomically create and update
a Canvas.

## Draft discipline

- Numeric values in this draft may still change.
- Once a service reaches 1.0, published IDs are never reused.
- Host implementation details such as LVGL, WAMR, LittleFS and FreeRTOS are not
  protocol concepts.
- A draft service must have golden vectors and malformed-input tests before an
  implementation is treated as authoritative.

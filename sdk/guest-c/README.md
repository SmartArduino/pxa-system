# PXA Guest SDK

The C SDK targets PXA Core 0.1.0 through exactly two non-blocking imports from
module `pxa.core.v0`: `pxa_control` for one complete control message and
`pxa_io` for bounded Handle reads and writes. Apps export:

```c
uint32_t pxa_app_api_version(void);
int32_t pxa_app_start(const uint8_t* config, uint32_t length);
int32_t pxa_app_on_event(const uint8_t* event, uint32_t length);
void pxa_app_stop(uint32_t reason);
```

Callbacks are serialized. Imports are legal during start and event callbacks,
but forbidden during stop; the Host revokes Component resources before stop.

`pxa.h` contains the Core envelope, record helpers, Window fullscreen helper
and periodic Clock helper. `pxa_ui.h` builds atomic UI 0.3 tree transactions.
`pxa_ui_builder.h` is a streaming tree builder that retains only the current
ancestor path, and `pxa_ui_components.h` supplies semantic compositions such
as buttons and VirtualList. The opinionated page template used by the lab applications is
in `apps/pxa/common/pxa_ui_demo_page.h`, rather than the SDK. `pxa_canvas.h`
adds the general Canvas display-list node with
rectangles, circles, lines and UTF-8 text. Canvas is a UI node, not a game ABI.
`pxa_fs.h` builds asynchronous private-file requests and parses their results.
It uses Core file and directory Handles plus `pxa_io`; it does not expose host
paths, descriptors or a blocking filesystem API.
`pxa_permission.h` checks or acquires a signed Manifest permission; it cannot
grant access, which remains a Host policy and application-management action.
`pxa_work.h` enqueues a declared worker, parses its activation context, reports
success/retry/failure and handles cancellation or deadline stop requests. Apps
do not acquire an execution lease for Work; the Host owns that lifetime.
`pxa_lease.h` is a low-level resource-lifetime API for specialized foreground,
audio, network and sensor integrations, not a background-task API.
`pxa_sensor.h` discovers semantic sensor descriptors, subscribes with an exact
`sensor.read` permission Handle and parses coalescible samples. It never
exposes board buses, device registers or vendor driver types.
`pxa_device.h` reads one explicitly selected interface MAC through an exact
`device.identity` Permission Handle. Its result is raw six-byte data; the
Guest chooses any protocol-specific text representation.
`pxa_net.h` retains the v1.0 bounded HTTP(S) GET helper and adds v1.1 GET, HEAD,
POST, PUT, PATCH and DELETE requests. v1.1 supports bounded request headers,
an inline request body, per-request timeout, selected response headers, known
body length and an optional read-only response body Stream. Every request uses
an exact-origin `net.client` Permission Handle.

Canvas frames and UI trees have no ABI-defined count or depth limit. The Host
charges transaction, Canvas, node-registry and asset allocations to explicit
byte budgets and reports resource exhaustion instead of truncating content.
`pxa_canvas_present()` creates the Root and Canvas on its first successful
generation and patches only the display list thereafter. Pointer payloads
contain Surface, node, committed generation, phase and logical coordinates;
stale-generation events are discarded with `pxa_ui_event_is_current()`.
Clock ticks contain a monotonic `u64` microsecond timestamp; events may be
coalesced.

The startup environment describes logical viewport size, density, font scale,
safe insets, input capabilities, theme direction and optional feature bits.
`pxa_ui_parse_start_environment()` reads it from startup configuration without
requiring the App to understand unrelated Core configuration records.
Baseline tree controls require no handshake. Apps test optional features before
using Canvas, VirtualList, Grid, media, additional Surfaces or other extensions,
and rebuild responsive layout after `ENVIRONMENT_CHANGED`. Resource pressure is
reported semantically as normal, constrained or critical; raw Host memory is
not application ABI.

The product integration Apps under `apps/pxa` demonstrate Canvas, private FS
and Permission v1 with the same lifecycle. A standalone consumer can keep Apps
elsewhere and set `PXA_APP_SOURCE_ROOT`. Their `package.json` files are source metadata, not
the installed ABI manifest. `permissions` declares signed policy declarations;
`services` is an optional unique list of additional required service names:
`core`, `window`, `ui`, `clock`, `fs`, `storage`, `ipc`, `sensor`, `net`,
`audio`, `permission`, `secrets`, or `device`. The packager maps names to stable ABI IDs;
Core is included for every Component automatically. Legacy source metadata
describes one `main` UI Component from `main.c`. A
`components` array may instead describe multiple Components with stable `id`,
`kind`, `source`, and optional Component-local `services` fields. UI Components
receive Window, UI and Clock; service and job Components do not. IPC callers
must declare `ipc`; an `ipc_endpoints` provider receives it automatically.
Package Component records and endpoint routes are sorted by ID/name, so the
generated signed manifest stays canonical.
`build_package_manifest.py` produces the canonical signed `manifest.pxm` and
complete SHA-256 file inventory.

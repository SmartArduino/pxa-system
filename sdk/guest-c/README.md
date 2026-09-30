# PXA Guest SDK

The Guest SDK has a single API surface. Include `pxa.h` for Core status codes
and lifecycle events, then include only the service headers you use, such as
`pxa_clock.h` or `pxa_ui.h`. Header, function and macro names carry no version
suffix, and the SDK never ships a second API generation beside the current one.
Only the wire ABI keeps its version: application Wasm artifacts import
`pxa_submit` and `pxa_io` from `pxa.core.v1`, and package metadata records the
required Core version through `min_sdk`/`target_sdk`/`compile_sdk`, which
default to SDK 1.0.

In `pxa_ui.h`, `pxa_ui_parse_event`, `pxa_ui_parse_pointer`,
`pxa_ui_parse_controller`, `pxa_ui_parse_environment_event` and related
high-level parsers accept `pxa_event_t` directly. `pxa_ui_theme_get` accepts
a full 64-bit request token. Include `pxa_ui.h` for UI builders and event
parsing; include `pxa_ui_wire.h` only when assembling low-level UI packets
yourself. Shared byte-writing and DrawList wire helpers live in separate,
transport-independent headers, and `pxa_i18n_handle_event` accepts a Core
event.

The [hello example](examples/hello/main.c) builds as a direct package:

```sh
PXA_APP_SOURCE_ROOT="$PWD/deps/pxa-system/sdk/guest-c/examples" \
  PXA_PACKAGE_ARTIFACT_MODE=wasm \
  tools/pxa/package_app.sh hello /tmp/pxa-hello pai-touch
```

The example uses a zero-initialized `uint64_t` token cursor and
`pxa_next_token(&cursor)` for asynchronous requests. Keep one cursor per
Component, pass the returned token to a typed request helper, and match its
completion with that helper's parser. `pxa_lifecycle_parse()` checks the
background/foreground event shape before reading its payload. Event payloads
are borrowed views: process or copy needed data before returning from the
callback. Requests and callbacks use no Guest heap by default.

The default SDK stays header-only for small wire encoders and import wrappers.
Direct and CMake builds compile only the helpers they use, so an additional
Guest library is not required. If larger scene or image state machines become
expensive across multiple translation units, move those implementations to an
optional `.c` library after comparing Wasm/AOT size and build time. The public
declarations can stay in `.h`; keeping one implementation avoids duplicated
maintenance. A `.c` split should not add a second ABI or hidden allocation.

The Core imports are `pxa_submit` and `pxa_io` from `pxa.core.v1`. Its 20-byte
envelope and 64-bit request tokens support Log, Device runtime information and scoped MAC, Window
configure/snapshot/toast, Permission check/acquire, Storage get/set/remove/list,
private FS, IPC call/reply, Net fetch/HTTP, Audio media, Sensor discovery and
subscriptions, Core leases, and GameRender context creation, telemetry I/O
and close, plus the privileged Store Installer, bounded Work jobs, Surface buffers, Clock timers and UI
transactions.
`pxa_device.h`, `pxa_window.h`, `pxa_permission.h`,
`pxa_storage.h`, `pxa_fs.h`, `pxa_ipc.h`, `pxa_net.h`,
`pxa_audio.h`, `pxa_sensor.h`, `pxa_lease.h`, `pxa_work.h`,
`pxa_surface.h`, `pxa_clock.h`, `pxa_ui_wire.h`,
`pxa_store_installer.h`, `pxa_assets.h` and
`pxa_game_render.h` provide typed
builders and completion decoders. Permission also exposes a zero-copy
revocation-event view, valid during the callback. Window exposes typed metrics
and back-request event helpers. GameRender, Permission acquire and FS open
return native 64-bit Handles allocated from the Host resource table with a
32-bit generation. Typed I/O and close validate the full Handle; Permission
Handles authorize Device MAC, while file Handles support read and write.
For a GameRender context, select
`PXA_GAME_RENDER_SCRATCH_DEPTH16`,
`PXA_GAME_RENDER_SCRATCH_NONE`, or
`PXA_GAME_RENDER_SCRATCH_COVERAGE_2BIT` in
`pxa_game_render_options_t`; use `pxa_game_render_map_coord` to map
display input coordinates to the chosen render resolution.
Net requires an exact-origin native Permission Handle and returns a native
Stream Handle. Its builder writes into one caller-owned packet, and response
headers and content type are borrowed views valid during the callback.
Audio uses native Permission and Session Handles for graph control, PCM,
tones and asset playback. After `open_media` succeeds, `commit_gain` offers a
simple speaker graph; wait for its result before starting playback. `beep`
adds a short envelope, `play_file` accepts a bounded NUL-terminated package
path, and `query` / `flush` wrap the asynchronous session requests. The lower
level `commit_graph`, `play_tone` and `play_asset` remain available for custom
EQ/envelopes and reusable command buffers. PCM/tone queues return
`WOULD_BLOCK` without losing existing samples; retry on a later application
tick. Graph gain/EQ apply to PCM/tones; assets use their own play/control gain.
The current ESP/desktop profile mixes three sessions at 16 kHz mono, streams
Ogg Vorbis/Opus, and supports six short U8 PCM file effects. See
[Audio profile](../../spec/draft/audio.md) for queue and playback semantics.
Sensor returns native subscription Handles and reports them in coalescible
sample events; the SDK decodes signed values by descriptor dimension.
Core leases return native Handles and report full-width Handles on revocation.
Work retains its `u32` job ID while Core request tokens use the full 64-bit
range; the SDK builds enqueue/cancel/complete packets and parses worker start
and stop records without allocation.
Surface returns native 64-bit Handles. Its per-frame queue is a one-way
command; mapped buffers require a signed pinned-memory declaration.
Clock `now` returns a reserved completion with its request token; ticks remain
coalescible timestamp events.
UI keeps logical Surface and node IDs at 32 bits while Canvas stream Handles
are native 64-bit resources. `pxa_ui_wire.h` builds one-packet transaction
records and exposes callback-lifetime views for UI input and Surface events.
`pxa_close_handle()` releases any migrated resource type. Store Installer
uses a 64-bit request token and a 24-byte download-progress payload; the ESP
Host grants the service only to its enabled, built-in Store package.
IPC request and result events carry a broker `call_id:u32` in the Core envelope's
token field, zero-extended to 64 bits. This event ID is distinct from the
call/reply request token, which may use the full 64-bit range.
The FS helpers share one UTF-8 path validator in `pxa_fs_path.h`.
`pxa_pending.h` provides an optional fixed-capacity, caller-owned table:
build a packet, call `pxa_send_tracked()`, then match the completion with
`pxa_pending_take()`. Immediate Host rejection removes the pending entry.
One-way commands can call `pxa_submit()` directly.
`pxa_cancel(token)` submits an idempotent Core cancellation command. A
request cancelled before commit completes once with `PXA_STATUS_CANCELLED`;
cancellation after completion is queued leaves that result unchanged.

## File textures and scene resources

Declare `assets` and `game-render` in the package's service requirements.
`pxa_assets_load_texture(token, path)` and `load_palette` submit a single
asynchronous load. Parse the completion with `pxa_assets_parse_result`, check
its status, bind the resulting handle with `pxa_game_render_bind_assets`,
then close the Guest handle. Context bindings and accepted frames own separate
references. Assets 2.0 also provides `prefetch_texture/palette` (no handle;
the warmed object may be evicted) and `status` (a metadata-only snapshot).
Neither replaces LOAD when an application needs a residency guarantee.

Assets 2.0 provides `pxa_assets_read(token, path, offset, max_bytes)` for raw
package data such as maps. Put `map.bin` in the app's `assets/` directory; the
packager automatically indexes it. Installation verifies whole-file digests;
runtime reads do not repeat SHA checks. Reads return the requested count or
stop at EOF, without a 4096-byte block boundary. Declare Assets 2.0 (the current
packager default); recompile and repackage older Guests because their parsers
may reject results that cross the former boundary. Core messages remain
on `pxa.core.v1`.
Textures and audio should stay on their Host consumer paths rather than being
read into Guest memory and uploaded again.

```c
/* Submit during start/event; rejection means there will be no completion. */
int32_t status = pxa_assets_read(token, "assets/map.bin", offset,
                                  PXA_ASSETS_READ_MAX_BYTES);
/* On a matching event, inspect before returning from the callback. */
pxa_asset_read_result_t chunk;
if (pxa_assets_parse_read(event, token, &chunk) && chunk.status == 0) {
    /* Consume chunk.data[0..chunk.bytes) here; copy only if needed later. */
    offset = chunk.offset + chunk.bytes;
    /* offset == chunk.total_bytes means all data has been consumed. */
}
```

The maximum is **4064 data bytes**, allowing the complete result to fit the
existing 4096-byte control-message limit. Reads stop early only at EOF.
An explicit read at EOF returns zero bytes; beyond
EOF is an error. Use fresh full-width tokens and the existing Core cancel API;
cancellation can lose to an already queued result. No extra file handle,
worker, event loop or hidden map cache is required. Memory and queue pressure
can fail a read; retry only through a bounded application policy after the
cause is removed. `resource-scenes` verifies an 8193-byte map, short reads and
EOF while texture loading runs, using tokens above UINT32_MAX. Its maximum
Guest event receive buffer is 4096 bytes, part of its linear memory.

For a small group, include `pxa_asset_scene.h`. The caller owns the scene,
item descriptions and binding array; the helper adds no heap allocation, pixel
cache, thread or event loop. Its request window advances through the same LOAD
API from the app's existing event dispatcher:

```c
static pxa_asset_scene_t scene;
static pxa_game_render_binding_t bindings[2];
static const pxa_asset_scene_item_t items[] = {
    {"assets/tiles.pxr", PXA_ASSET_TEXTURE, 0},
    {"assets/colors.pxr", PXA_ASSET_PALETTE, 0},
};

/* During start/event: reserve two fresh contiguous tokens in the app's
 * namespace. Keep forwarding events even if begin reports a later rejection. */
static int32_t begin_scene(uint64_t first_token) {
    return pxa_asset_scene_begin(&scene, items, bindings, 2, first_token, 2);
}

/* Call from the existing dispatcher, with a live GameRender context. */
static int32_t handle_scene_event(const pxa_event_t *event, uint64_t context) {
    if (!pxa_asset_scene_on_event(&scene, event)) return 0; /* unrelated */
    if (scene.state == PXA_SCENE_FAILED) return scene.status;
    if (scene.state == PXA_SCENE_READY) {
        int32_t status = pxa_asset_scene_bind(&scene, context);
        if (status < 0) return status; /* preserves old binding and new handles */
        status = pxa_asset_scene_release(&scene);
        if (status < 0) return status;
        /* The app can now submit DrawLists using the context's bindings. */
    }
    return 1;
}
```

The app handles reported failures in its loading screen. A failed begin may
have accepted earlier loads; failure/release cancels them, but their results
must still be forwarded until `pxa_asset_scene_pending` is zero. Cancellation
can lose to an already queued success, which the helper closes. Retry uses a
fresh token range after draining and successfully releasing handles; no hidden
retry or polling loop exists. Binding failure keeps READY resources available
for retry or explicit release. `release` does not unbind the renderer: replace
or unbind old slots explicitly when switching scenes, and submit a loading
frame first when old and new scenes cannot fit together.

Keep item/path storage valid and unchanged until pending results drain. Keep
binding storage alive while attached to the scene, and do not modify its
handles yourself. To detach it, release successfully, drain, then zero the
scene. The description limit of 49 does not override the Host handle quota:
all successfully loaded items are held until binding. A small request window
limits concurrent requests, not total handles. Large scenes can use progressive
binding through the lower-level SDK, as voxel-craft does.

Only use release/cancel imports during start/event callbacks. At component
stop, Core revokes requests, handles and contexts before `pxa_app_stop`; imports
are forbidden there. See [resource protocol](../../spec/draft/assets.md) for
wire formats, budgets and lifetime rules. The workspace's `resource-scenes`
application demonstrates loading, optional prefetch, 100 scene switches and
exit while loading through an actual signed AOT.

## Application lifecycle

Apps in either Core version export:

```c
int32_t pxa_app_start(const uint8_t* config, uint32_t length);
int32_t pxa_app_on_event(const uint8_t* event, uint32_t length);
void pxa_app_stop(uint32_t reason);
```

Callbacks are serialized. Imports are legal during start and event callbacks,
but forbidden during stop; the Host revokes Component resources before stop.
The signed manifest's `minSdk` and Service requirements, rather than a Guest
version export, define runtime compatibility. `compileSdk` is recorded in the
build provenance sidecar.

`pxa.h` is the Core entry point. `pxa_window.h` and
`pxa_clock.h` contain the Window and Clock helpers. `pxa_ui.h` builds
atomic UI tree transactions.
`pxa_ui_builder.h` is a streaming tree builder that retains only the current
ancestor path, and `pxa_ui_components.h` supplies semantic compositions such
as buttons and VirtualList. The builder can assign node IDs automatically:

```c
#include "pxa_ui_components.h"

static pxa_ui_builder_t ui; /* zero-initialize once; retain across patches */
static uint32_t generation;
static uint8_t scratch[128];
static uint32_t ancestors[8];
static uint32_t ok_button;

static int build_ui(void) {
    if (!pxa_ui_builder_begin(&ui, &generation, PXA_UI_PRIMARY_SURFACE, 0,
                              PXA_UI_TRANSACTION_REPLACE_SURFACE,
                              scratch, sizeof(scratch), ancestors, 8))
        return 0;
    if (!pxa_ui_builder_auto_enter(&ui, PXA_UI_NODE_ROOT) ||
        !pxa_ui_text(&ui, "Welcome", 2, PXA_UI_THEME_TEXT) ||
        !(ok_button = pxa_ui_button(&ui, "OK", PXA_UI_THEME_PRIMARY,
                                    PXA_UI_THEME_ON_PRIMARY)) ||
        !pxa_ui_builder_leave(&ui)) {
        (void)pxa_ui_builder_abort(&ui);
        return 0;
    }
    return pxa_ui_builder_end(&ui);
}
```

The returned IDs identify interactive nodes in input events. Check the event's
surface and generation with `pxa_ui_event_is_current()` before comparing its
node with `ok_button`. `REPLACE_SURFACE` restarts automatic IDs at 1, while
`PATCH` continues from the last committed ID; keep the same builder for both.
Use a separate builder for each surface.
For a patch that adds a child to an existing node, use
`pxa_ui_builder_enter_existing()` and `pxa_ui_builder_leave()`. Failed or
cancelled transactions restore the ID cursor. Explicit IDs remain available
through `pxa_ui_builder_node()` and `pxa_ui_component_*()`; when mixed with
automatic IDs, explicitly created nodes advance the cursor.
See `examples/ui-button` for a complete app with click-event handling.

The opinionated page template used by the lab applications is
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
`pxa_net.h` builds bounded HTTP(S) GET, HEAD, POST, PUT, PATCH and DELETE
requests. It supports bounded request headers,
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
Localized Apps call `pxa_i18n_init_from_start_config()` before their first
render. It selects the Host locale from startup record 12 and falls back to the
bundle's source locale when an older Host does not provide that record. Runtime
locale changes continue through the System configuration event and
`pxa_i18n_handle_event()`.
Baseline tree controls require no handshake. Apps test optional features before
using Canvas, VirtualList, Grid, media, additional Surfaces or other extensions,
and rebuild responsive layout after `ENVIRONMENT_CHANGED`. Resource pressure is
reported semantically as normal, constrained or critical; raw Host memory is
not application ABI.

The product integration Apps under `apps/pxa` demonstrate Canvas, private FS
and Permission v1 with the same lifecycle. A standalone consumer can keep Apps
elsewhere and set `PXA_APP_SOURCE_ROOT`. Their `package.json` files are source metadata, not
the installed ABI manifest. `permissions` declares signed policy declarations;
`services` is an optional unique list of additional required service names or
requirement objects. Valid names are `window`, `ui`, `clock`, `fs`, `storage`,
`ipc`, `sensor`, `net`, `audio`, `permission`, `work`, `device`, `surface`,
`game-render`, `assets`, or `log`.
`core` is intentionally not a Service declaration: Core compatibility comes
from the SDK fields. A string or a requirement object without a version
requires the build SDK's current Service minor and permits all later minors
in that major. A requirement object can set
`min_version`, `max_version` and named `features`, for example:

```json
"services": [{"name": "ui", "min_version": [0, 3],
              "features": ["canvas", "virtual-list"]}]
```

UI Components automatically require the build SDK's current Window, UI and
Clock versions. An explicit UI
requirement replaces that automatic default when an App needs a newer minor or
feature. Legacy source metadata describes one `main` UI Component from `main.c`. A

Compatibility declarations in `package.json` are `min_sdk`, `target_sdk` and
`compile_sdk`, each encoded as `[major, minor]`. The first two are signed into
Manifest 0.5; `compile_sdk` is emitted only in the provenance/SBOM sidecar.
Use `min_sdk` for required Core APIs, `target_sdk` for behavior-policy
selection, and Service ranges/features for individual capabilities. Do not add
Core to `services`: doing so creates an unnecessary exact-version gate that
prevents a newer compatible Core minor from running the App.

`components` array may instead describe multiple Components with stable `id`,
`kind`, `source`, and optional Component-local `services` fields. UI Components
receive Window, UI and Clock; service and job Components do not. IPC callers
must declare `ipc`; an `ipc_endpoints` provider receives it automatically.
Package Component records and endpoint routes are sorted by ID/name, so the
generated signed manifest stays canonical.
`build_package_manifest.py` produces the canonical signed `manifest.pxm` and
complete SHA-256 file inventory.

`package.json` directly defines the default App `name`, `description`, and
optional `icon`, so i18n is not required for small or rapidly developed Apps.
When localization is needed, `messages.yaml` defines semantic in-App messages
and locale files such as `zh-CN.yaml` may contain both `translations` and an
optional `metadata` mapping. The packager generates `pxa_app_messages.h` and
signed locale metadata from these files; standard App sources do not use a
`package.json.localizations` object.


### Prepared UI images (Assets 2.1, UI 0.6)

Keep PNG source artwork outside the packaged `assets/` directory and declare its
compiled representation in `resources.json`:

```json
{"assets":[{"kind":"image","source":"resources/icon.png","path":"assets/icon.pxr","encoding":"bgra8888"}]}
```

BGRA8888 preserves straight alpha and color precision. Explicit `rgb565` uses
half the pixel bytes and requires an opaque source. Conversion occurs during
packaging; the resulting PXR file contains directly usable pixels. These files
can be larger than PNG, but loading does not retain compressed and decoded
copies or need a runtime PNG workspace.

Submit `pxa_assets_load_image(token, "assets/icon.pxr")`, then parse the usual
Assets LOAD completion. On success, add
`pxa_ui_set_image(&transaction, image_node, result.handle)` to a UI transaction.
After a successful commit, close the Guest handle with `pxa_close_handle`;
the node retains its own reference until replacement, clear, removal or exit.
Clear with `PXA_UI_PROPERTY_IMAGE_HANDLE`. An invalid/stale/foreign handle fails
transaction preparation before changing the displayed image. Loading remains
cancellable through `pxa_cancel(token)` and obeys the shared pixel budget.

For several page images, include `pxa_image_set.h`. It keeps only the handles
the app currently selects and submits at most one LOAD at a time. The app still
owns its ordinary event callback, page transitions and retry policy:

```c
static const char *const paths[] = {"assets/sun.pxr", "assets/rain.pxr"};
static uint64_t handles[2];
static pxa_image_set_t images =
    PXA_IMAGE_SET_INIT(paths, handles, UINT64_C(0x57494d4700000000));

/* After the new UI transaction commits, release images the page no longer uses. */
pxa_image_set_select(&images, UINT64_C(1) << 0);
pxa_image_set_pump(&images);

/* In the existing event callback, after pxa_parse_event succeeds: */
if (pxa_image_set_on_event(&images, &event)) {
    /* Rebuild/commit UI with pxa_image_set_handle(&images, 0), if nonzero. */
    pxa_image_set_pump(&images);
}
/* Background: pause(1); foreground: pause(0), retry(), pump().
 * Stop: stop(), then allow the normal event drain to close late successes. */
```

The borrowed path table and zeroed handle array must outlive the set and its
terminal event. A changed selection closes unused handles; the UI must have
committed its replacement first so it no longer pins those pixels. Failure
stops automatic submission until the app explicitly calls `retry`; pending
loads are cancelled on background or stop. The helper does not allocate a
Guest pixel buffer, start a second event loop or create a hidden cache.

For Canvas, include `pxa_canvas.h` and require UI 0.6. After the same IMAGE
LOAD completes, append a prepared image to the frame:

```c
if (!pxa_canvas_image_handle(&frame, 12, 20, 64, 64, 255,
                             PXA_UI_IMAGE_FIT_CONTAIN, result.handle)) {
    /* Handle an invalid argument or insufficient display-list capacity. */
}
```

Keep the handle open until `pxa_canvas_present` (or `present_regions`) succeeds.
The committed frame keeps its pixels until replacement, node removal or exit.
Closing the Guest handle does not remove that frame, but submitting another
frame with the closed handle fails. Keep the handle open across animated frames
that reuse the image. Each submission checks the actual component and full u64
handle; repeated references within a frame share one descriptor. Drawing uses
ordered prepared views, without file reads, decode or handle lookup. Failed
preparation or alpha-plane allocation keeps the previous Canvas frame.

The existing path-based image and Canvas APIs still require application
migration; the handle helpers do not redirect those calls automatically.

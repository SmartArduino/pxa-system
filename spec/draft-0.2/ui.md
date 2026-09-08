# PXA UI Service 0.3.0

UI 0.3 is a backend-neutral command protocol. The Host owns the rendered tree;
the service does not retain a second property-complete scene. A Component has a
primary Surface with ID 1 and may request more Surfaces when the corresponding
feature is available.

## Transaction model

`TX_BEGIN` identifies the Surface, generation, replacement target and semantic
transaction kind. `TX_WRITE` fragments concatenate into one command stream;
fragments may split a command header or payload. `TX_COMMIT` first validates the
complete stream and reserves the resources required by the changes. The Host
then prepares the backend transaction and publishes it atomically. Any failure
leaves the previous committed tree and generation active.

`PATCH` changes an existing tree. `REPLACE_SUBTREE` replaces the target node and
its descendants. `REPLACE_SURFACE` replaces the Surface root and requires a
single Root command. Node IDs are scoped to one Surface and remain stable until
their nodes are removed. Removing a node removes its complete subtree.

No protocol field limits node count, tree depth, canvas count, text bytes or
property bytes. Hosts use iterative validation and enforce private byte budgets
to protect the system. Resource exhaustion is reported, never handled by
silently truncating a tree.

## Rendering and adaptation

Primitive nodes describe layout, content and interaction without naming LVGL
or another renderer. Product controls and visual components are Guest SDK
compositions over these primitives. Logical pixels, design tokens, safe-area
insets, font scale and direction make the same command stream adaptable across
device classes.

The initial primary-Surface environment is Core startup-configuration record 8
(`ui-environment`) containing the same environment record list used by events.
Changes are delivered through `ENVIRONMENT_CHANGED`. Baseline applications do not
perform an explicit capability handshake. They declare required service
features in the signed Package and use SDK fallbacks for optional features.

`composition=alpha-overlay` marks a complete UI subtree for the transparent
UI plane. The Host snapshots that subtree before it is flattened into the
primary RGB565 framebuffer, retains its color and alpha, and composites it
after app Surfaces. The node still participates in normal LVGL hit
testing. This is the required mode for controls with non-opaque pixels over a
Surface; opaque UI regions remain an opaque-only compatibility path.

## Resource lifetime

Widget properties live in the backend object tree. The service retains only
structural Node Registry metadata needed for ownership, validation and event
routing. Transaction bytes and structural overlays are released on commit or
cancel. Canvas and decoded asset storage are allocated lazily. Image nodes and
committed Canvas frames hold references to their decoded assets. The reference
Host evicts zero-reference images by least-recently-used order when a new image
would exceed its private cache budget; active references remain stable. A Host
may additionally retire inactive optional Surfaces according to its own policy.
Resource-pressure events let the Guest reduce detail before the Host rejects a
new allocation or stops a Component.

Host diagnostics expose committed UI byte usage plus the latest and maximum
transaction commit duration. Timing is operational telemetry, not a Guest ABI
quota or capability value.

Canvas display lists have independent frame generations. A successful Present
transfers the new list to the backend and retires the old list only after the
backend no longer renders it. A Component that never creates a Canvas consumes
no Canvas frame storage.

Hosts advertising `canvas-stream-io` let a Guest open a persistent stream for
a Canvas node. Between `CANVAS_BEGIN` and `CANVAS_PRESENT`, one or more
`pxa_io(handle, PXA_IO_WRITE, ...)` calls append display-list bytes under the
same Canvas quota as `CANVAS_WRITE`. This keeps large byte streams out of the
bounded control envelope; a Guest falls back to `CANVAS_WRITE` when the feature
or stream is unavailable. The handle is scoped to that Canvas instance, becomes
stale when the node is removed and must be closed by the Guest.

Each display-list record has a four-byte `primitive:u8 | flags:u8 |
payload-len:u16` header. Records support rectangles, ellipses, lines, arcs,
text, package images and balanced rectangular clip scopes. Coordinates are
signed logical pixels relative to the Canvas content origin and colors are
RGBA8888. `CANVAS_WRITE` may split any header or payload. Clip depth and
primitive count have no ABI limits; their storage is charged to the Host's
private byte budget. The normative payload layouts are listed in
`pxa-ui.yaml`.

VirtualList owns the full logical scroll extent but not one node per item. On
scroll it emits `VISIBLE_RANGE(first, count)`, including a Host-selected
overscan window. The Guest materializes only that range using stable item node
IDs and `PATCH` or `REPLACE_SUBTREE`; the overscan size is policy rather than an
ABI constant.

## Feature profiles

The base protocol requires the primary Surface, Flex/Stack layout, text,
package images, controls, progress, scrolling and partial tree transactions.
Canvas and VirtualList are independently advertised extensions. Grid,
MediaSurface, multiple Surfaces, Host animation, accessibility and a shared
command buffer are optional and must not be used unless their feature bit is
present. The current ESP and simulator Hosts advertise Canvas and VirtualList;
their absence on a smaller Host does not change the baseline ABI.

## Events and stale input

Events include Surface, node and committed generation. Guests discard events
whose generation no longer matches their view. The common event envelope
supports actions, value changes, scrolling, focus, keyboard, text, pointer and
accessibility operations. Resource pressure is semantic (`normal`,
`constrained`, `critical`); raw free-memory values are not application ABI.

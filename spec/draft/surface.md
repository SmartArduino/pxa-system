# PXA Surface Service 0.1.0

Surface is the bulk-pixel path for emulators, video and other producers that
already own complete frames. It is independent from the retained UI service.
The Host compositor combines Surface layers with UI content at presentation
time; a Guest does not create an RGB565 Canvas node or send frame bytes through
the bounded control envelope.

## Service and resource

The service ID is 16 and the initial version is 0.1.0. `PXA_SURFACE_CREATE`
returns a `PXA_RESOURCE_SURFACE` handle. The handle is both the Surface lifetime
token and its byte-stream endpoint. Closing it removes the layer and eventually
releases all buffers after an active presenter lease completes.

Version 0.1.0 supports tightly packed or Host-aligned RGB565 rows and
`ARGB8888_PREMULTIPLIED` for a Surface whose pixels need alpha. The result
contains the negotiated stride and exact frame byte count. A write must
contain exactly that many bytes:

```text
pxa_io(surface, PXA_IO_WRITE, pixels, frame_bytes)
```

The Host copies the write into a free producer buffer and stages it atomically.
It returns `WOULD_BLOCK` when no producer buffer is free. Guests retry on a
later clock/input event and must not spin inside one callback.

## Control messages

All integers are little-endian.

| Opcode | Request ID | Request payload | Successful result after status |
| --- | --- | --- | --- |
| `PXA_SURFACE_CREATE` (1) | nonzero | `width:u16, height:u16, format:u16, buffer_count:u8, flags:u8` | `handle:u32, stride:u32, frame_bytes:u32, buffer_count:u8, reserved[3]` |
| `PXA_SURFACE_CONFIGURE_LAYER` (2) | nonzero | `handle:u32, x:i32, y:i32, width:u16, height:u16, z:i16, visible:u8, reserved:u8` | empty |
| `PXA_SURFACE_QUEUE_FRAME` (3) | zero | header plus damage rectangles below | no completion event |
| `PXA_SURFACE_QUERY_STATE` (4) | nonzero | `handle:u32` | `submitted:u64, presented:u64, dropped:u64, free_buffers:u32, flags:u32` |
| `PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS` (5) | nonzero | `handle:u32, count:u8, reserved[3], regions[count]` | empty |

`format=1` is RGB565 and accepts `flags=0` or
`PXA_SURFACE_FLAG_PREFER_DIRECT_SCANOUT`. The latter is an opt-in request,
not a guarantee: a Host can bypass its UI compositor only for an exact
full-screen RGB565 layer with no retained or trusted UI above it. `format=2`
is native-endian `0xAARRGGBB` with premultiplied RGB channels and requires
`flags & 1 = PREMULTIPLIED_ALPHA`. Hosts blend it with the fixed `src-over`
equation. Layer width and height must equal Surface width and height, so
composition is 1:1. Coordinates are in the primary display's logical
orientation and are clipped by the Host.

`PXA_SURFACE_QUEUE_FRAME` is the per-frame fast control path:

```text
handle:u32
frame_id:u64
damage_count:u8
reserved[3]
damage[damage_count] = x:u16, y:u16, width:u16, height:u16
```

`frame_id` is nonzero and monotonically increasing by Guest convention. Damage
is advisory compositor metadata and does not make a byte-stream write partial.
At most eight rectangles are accepted. A zero count means the complete Surface
may have changed. The zero-request-ID form avoids allocating a Runtime request
or completion event for every frame.

`PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS` is an optional ESP profile. Each
region is `x:u16, y:u16, width:u16, height:u16` relative to the Surface. Within
a region the presenter keeps the LVGL framebuffer instead of the opaque
Surface. It is for controls with opaque rectangular backgrounds and is
configured outside the frame loop. A Host without this profile returns
`UNSUPPORTED`; at most eight regions are accepted.

`PXA_SURFACE_QUERY_STATE.flags` uses
`PXA_SURFACE_STATE_FLAG_SUPPORTS_OPAQUE_UI_REGIONS` and
`PXA_SURFACE_STATE_FLAG_SUPPORTS_ALPHA_COMPOSITING` for Host capabilities.
`PXA_SURFACE_STATE_FLAG_UI_ALPHA_PLANE_ACTIVE` indicates that the Host currently
has an alpha-bearing UI plane installed over the Surface.

## BufferQueue semantics

A Surface has `buffer_count` Host-owned buffers. An exact stream write changes a
free buffer to staged. `QUEUE_FRAME` changes the staged buffer to pending. The
presenter acquires the newest pending buffer under a lease, composes it, then
releases the lease. The acquired frame becomes current and remains immutable so
unrelated UI refreshes can reproduce the same display until a newer frame is
presented.

When a new pending frame replaces an older pending frame, the older buffer is
released without presentation and `dropped` increases. This latest-wins rule
bounds latency when a producer runs faster than the display. Two buffers suit a
producer paced by presentation. Three buffers are recommended for an emulator:
they permit pending replacement while one current buffer is retained.

`submitted` counts accepted queue operations. `presented` counts frames that
completed compositor access. `free_buffers` excludes current, pending, staged,
writing and presenter-acquired buffers.

## UI composition

The ESP reference Host keeps LVGL as the owner of UI rendering and the panel
flush callback as the sole LCD submission point. A queued Surface frame marks
a minimal LVGL region dirty. On the final LVGL flush, the board presenter leases
the latest Surface frame and selects RGB565 pixels from either the opaque
Surface rectangle or the LVGL framebuffer while performing the existing tiled
rotation. Rotation, composition and byte swapping therefore remain one memory
pass and one LCD frame submission.

The ESP profile exposes one visible Surface. LVGL remains visible outside its
rectangle. The optional opaque-UI-regions capability keeps LVGL pixels in
declared opaque control regions within that rectangle. Host system layers such
as the lock screen suspend app Surface presentation and resume with the newest
pending frame when dismissed. `z` records the requested layer order but does
not make arbitrary LVGL pixels inside the Surface rectangle visible.

### Direct scanout profile

An App may create a full-screen RGB565 Surface with
`PXA_SURFACE_FLAG_PREFER_DIRECT_SCANOUT`. On supported boards, the presenter
uses that Surface as the rotation source and bypasses LVGL frame production
until composition becomes necessary. This avoids a full LVGL refresh for an
App that owns every pixel, but a rotated panel can still require a copy into a
board-owned DMA buffer.

The Host remains authoritative. Any trusted UI transaction, system foreground
change, alpha overlay, or ineligible Surface configuration returns presentation
to the normal LVGL composition path after outstanding output transfers drain.
Existing Surfaces without this flag always use composition.

### Transparent overlay profile

Transparent text, rounded controls and translucent panels over video or
emulator pixels require an explicit alpha-bearing UI plane. They cannot be
recovered from the final opaque LVGL framebuffer after the Surface has replaced
those pixels. The ESP transparent overlay profile therefore uses this fixed
order:

1. app Surface;
2. app UI overlay;
3. trusted Host UI, including the lock screen and system prompts.

The app UI overlay is cached as RGB565 color plus A8 coverage and is redrawn
only for changed UI regions. The panel presenter blends the Surface
over its LVGL base, then blends a nonzero UI alpha pixel while performing the
existing tiled rotation and byte swap. Fully transparent runs skip overlay
reads and fully opaque RGB565 Surface pixels use the copy fast path. This
preserves one panel submission and avoids copying the complete Surface through
LVGL on every frame.

Overlay buffers and damage belong to the Host; Guests continue to describe
controls through the retained UI service and never upload overlay pixels through
control messages. The ESP provider API passes this plane into the panel worker;
the LVGL capture producer remains Host-private. Capability negotiation
distinguishes opaque regions, alpha Surface composition and a currently active
UI alpha plane.

Multiple Surface layers, scaling, additional formats, release events and
mapped shared buffers can be added without changing the v1.0 stream-and-queue
contract.

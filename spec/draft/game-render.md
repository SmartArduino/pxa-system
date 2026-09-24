# PXA GameRender Service 0.3.0

GameRender is the low-overhead 2D and software-3D path. It owns persistent
palettes and textures, validates compact draw lists, and rasterizes into
Host-owned RGB565 buffers. Surface remains the API for applications that
already own complete pixel frames.

The service ID is 18. `CREATE_CONTEXT` returns a
`PXA_RESOURCE_GAME_RENDER_CONTEXT` handle plus the supported command mask and
resource limits. Closing the handle releases all renderer resources and removes
its visible layer.

## Context creation

`CREATE_CONTEXT` keeps the explicit eight-byte request: `width:u16, height:u16, buffer_count:u8, flags:u8,
reserved:u16`. The successful result after status is `handle:u32,
capabilities:u32, max_draw_bytes:u32, max_texture_dimension:u16,
max_textures:u8, reserved:u8`.

`CREATE_AUTO_CONTEXT` uses `width` and `height` set to zero and replaces the
first reserved byte with `requested_scale:u8`; zero asks the target board for
its default. A nonzero request must be one of the board-declared scales.
The board owns the supported integer scale set and its default; the generic
service only validates the request and resolves the target. Its successful
result extends the legacy fields with
`display_width:u16, display_height:u16, render_width:u16, render_height:u16,
render_scale:u8, supported_scale_mask:u8, reserved:[2]`. Guests use display
coordinates for input and UI, and convert them to render-buffer coordinates
through the SDK helper.

`PREFER_DIRECT_SCANOUT` is advisory. A Host may still compose when trusted UI
or system overlays are visible. The ESP profile supports one Surface or
GameRender context at a time because both ultimately target the same panel.
The service imposes no configured width or height ceiling; non-zero dimensions
are passed to the backend, which may return a resource error if allocation is
not possible.

## Resource and frame IO

The context handle accepts three operations:

```text
PXA_GAME_RENDER_IO_UPLOAD     0x100
PXA_GAME_RENDER_IO_SUBMIT     0x101
PXA_GAME_RENDER_IO_TELEMETRY  0x102
```

Uploads install a 256-entry RGB565 palette, a lit RGB565 palette with up to 256
rows of 256 entries, or an INDEX8 texture in a persistent slot. Resources
remain resident across frames. The ESP profile
freezes resources after the first accepted frame to keep presenter reads free
of lifetime races.

Submit validates and copies a complete, bounded DrawList into a latest-wins
mailbox. It never waits for rasterization, display rotation, TE, or SPI. The
presenter rasterizes only the newest pending list and counts replaced lists as
dropped frames.

Raster ABI 1.7 supports clear, flat quad, textured depth quad, sprite, sprite
batch, and triangle batch records. Sprite batches share texture, blend flags,
and optional solid color across compact 16-byte instances. Triangle batches
share texture or solid color across screen-space 12-byte vertices; every three
vertices form one depth-tested triangle. Coordinates and UV values use signed
12.4 fixed point and depth uses reciprocal-compatible Q8 values.

Hosts advertising `PAINTER_POLYGON` also accept the painter flag on textured
quad and triangle-batch records. Painter polygons are convex affine scanlines,
execute in list order without reading or writing depth, and use each vertex's
light value as a row in the lit palette. Solid painter polygons carry an
8-bit palette index instead of RGB565. `TRANSPARENT_INDEX0` skips texel zero.

Hosts advertising `LIT_PALETTE_DEPTH` also accept the `LIT_PALETTE` flag on
depth-tested textured quad and triangle-batch records. The light value selects
a row in the uploaded lit palette, avoiding per-pixel RGB565 multiplication
without changing depth testing or affine/perspective UV selection.

Hosts advertising `DEPTH_CUTOUT` accept `TRANSPARENT_INDEX0` on depth-tested
textured quad and triangle-batch records. Texel index zero skips both color and
depth writes, providing low-cost cutout or ordered-dither transparency without
an alpha buffer, blending pass, or additional full-frame storage.

Hosts advertising `FIXED_ALPHA_BLEND` accept `BLEND_75` on textured painter or
depth-tested polygons. Each covered pixel combines three parts source and one
part destination RGB565 using shifts and masks. The depth-tested form reads depth but
does not write it, allowing back-to-front translucent surfaces without an
alpha buffer or another full-frame allocation.

Hosts advertising `COVERAGE_MASK` reuse the depth scratch as two one-bit planes
for near-to-far opaque coverage and deferred translucent coverage. Textured
painter quads may carry perspective UVs when `PAINTER_PERSPECTIVE` is also
advertised; `AFFINE_UV` remains an explicit cheaper option. Starting with ABI
1.7, `SOLID_COLOR | PAINTER | AFFINE_UV | COVERAGE_MASK` carries direct RGB565
(rather than a palette index) so opaque billboards participate in the same
coverage test as textured terrain.

The 88-byte telemetry record reports submitted and dropped frames, draw bytes,
covered pixels, host raster time, queue/presentation time, command counts,
rejected lists, and last-frame values.

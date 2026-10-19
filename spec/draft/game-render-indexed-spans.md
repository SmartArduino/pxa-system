# GameRender indexed spans 1.1

The normative layout source is `pxa-game-render.json`. Run
`tools/generate_game_render_spans.py --check` to verify the generated C Host,
C Guest and C++ Guest codecs. All wire integers and packed light pairs are
little-endian. Native pointers, C struct padding and callbacks never cross ABI.

## Context and lifetime

Core 2 callers declare GameRender 1.0 and feature `borrowed-indexed-spans`
(bit 1). `CREATE_RASTER_CONTEXT` (3) takes `max-spans:u32, reserved:u32=0`.
The current Host accepts 1 through 4096 spans. Success after Core status is
`handle:u64, max-spans:u32, reserved:u32=0`. Creation consumes no display
backend resources and allocates no pixel or command storage.

The context accepts `EXECUTE_SPANS` (0x104), a synchronous I/O operation. The
engine adapter installs the current Component's linear-memory view for the
serialized import and removes it on every return, including errors. Pinned
memory is unnecessary. A later `memory.grow` may relocate the linear memory;
the next import resolves every offset against its new base and committed size.
The Guest cannot resume while either native execution partition is running.

Every descriptor, source range and output range is validated before the first
pixel is written. Invalid batches write no pixels. No written pixel may overlap
the command bytes, texture, light grid or colormap. Unwritten portions of a
coarse target view may contain read-only resources, allowing packed Guest arenas
without copying. The sources may share read-only storage. A parallel backend must join all work before returning
and must not report a retryable failure after writing pixels. `WOULD_BLOCK`
therefore means the caller may replay the whole batch. Closing the context
releases its handle; it never frees borrowed Guest allocations.

## Packet

A 72-byte header is followed by exactly `count` 24-byte span records. There is
no frame submission, framebuffer rotation or display wait in this operation.
Groups should cover a surface or multiple compatible scanlines; avoid one
import per short perspective segment.

| Header offset | Field |
| --- | --- |
| 0 | magic:u32 = 0x50535850; major:u16=1; minor:u16=0 |
| 8 | count:u32; reserved:u32=0 |
| 16 | target-offset:u32; target-bytes:u32 |
| 24 | texture-offset:u32; texture-bytes:u32 |
| 32 | texture-width:u16; texture-height:u16 |
| 36 | light-offset:u32; light-bytes:u32 |
| 44 | light-width:u16; light-height:u16 |
| 48 | light-origin-s:i16; light-origin-t:i16 |
| 52 | colormap-offset:u32; colormap-bytes:u32 |
| 60 | colormap-levels:u16; light-shift:u8; flags:u8 |
| 64 | wrap-s:u32; wrap-t:u32 |

Offsets identify the calling Guest's linear memory. Texture pixels are INDEX8,
tightly packed. The colormap has 1 through 256 rows, each 256 INDEX8 entries.
Light storage is a naturally aligned, tightly packed grid of u32 words, each
containing a top value in the low 16 bits and a bottom value in the high 16.
Grid dimensions are at least 2 by 2. Validation limits a batch to 262144 output
pixels (also at most four times target bytes) and 262144 light-grid bytes. These
are execution-work bounds, not allocations or framebuffer size requirements.

Each span is `destination:u32, count:u32, s:i32, t:i32, ds:i32, dt:i32`.
Destination is a byte offset relative to the target. Count is nonzero. Spans
execute in packet order, including overlapping destinations. Parallel execution
partitions the output address space, preserving that order for each pixel.

## Arithmetic

`s,t,ds,dt` are 16.16 values. Each is arithmetically shifted right by 10 before
stepping; the initial coordinates are narrowed to signed 16-bit 10.6. This
preserves the reference engine's fractional-step rounding. The current fast
path rejects a span that crosses signed 10.6 wrap; adapters may use their
reference renderer for such a batch.

For integer coordinates `si=floor(s6/64), ti=floor(t6/64)`, the light cell is
`floor(si/2^light_shift)-origin_s`,
`floor(ti/2^light_shift)-origin_t`, with `light_shift` in 1 through 4.
The two adjacent packed words provide the four grid samples. First interpolate
vertically using `ti & (2^shift-1)`, then horizontally using the corresponding
`si` fraction. Preserve integer multiplication and truncation at each specified
step. The colormap row byte offset is `(interpolated >> shift) & 0xff00`.
Preflight bounds all sampled packed words against the number of colormap levels.
The final grid row is represented by the preceding row's packed bottom values;
its own words are not sampled or required to contain initialized light values.

Flag bit 0 (`POW2`) requires power-of-two texture dimensions and samples with
integer-coordinate masks. Otherwise, for coordinate `c6`, wrap factor `q`, and
dimension `d`, texture coordinate is the high u32 of
`u64(u32(q * u32(c6 << 10))) * d`, with the shift and inner multiplication
performed modulo 2^32. This explicitly defines negative-coordinate behavior.
Flag bit 1 (`FULLBRIGHT`) selects colormap row zero. Other flag bits are invalid.

## SDK and statistics

C includes `pxa_game_render_spans.h`: create, parse, batch begin, append and
execute use caller-owned storage. C++ uses `pxa::game::Service::create_raster`,
`RasterContext` and `SpanBatch`; resource handles close through RAII. Neither
builder allocates a per-frame heap buffer.

`SPAN_TELEMETRY` (0x105) takes a 64-byte output buffer. It returns six u64 values:
successful batches, rejected batches, successful spans, written pixels,
validation microseconds, execution microseconds; followed by four u32 values:
last validation microseconds, last execution microseconds, max spans, zero.
Times are wall durations and include preemption; execution includes worker join.
A Host without a clock reports zero timing. They are compute statistics, not
presented frames or game FPS. Query outside the timed frame window.

## Projected rows (GameRender 1.1)

Declare minimum GameRender 1.1 and feature `projected-indexed-spans` (bit 2).
The same compute context accepts `EXECUTE_PROJECTED_SPANS` (0x106). Existing
0x104 packets remain unchanged. The new packet is exactly a 72-byte material
header, an 80-byte projection descriptor, and `count` eight-byte rows. The
header magic is **0x50505850**; its packet major/minor remain 1/0. This packet
version is independent of the service's additive minor version.

Projection fields, in order:

| Relative offset | Fields |
| --- | --- |
| 0 | s-origin, s-du, s-dv: three u32 binary32 bit patterns |
| 12 | t-origin, t-du, t-dv: three u32 binary32 bit patterns |
| 24 | z-origin, z-du, z-dv: three u32 binary32 bit patterns |
| 36 | s-adjust, t-adjust, s-max, t-max, s-offset, t-offset: six i32 |
| 60 | pitch:u32, depth-offset:u32, depth-bytes:u32, depth-pitch:u32 |
| 76 | flags:u16, reserved:u16=0 |

A row is `x:u16, y:u16, count:u16, reserved:u16=0`. Count is nonzero,
`x+count <= pitch`, and the full colour interval fits the target view.
Pitch is in bytes, in 1..65535. The two extent maxima must be at least 32.
All coefficients must be finite. There are no native float structs on the wire.

All operations below use strict IEEE binary32, round to nearest, with no
contraction or reassociation. Start each row with left-associated
`sdivz = (s_origin + float(y)*s_dv) + float(x)*s_du` and corresponding `tdivz`
and `zi`. Each sampled projection point requires positive `zi` and computes
`scale = 65536.0f * (1.0f / zi)`, preserving the two operations. Convert
`sdivz*scale` to i32 by truncation toward zero, add `s_adjust` in widened integer
arithmetic, and clamp to `[0,s_max]`; similarly for t. Out-of-range float-to-i32
conversions reject the complete batch before writing.

Subdivide into at most 32 pixels. When pixels remain, advance sdivz/tdivz/zi by
32 times their du coefficients, project the next point, clamp first to the
maximum then to minimum 32, and divide the coordinate difference by 32 with
signed truncation toward zero. In the final segment, advance by `count-1`,
project and clamp the same way; for count > 1, multiply the coordinate
difference by `floor(65536/(count-1))` in i64 and arithmetic-shift right by 16.
Add the signed texture offsets times 65536 modulo 2^32 to the segment's initial
coordinates. Add 1024 to a negative step before handing the segment to the
10.6 sampler described above. Continue the next segment from the projected
endpoint before the texture offset was applied. A one-pixel segment's step
has no effect on its output.

Projection flag bit 0 writes inverse depth alongside colour. All other bits
are invalid. With depth enabled, the depth view must be valid and two-byte
aligned, depth-pitch must be even and at least `2*pitch`, and each depth row
interval must fit. With depth disabled, offset, bytes and pitch must all be
zero. Start inverse depth from `int32(zi*32768.0f*65536.0f)` at the row's initial
point, advance modulo 2^32 by
`int32(z_du*32768.0f*65536.0f)` for each pixel, and write the high 16 bits in
little-endian order. Both float-to-i32 conversions are preflight checked.

Every row and generated segment is preflight checked using the same arithmetic
as execution. No span scratch array or duplicated resource is allocated.
Execution recomputes the projection from immutable borrowed inputs. Both colour
and depth writes must be disjoint from all source resources and the packet.
Their actual written bounding intervals must also be disjoint from one another;
unused tails of the declared views may overlap. The pitch rule ensures distinct
colour pixels map to distinct depth pixels. Native workers split by colour byte
address and write the corresponding depth pixels; every worker joins before the
import returns. Ordered overlapping rows preserve both colour and depth output.

C exposes `pxa_game_render_projected_batch_begin`, `projected_append` and
`execute_projected_spans` with a distinct caller-owned builder. C++ provides
`ProjectedBatch` and a `RasterContext::execute` overload. Short successful I/O
responses are protocol errors. Telemetry uses the same 64-byte layout; its
`spans` field counts submitted rows for 0x106, and `pixels` counts colour
pixels, excluding the additional depth stores. These remain compute counters,
not game FPS.

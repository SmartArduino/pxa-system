# GameRender indexed spans 1.0

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

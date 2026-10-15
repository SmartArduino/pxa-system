# Host image codecs

`pxa_image_decoder_t` accepts bounded input and publishes immutable image assets.
The core owns scaling, pixel formats, allocation budgets and ownership. Platform
adapters own entropy decoding. No codec, timer or pixel buffer is allocated at
service initialization.

## Optional image sequences

`pxa_image_decoder_next(decoder, &image, &frame)` finalizes input on its first
call and then pulls one frame at a time. `frame` reports the delay in milliseconds,
zero-based iteration and whether this is the final frame. Success with a null
image and `last = 1` means EOF; repeated EOF calls are safe. Static codecs return
one final frame. The existing `pxa_image_decoder_finish` still returns the first
image, so existing PNG/JPEG callers need no change.

A codec supplies an optional `next` callback. Its `finish` callback validates
input and prepares the target; `next` composes each requested frame and returns
`NOT_FOUND` at EOF. Published frames can outlive the decoder and must not be
modified. `begin_frame` creates a writable scaled copy, `retain_frame` retains a
completed immutable frame, and `copy_frame` provides a bounded checkpoint when
disposal requires a distinct background. All pixels use the existing image
allocator; workspaces and compressed bytes use the temporary allocator.

This is a **native Host extension**, not a new Guest ABI or an SDK animation API.
No animation resource is currently published to Guests. Scheduling, presentation
backpressure and foreground policy belong to the consumer; a codec never queues
frames or advances a hidden timer.

## LVGL GIF adapter

`pxa_lvgl_gif_codec` reuses LVGL 9.6's RAW scanline LZW decoder without its widget
or original-sized framebuffer. It handles transparency, local palettes,
interlacing, disposal 0–3 and NETSCAPE/ANIMEXTS loop counts. Each iteration starts
from the initial canvas. One-image GIFs yield a single final frame.

The container is checked before native decoding: dimensions, rectangles, palettes,
extensions, sub-block lengths and trailer. Before each requested frame, code
references and exact expanded length are checked using the native dictionary as
a length table. There is no second dictionary or all-frame entropy pass at open.
Unsupported plain-text graphics, interactive frames, reserved disposal methods
and streams with 65,000 codes between dictionary resets fail explicitly. The last
limit prevents the upstream decoder's 16-bit code counter from wrapping.

The private native bridge compiles the installed upstream source under private
entry-point names. It replaces its signed 32-bit byte-load macro with unsigned,
alignment-safe loads and disables speculative word copies at row ends. This
avoids modifying managed dependencies or keeping a source fork. Re-run pixel and
sanitizer tests when upgrading LVGL. `LV_USE_GIF=0` builds a no-allocation
unsupported stub. LVGL and ESP backend lists include this optional adapter;
the desktop product composes PNG, POSIX JPEG and LVGL GIF explicitly so the
headless POSIX library remains independent of LVGL.
An integration that supplies only prebuilt LVGL libraries without the GIF source
also gets this stub. ESP-IDF's managed LVGL and the desktop pinned source tree
provide the source automatically; enabling GIF needs no platform entropy decoder.

GIF input is retained in bounded compressed storage and starts after download;
there is no incremental first-frame presentation yet. Only scaled pixels are
composed. Disposal 3 can retain one extra scaled background. Unchanged frames are
shared by reference; published assets remain independent of later frames.
If all frames are opaque and a global background exists, output uses RGB565,
matching the existing opaque JPEG path and halving pixel storage. This quantizes
colors to 5/6/5 bits; transparent animations retain straight BGRA8888. Tests check
the selected representation against the same conversion, not a claim of lossless
RGB888 color in the RGB565 case.

## Checks

With the desktop LVGL configuration:

```sh
cmake --build build/simulator/pai-touch --target pxa_gif_decode_test pxa_image_decode_test
ctest --test-dir build/simulator/pai-touch -R '^pxa_(gif_decode|image_decode)_test$' --output-on-failure
```

`tests/test_gif_decode.py` generates deterministic GIFs and full-size Pillow
references. Native tests compare every visible output pixel after nearest-sample
scaling, delays and iteration/end metadata. They keep the first frame alive
through later frames and decoder destruction, inject failure at every allocation,
cancel partial inputs, truncate files and mutate encoded bytes. A separate
ASan/UBSan build should instrument the native bridge, adapter, core, assets and
allocator together; use `-fno-sanitize-recover=all` so diagnostics cannot be mistaken
for a passing exit code. Python and Pillow are test-only dependencies.

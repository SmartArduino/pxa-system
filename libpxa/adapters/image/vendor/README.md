# Image decoder sources

Pngle/miniz: https://github.com/kikuchan/pngle at b1c68193f1d3f8642b3e0e095d457a828038e6fb.
Pngle is MIT licensed; miniz carries its public-domain notice in miniz.c.
PXA adds per-instance allocation hooks and validates dimensions before scanline allocation;
gamma tables are disabled. No whole-size PNG framebuffer is allocated. Unsigned chunk
reads and aligned miniz copies avoid undefined shifts and unaligned C accesses;
IEND requires a completed deflate stream. Header ordering, unknown critical chunks,
excess decompressed scanlines and metadata sizes are checked. Optional bKGD data is
skipped instead of interpreted through pngle's unsafe temporary channel array;
RGBA scratch storage belongs to each decoder instance.

The PNG source ceiling is 4096 per dimension / 4,194,304 pixels; output remains
caller-budgeted. Ring indices use bounded wrap instead of per-channel modulo,
and 8-bit samples avoid redundant normalization; packed and 16-bit samples retain
their original scaling. The adapter skips tiles with no output sample and resets
sampling on overlapping Adam7 passes (including 1–9 pixel edge cases). These
changes preserve pixel/alpha results and instance/workspace sizes. Input work is
split using the target's optional poll budget; it is not a wall-clock guarantee.

JPEG is not vendored by PXA. The ESP adapter calls the device ROM, the desktop
adapter uses system libjpeg, and the LVGL adapter calls the registered platform
decoder. New platforms can supply the
same four session callbacks without changing Core, the wire ABI or the SDK.

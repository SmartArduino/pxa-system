# Third-party arithmetic kernel

`src/services/game_render/raster_spans.c` and
`src/services/game_render/raster_projected_spans.c` adapt the lit-span,
perspective subdivision and inverse-depth arithmetic from
recrof/Quake-ESP32S3 (commit `b4c8c22bae3298b98c02166116eaeb91a34ffc24`),
`QuakeESP32S3/quake/d_scan.c`, based on MG24Quake.

Original copyright: id Software, Inc. (1996–1997), Nicola Wrachien
(2023–2024), and contributors. The derived kernel is licensed under
**GPL-2.0-or-later**, as marked in its source; the surrounding MIT license does
not replace those files' licenses. A copy of GPL version 2 is provided in
[licenses/GPL-2.0.txt](licenses/GPL-2.0.txt).

The Guest SDK helpers, ABI codecs and service ownership implementation are
original PXA code. The differential test in pxa-apps extracts its reference
macro and scanline/depth functions from the user's current vendored engine rather than distributing a
second copy inside this SDK.

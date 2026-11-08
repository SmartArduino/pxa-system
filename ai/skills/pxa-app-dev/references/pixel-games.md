# Pixel games on PXA

Start from the locked SDK's game, surface and game-input examples; query the
installed `context` for their real declarations. Prefer retained tiles/sprites,
texture atlases and dirty regions for a tile game. Use a bounded RGB565 Surface
for custom rasterization; account for width × height × 2 bytes per buffer,
double buffering, AOT, audio and asset caches together. Reuse buffers and handles
instead of allocating or decoding every frame. Separate a fixed simulation step
from rendering, cap catch-up after suspend and measure physical presentation FPS.
Use seeded procedural content for reproducible tests and async atomic saves at
checkpoints; never block each frame on filesystem writes.

For Chinese pixel lettering, consider [Z Labs Pixel 12px](https://github.com/Astro-2539/ZLabs-Pixel-12px)
(CN/HC/JP variants), or the separate RoundPix 12px/16px families. Pick the exact
font file and upstream license, pin its revision/hash and preserve its license,
credits and any reserved font name conditions. These are app assets, not the
system's default UI font. Do not assume any vaguely named ZLabs font is covered
by another family's license. Subset the characters needed for fixed labels;
for dynamic text retain the required coverage and a visible missing-glyph fallback.
Rasterize at the font's intended grid (12 or 16 pixels), then enlarge the bitmap
by an integer with nearest-neighbor sampling. Rendering at an arbitrary TTF size
or smoothing a bitmap can destroy its pixel grid.

Choose a logical canvas that fits the smallest target, then use
`scale = floor(min(available_width / canvas_width, available_height / canvas_height))`.
For scale >= 1 center the canvas at integer pixel offsets and letterbox the
remaining space. Apply the same scale to sprites, glyphs and hit testing:
`logical = floor((physical - offset) / scale)`; reject letterbox input first.
If scale < 1, choose a smaller layout or show a supported-size message; do not
silently clamp to 1 and crop controls. Use actual physical Surface dimensions
and safe bounds, with the installed SDK's conversions for logical UI coordinates.
Keep HUD and touch controls inside safe insets for rounded/round screens;
recompute layout on metrics changes. Confirm any intentionally fixed resolution.

Support input edges, hold/repeat and release, including cancel, suspend and focus
loss. Keep touch targets readable at the real display size and implement gamepad
navigation only where requested. Enumerate Sensor service descriptors and
permissions; provide a manual control path if a sensor is optional or denied.
Test the smallest and largest target plus any requested round/rounded profile.
For store screenshots use the actual rectangular simulator capture mode; keep
normal adaptive-layout tests as well. Never replace runtime evidence with artwork.

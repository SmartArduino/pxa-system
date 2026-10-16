# Density-aware UI symbols

`pxa_lvgl_ui_icon_font(density_q16)` selects the nearest raster size for an
18 dp icon from 8/12/16/24/32/48 px banks. In particular, 130 DPI selects 16 px
and 284/305 DPI selects 32 px. Graphics follow display density independently
of the accessibility text scale. Sizes outside the supported range clamp to
the smallest/largest bank.

The banks contain the same 60 Font Awesome private-use glyphs as LVGL's built-in
Montserrat fonts, with their original 4-bit antialiasing and metrics. Latin
glyphs and kerning tables are omitted. The default LVGL face remains a fallback
for other characters. Font data comes from Font Awesome Free / Fonticons, Inc.,
distributed under SIL OFL 1.1 in [symbols-LICENSE.txt](symbols-LICENSE.txt).

All descriptors, maps and pixels reside in read-only storage. Selecting a bank
and drawing its glyphs requires no new font face, atlas, image asset, transform
layer or runtime scaling buffer. This trades additional firmware Flash for
larger icons without allocating another font cache. Hosts moving all rodata to
PSRAM must account for that platform setting separately.

The adapter's ICON role selects this font when no custom icon font is supplied.
An explicit theme icon font takes precedence. ESP and desktop product Hosts
also populate the theme from their initial density so input methods and PXA
widgets use the same bank. Give icon labels content height; a fixed container
sized for the old 14 px glyphs can clip the new font.

Regenerate offline from the managed LVGL source in the product repository:

```sh
python deps/pxa-system/tools/fonts/generate_ui_symbols.py \
  --lvgl-font-dir firmware/managed_components/lvgl__lvgl/src/font \
  --output deps/pxa-system/libpxa/adapters/lvgl/pxa_lvgl_ui_symbols.c
```

The native LVGL regression compares all 60 symbols at 16 px byte-for-byte with
the original font, checks navigation glyph growth at high DPI and verifies no
LVGL heap allocation during font selection/glyph lookup. App-specific layout
tests must still verify actual glyph, label and touch-target bounds.

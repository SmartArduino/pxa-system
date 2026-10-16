The IME label bitmap font is a derivative of PXA Sans SC Regular, based on Noto
Sans SC and Noto Sans CJK SC Regular 2.004 (Chinese mainland outlines).
Copyright 2014–2021 Adobe; Noto is a trademark of Google Inc.
Distributed under the SIL Open Font License 1.1 in OFL.txt.

Only 39 glyphs for 12 px fixed keyboard labels (including the middle dot) are
included: 5,532 bytes of A8 pixels in read-only storage. Candidate words use the existing
product face for other characters. A8 pixels preserve antialiasing without a
runtime glyph atlas or FreeType cache for these labels. Larger sizes keep the
product font.

Very narrow keyboards additionally use `ime_latin_12.h`: 95 printable ASCII
glyphs from the same SC face at 12 px, horizontally condensed to 62.5% width.
Its 3,370 bytes of antialiased A8 pixels and glyph metadata are read-only; the
desktop build totals 4,472 bytes. CJK characters retain the existing face and
size. This avoids clipping number/letter groups on narrow four-column keypads
without opening another font face or allocating a runtime glyph cache.

Rebuild from pxa-projects:

```sh
python3 deps/pxa-system/tools/fonts/generate_ime_labels.py \
  factory/base/system/fonts/noto_sans_cjk_common.ttf \
  deps/pxa-system/libpxa/adapters/lvgl/fonts/ime_label_12.h
python3 deps/pxa-system/tools/fonts/generate_ime_labels.py \
  factory/base/system/fonts/noto_sans_cjk_common.ttf \
  deps/pxa-system/libpxa/adapters/lvgl/fonts/ime_latin_12.h --compact-ascii
```

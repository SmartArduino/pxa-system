#include "pxa_lvgl_gif_native.h"
#if PXA_LVGL_GIF_AVAILABLE
/* LVGL 9.6's portable INTELLONG shifts promoted signed int by 24; UBSan
 * reports overflow on ordinary valid GIF bytes. Correct only that load when
 * compiling the upstream codec for PXA. Byte loads remain alignment safe on
 * ESP32; no changes to installed dependencies or global LVGL configuration. */
#undef INTELLONG
#define INTELLONG(p) ((uint32_t)(p)[0] | (uint32_t)(p)[1] << 8 | \
                     (uint32_t)(p)[2] << 16 | (uint32_t)(p)[3] << 24)
#include "src/libs/gif/gif.c"
#endif

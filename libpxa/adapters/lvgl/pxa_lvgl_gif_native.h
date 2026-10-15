#ifndef PXA_LVGL_GIF_NATIVE_H
#define PXA_LVGL_GIF_NATIVE_H
#include "lvgl.h"
#if LV_USE_GIF && __has_include("src/libs/gif/gif.c")
#define PXA_LVGL_GIF_AVAILABLE 1
/* Raw rows use exact byte copies, including their final one to three bytes. */
#undef ALLOWS_UNALIGNED
/* Private names keep this bounded adapter independent of LVGL's GIF widget.
 * Both compile the same pinned upstream implementation; no vendored fork. */
#define GIF_openRAM pxa_lvgl_gif_native_open_ram
#define GIF_openFile pxa_lvgl_gif_native_open_file
#define GIF_close pxa_lvgl_gif_native_close
#define GIF_begin pxa_lvgl_gif_native_begin
#define GIF_reset pxa_lvgl_gif_native_reset
#define GIF_playFrame pxa_lvgl_gif_native_play_frame
#define GIF_getCanvasWidth pxa_lvgl_gif_native_width
#define GIF_getCanvasHeight pxa_lvgl_gif_native_height
#define GIF_getComment pxa_lvgl_gif_native_comment
#define GIF_getInfo pxa_lvgl_gif_native_info
#define GIF_getLastError pxa_lvgl_gif_native_error
#define GIF_getLoopCount pxa_lvgl_gif_native_loops
#include "src/libs/gif/AnimatedGIF.h"
#else
#define PXA_LVGL_GIF_AVAILABLE 0
#endif
#endif

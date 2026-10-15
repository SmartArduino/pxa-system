#ifndef PXA_IMAGE_CODECS_H
#define PXA_IMAGE_CODECS_H
#include "pxa/image_decode.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Optional adapters, linked by the product. A new LVGL product can use the
 * shared PNG + registered-LVGL list; a native codec precedes LVGL as desired.
 * No new Wasm import, format opcode or platform-specific SDK is required. */
extern const pxa_image_codec_t pxa_stream_png_codec;
extern const pxa_image_codec_t pxa_lvgl_image_codec;
extern const pxa_image_codec_t pxa_lvgl_gif_codec;
extern const pxa_image_codec_t pxa_posix_jpeg_codec;
extern const pxa_image_codec_t pxa_esp_rom_jpeg_codec;
const pxa_image_backend_t *pxa_lvgl_image_backend(void);
const pxa_image_backend_t *pxa_posix_image_backend(void);
const pxa_image_backend_t *pxa_esp_rom_image_backend(void);
#ifdef __cplusplus
}
#endif
#endif

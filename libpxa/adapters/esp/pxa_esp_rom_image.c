#include "../image/image_buffer.h"
#include "pxa/image/codecs.h"
#include "sdkconfig.h"
#if CONFIG_ESP_ROM_HAS_JPEG_DECODE
#include "rom/tjpgd.h"

typedef struct {
    pxa_image_buffer_t input;
    size_t position;
    pxa_status_t status;
} rom_session_t;
static pxa_status_t open_rom(pxa_bytes_t signature, pxa_image_target_t *target,
                             const pxa_memory_allocator_t *temporary, void **session) {
    *session = NULL;
    if (signature.size < 3 || signature.data[0] != 255 || signature.data[1] != 216 ||
        signature.data[2] != 255)
        return PXA_STATUS_UNSUPPORTED;
    rom_session_t *s = pxa_memory_allocate(temporary, sizeof(*s));
    if (!s)
        return PXA_STATUS_RESOURCE_LIMIT;
    memset(s, 0, sizeof(*s));
    s->input.target = target;
    s->input.temporary = temporary;
    *session = s;
    return PXA_STATUS_OK;
}
static pxa_status_t write_rom(void *session, pxa_bytes_t bytes) {
    return pxa_image_buffer_write(&((rom_session_t *)session)->input, bytes);
}
static UINT read_rom(JDEC *jpeg, BYTE *output, UINT bytes) {
    rom_session_t *s = jpeg->device;
    size_t n = s->input.size - s->position;
    if (n > bytes)
        n = bytes;
    if (output && n)
        memcpy(output, s->input.data + s->position, n);
    s->position += n;
    return (UINT)n;
}
static UINT pixels_rom(JDEC *jpeg, void *pixels, JRECT *rect) {
    rom_session_t *s = jpeg->device;
    uint32_t factor = 1u << jpeg->scale, w = rect->right - rect->left + 1u,
             h = rect->bottom - rect->top + 1u;
    pxa_image_tile_t tile = {rect->left * factor,
                             rect->top * factor,
                             w * factor,
                             h * factor,
                             w,
                             h,
                             w * 3,
                             PXA_IMAGE_RGB888,
                             pixels};
    s->status = pxa_image_target_blit(s->input.target, &tile);
    return !s->status;
}
static pxa_status_t finish_rom(void *session) {
    rom_session_t *s = session;
    uint16_t source_width, source_height;
    pxa_status_t checked = pxa_image_jpeg_info((pxa_bytes_t){s->input.data, s->input.size},
                                               &source_width, &source_height);
    if (checked)
        return checked;
    void *workspace = pxa_memory_allocate(s->input.temporary, 8192);
    if (!workspace)
        return PXA_STATUS_RESOURCE_LIMIT;
    JDEC jpeg = {0};
    JRESULT result = jd_prepare(&jpeg, read_rom, workspace, 8192, s);
    pxa_status_t status = result == JDR_OK
                              ? pxa_image_target_begin(s->input.target, jpeg.width, jpeg.height, 0)
                          : result == JDR_FMT2 || result == JDR_FMT3 ? PXA_STATUS_UNSUPPORTED
                                                                     : PXA_STATUS_PROTOCOL_ERROR;
    if (!status) {
        uint16_t w, h;
        pxa_image_target_dimensions(s->input.target, &w, &h);
        BYTE scale = 0;
        while (scale < 3 && (jpeg.width >> (scale + 1)) >= w && (jpeg.height >> (scale + 1)) >= h)
            ++scale;
        if (jd_decomp(&jpeg, pixels_rom, scale) != JDR_OK)
            status = s->status ? s->status : PXA_STATUS_PROTOCOL_ERROR;
    }
    pxa_memory_release(workspace);
    return status;
}
static void close_rom(void *session) {
    rom_session_t *s = session;
    pxa_memory_release(s->input.data);
    pxa_memory_release(s);
}
const pxa_image_codec_t pxa_esp_rom_jpeg_codec = {.open = open_rom, .write = write_rom, .finish = finish_rom, .close = close_rom};
#endif
const pxa_image_backend_t *pxa_esp_rom_image_backend(void) {
    static const pxa_image_codec_t *const codecs[] = {&pxa_stream_png_codec, &pxa_lvgl_gif_codec,
#if CONFIG_ESP_ROM_HAS_JPEG_DECODE
                                                      &pxa_esp_rom_jpeg_codec
#else
                                                      &pxa_lvgl_image_codec
#endif
    };
    static const pxa_image_backend_t backend = {codecs, sizeof(codecs) / sizeof(codecs[0])};
    return &backend;
}

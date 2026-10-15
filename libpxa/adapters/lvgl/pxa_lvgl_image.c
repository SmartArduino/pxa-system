#include "../image/image_buffer.h"
#include "lvgl.h"
#include "pxa/image/codecs.h"
#if __has_include("src/image/lv_image_decoder_private.h")
#include "src/image/lv_image_decoder_private.h"
#else
#include "src/draw/lv_image_decoder_private.h"
#endif

/* Reuse the registered device decoders (including esp_lv_decoder) and their
 * native buffers. There is no PXA copy of a JPEG/PNG codec here. PNG streaming
 * is selected first by the default list because LodePNG expands the original
 * image. JPEG on ROM-capable devices uses the bounded MCU adapter instead.
 * LVGL owns its internal workspace; product heap telemetry must account for
 * it separately from the PXA budget. No global allocation hooks are changed. */
static pxa_status_t open_lvgl(pxa_bytes_t signature, pxa_image_target_t *target,
                              const pxa_memory_allocator_t *temporary, void **session) {
    *session = NULL;
    static const uint8_t png[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (signature.size != 8 ||
        (memcmp(signature.data, png, 8) &&
         !(signature.data[0] == 255 && signature.data[1] == 216 && signature.data[2] == 255)))
        return PXA_STATUS_UNSUPPORTED;
    pxa_image_buffer_t *s = pxa_memory_allocate(temporary, sizeof(*s));
    if (!s)
        return PXA_STATUS_RESOURCE_LIMIT;
    memset(s, 0, sizeof(*s));
    s->target = target;
    s->temporary = temporary;
    *session = s;
    return PXA_STATUS_OK;
}
static pxa_status_t write_lvgl(void *session, pxa_bytes_t input) {
    return pxa_image_buffer_write(session, input);
}
static pxa_status_t blit(pxa_image_target_t *target, const lv_draw_buf_t *buffer,
                         const lv_area_t *area) {
    if (!buffer || !buffer->data || area->x1 < 0 || area->y1 < 0)
        return PXA_STATUS_PROTOCOL_ERROR;
    /* The common target stores straight alpha. Do not silently interpret
     * premultiplied native pixels as straight and darken translucent edges. */
    if (buffer->header.flags & LV_IMAGE_FLAGS_PREMULTIPLIED)
        return PXA_STATUS_UNSUPPORTED;
    uint8_t format;
    switch (buffer->header.cf) {
    case LV_COLOR_FORMAT_RGB565:
        format = PXA_IMAGE_RGB565;
        break;
    case LV_COLOR_FORMAT_RGB888:
        format = PXA_IMAGE_BGR888;
        break;
    case LV_COLOR_FORMAT_ARGB8888:
        format = PXA_IMAGE_BGRA8888;
        break;
    case LV_COLOR_FORMAT_XRGB8888:
        format = PXA_IMAGE_BGRA8888;
        break;
    default:
        return PXA_STATUS_UNSUPPORTED;
    }
    pxa_image_tile_t tile = {(uint32_t)area->x1,
                             (uint32_t)area->y1,
                             (uint32_t)lv_area_get_width(area),
                             (uint32_t)lv_area_get_height(area),
                             buffer->header.w,
                             buffer->header.h,
                             buffer->header.stride,
                             format,
                             buffer->data};
    return pxa_image_target_blit(target, &tile);
}
static pxa_status_t finish_lvgl(void *session) {
    pxa_image_buffer_t *s = session;
    lv_image_dsc_t source = {0};
    source.header.magic = LV_IMAGE_HEADER_MAGIC;
    source.header.cf = LV_COLOR_FORMAT_RAW;
    source.data = s->data;
    source.data_size = (uint32_t)s->size;
    if (s->size > 2 && s->data[0] == 255 && s->data[1] == 216) {
        uint16_t w, h;
        pxa_status_t checked = pxa_image_jpeg_info((pxa_bytes_t){s->data, s->size}, &w, &h);
        if (checked)
            return checked;
        source.header.w = w;
        source.header.h = h;
    }
    lv_image_header_t header = {0};
    pxa_status_t status = PXA_STATUS_UNSUPPORTED;
    lv_lock();
    if (lv_image_decoder_get_info(&source, &header) != LV_RESULT_OK)
        goto done;
    /* Check untrusted dimensions before asking a native decoder to allocate. */
    if (!header.w || !header.h || header.w > 4096 || header.h > 4096 ||
        (uint32_t)header.w * header.h > 1048576) {
        status = PXA_STATUS_LIMIT_EXCEEDED;
        goto done;
    }
    int alpha = header.cf == LV_COLOR_FORMAT_ARGB8888 || header.cf == LV_COLOR_FORMAT_RAW_ALPHA;
    status = pxa_image_target_begin(s->target, header.w, header.h, alpha);
    if (status)
        goto done;
    lv_image_decoder_dsc_t decoder = {0};
    lv_image_decoder_args_t args = {0};
    args.no_cache = true;
    if (lv_image_decoder_open(&decoder, &source, &args) != LV_RESULT_OK) {
        status = PXA_STATUS_PROTOCOL_ERROR;
        goto done;
    }
    lv_area_t full = {0, 0, (int32_t)header.w - 1, (int32_t)header.h - 1};
    if (!decoder.decoder->get_area_cb && decoder.decoded)
        status = blit(s->target, decoder.decoded, &full);
    else {
        lv_area_t area = {LV_COORD_MIN, LV_COORD_MIN, LV_COORD_MIN, LV_COORD_MIN};
        uint32_t covered = 0, expected = (uint32_t)header.w * header.h;
        while (covered < expected) {
            if (lv_image_decoder_get_area(&decoder, &full, &area) != LV_RESULT_OK || area.x1 < 0 ||
                area.y1 < 0 || area.x2 < area.x1 || area.y2 < area.y1 || area.x2 >= header.w ||
                area.y2 >= header.h) {
                status = PXA_STATUS_PROTOCOL_ERROR;
                break;
            }
            status = blit(s->target, decoder.decoded, &area);
            if (status)
                break;
            uint32_t pixels =
                (uint32_t)lv_area_get_width(&area) * (uint32_t)lv_area_get_height(&area);
            if (pixels > expected - covered) {
                status = PXA_STATUS_PROTOCOL_ERROR;
                break;
            }
            covered += pixels;
        }
    }
    lv_image_decoder_close(&decoder);
done:
    /* no_cache bypasses the pixel cache; drop the transient pointer's header
     * as well, before freeing encoded bytes or reusing its address. */
    lv_image_cache_drop(&source);
    lv_unlock();
    return status;
}
static void close_lvgl(void *session) {
    pxa_image_buffer_t *s = session;
    pxa_memory_release(s->data);
    pxa_memory_release(s);
}
const pxa_image_codec_t pxa_lvgl_image_codec = {.open = open_lvgl, .write = write_lvgl, .finish = finish_lvgl, .close = close_lvgl};
const pxa_image_backend_t *pxa_lvgl_image_backend(void) {
    static const pxa_image_codec_t *const codecs[] = {&pxa_stream_png_codec, &pxa_lvgl_gif_codec, &pxa_lvgl_image_codec};
    static const pxa_image_backend_t backend = {codecs, sizeof(codecs) / sizeof(codecs[0])};
    return &backend;
}

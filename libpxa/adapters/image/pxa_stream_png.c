#include "pxa/image/codecs.h"
#include "vendor/pngle.h"
#include <string.h>

typedef struct {
    pxa_image_target_t *target;
    const pxa_memory_allocator_t *temporary;
    pngle_t *png;
    uint32_t width, height, pixels_seen, row_y, row_h, next_source_x, previous_end;
    uint16_t output_width, output_height;
    size_t pending_size;
    pxa_status_t status;
    uint8_t done, row_active, pending[512];
} png_session_t;
static void *allocate(void *context, size_t bytes) {
    return pxa_memory_allocate(((png_session_t *)context)->temporary, bytes);
}
static void release(void *context, void *memory) {
    (void)context;
    pxa_memory_release(memory);
}
static void begin(pngle_t *png, uint32_t w, uint32_t h) {
    png_session_t *s = pngle_get_user_data(png);
    s->width = w;
    s->height = h;
    /* Output remains caller-budgeted. Source storage is bounded by width,
     * not source area; retain an area limit to bound decompression work. */
    s->status = !w || !h || w > PNGLE_MAX_DIMENSION || h > PNGLE_MAX_DIMENSION || w > PNGLE_MAX_PIXELS / h
                    ? PXA_STATUS_LIMIT_EXCEEDED
                    : pxa_image_target_begin(s->target, w, h, 1);
    pxa_image_target_dimensions(s->target, &s->output_width, &s->output_height);
}
static void done(pngle_t *png) {
    png_session_t *s = pngle_get_user_data(png);
    s->done = 1;
    if (s->pixels_seen != s->width * s->height)
        s->status = PXA_STATUS_PROTOCOL_ERROR;
}
static void pixel(pngle_t *png, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  const uint8_t rgba[4]) {
    png_session_t *s = pngle_get_user_data(png);
    if (s->status)
        return;
    ++s->pixels_seen;
    if (y != s->row_y || h != s->row_h) {
        s->row_y = y;
        s->row_h = h;
        uint32_t first = (y * s->output_height + s->height - 1) / s->height;
        uint32_t end = ((y + h) * s->output_height + s->height - 1) / s->height;
        s->row_active = first != end;
        s->next_source_x = 0;
        s->previous_end = 0;
    }
    if (!s->row_active)
        return;
    /* Adam7 can revisit the same row in a later pass. Overlapping tiles start
     * a fresh run, including tiny images whose previous pass had one tile.
     * Skip source tiles containing no nearest-neighbour sample;
     * keep the target's existing alpha/channel/interlace handling unchanged. */
    if (x < s->previous_end)
        s->next_source_x = 0;
    s->previous_end = x + w;
    if (x + w <= s->next_source_x)
        return;
    pxa_image_tile_t tile = {x, y, w, h, 1, 1, 4, PXA_IMAGE_RGBA8888, rgba};
    s->status = pxa_image_target_blit(s->target, &tile);
    uint32_t next_output_x = ((x + w) * s->output_width + s->width - 1) / s->width;
    s->next_source_x = next_output_x * s->width / s->output_width;
}
static pxa_status_t open_png(pxa_bytes_t signature, pxa_image_target_t *target,
                             const pxa_memory_allocator_t *temporary, void **session) {
    static const uint8_t magic[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    *session = NULL;
    if (signature.size != 8 || memcmp(signature.data, magic, 8))
        return PXA_STATUS_UNSUPPORTED;
    pxa_image_target_set_input_budget(target, 64);
    png_session_t *s = pxa_memory_allocate(temporary, sizeof(*s));
    if (!s)
        return PXA_STATUS_RESOURCE_LIMIT;
    memset(s, 0, sizeof(*s));
    s->temporary = temporary;
    s->target = target;
    s->row_y = UINT32_MAX;
    s->png = pngle_new_with_allocator(s, allocate, release);
    if (!s->png) {
        pxa_memory_release(s);
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    pngle_set_user_data(s->png, s);
    pngle_set_init_callback(s->png, begin);
    pngle_set_draw_callback(s->png, pixel);
    pngle_set_done_callback(s->png, done);
    *session = s;
    return PXA_STATUS_OK;
}
static pxa_status_t write_png(void *session, pxa_bytes_t input) {
    png_session_t *s = session;
    if (s->done)
        return PXA_STATUS_PROTOCOL_ERROR;
    while (input.size || s->pending_size) {
        size_t n = sizeof(s->pending) - s->pending_size;
        if (n > input.size)
            n = input.size;
        if (n)
            memcpy(s->pending + s->pending_size, input.data, n);
        s->pending_size += n;
        input.data += n;
        input.size -= n;
        int consumed = pngle_feed(s->png, s->pending, s->pending_size);
        if (consumed < 0)
            return PXA_STATUS_PROTOCOL_ERROR;
        if (s->status)
            return s->status;
        if ((size_t)consumed > s->pending_size)
            return PXA_STATUS_INTERNAL;
        s->pending_size -= (size_t)consumed;
        if (consumed && s->pending_size)
            memmove(s->pending, s->pending + consumed, s->pending_size);
        if (!consumed)
            return input.size ? PXA_STATUS_PROTOCOL_ERROR : PXA_STATUS_OK;
        if (s->done && (s->pending_size || input.size))
            return PXA_STATUS_PROTOCOL_ERROR;
    }
    return PXA_STATUS_OK;
}
static pxa_status_t finish_png(void *session) {
    png_session_t *s = session;
    return s->status                     ? s->status
           : s->done && !s->pending_size ? PXA_STATUS_OK
                                         : PXA_STATUS_PROTOCOL_ERROR;
}
static void close_png(void *session) {
    png_session_t *s = session;
    pngle_destroy(s->png);
    pxa_memory_release(s);
}
const pxa_image_codec_t pxa_stream_png_codec = {
    .open = open_png, .write = write_png, .finish = finish_png, .close = close_png};

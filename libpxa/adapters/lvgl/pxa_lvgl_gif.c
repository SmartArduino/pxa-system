#include "../image/image_buffer.h"
#include "pxa/image/codecs.h"
#include "lvgl.h"

#include "pxa_lvgl_gif_native.h"
#if PXA_LVGL_GIF_AVAILABLE

/* LVGL owns entropy decoding; this adapter owns untrusted container checks,
 * timing, disposal and immutable scaled frames. No original-sized canvas,
 * LVGL widget, timer, global allocator hook or unbounded frame table. Keep
 * dependency-specific GIFIMAGE access isolated here. */
typedef struct {
    size_t descriptor, data, end;
    uint16_t x, y, width, height, colors;
    uint32_t delay;
    uint8_t disposal, transparent, transparent_index, code_size;
} gif_frame_t;
typedef struct {
    pxa_image_buffer_t input;
    GIFIMAGE native;
    gif_frame_t previous, current;
    pxa_asset_object_t *canvas, *restore;
    size_t start, cursor;
    uint32_t count, index, iteration, iterations, background, rows;
    uint16_t width, height, global_colors;
    uint8_t metadata_ready, alpha;
    pxa_status_t status;
} gif_session_t;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int take(gif_session_t *s, size_t *at, size_t n) {
    if (*at > s->input.size || n > s->input.size - *at) return 0;
    *at += n;
    return 1;
}
static int blocks(gif_session_t *s, size_t *at) {
    for (;;) {
        if (!take(s, at, 1)) return 0;
        size_t n = s->input.data[*at - 1];
        if (!n) return 1;
        if (!take(s, at, n)) return 0;
    }
}
static pxa_status_t frame_info(gif_session_t *s, size_t *at, gif_frame_t *f) {
    memset(f, 0, sizeof(*f));
    const uint8_t *p = s->input.data;
    while (take(s, at, 1)) {
        uint8_t tag = p[*at - 1];
        if (tag == 0x3b)
            return *at == s->input.size ? PXA_STATUS_NOT_FOUND : PXA_STATUS_PROTOCOL_ERROR;
        if (tag == 0x21) {
            if (!take(s, at, 1)) break;
            uint8_t label = p[*at - 1];
            if (label == 0xf9) {
                size_t a = *at;
                if (!take(s, at, 6) || p[a] != 4 || p[a + 5] || (p[a + 1] & 0xe0)) break;
                f->disposal = (p[a + 1] >> 2) & 7;
                if (f->disposal > 3 || (p[a + 1] & 2)) return PXA_STATUS_UNSUPPORTED;
                f->transparent = p[a + 1] & 1;
                f->transparent_index = p[a + 4];
                f->delay = (uint32_t)le16(p + a + 2) * 10;
            } else {
                if (label == 0x01) return PXA_STATUS_UNSUPPORTED; /* plain text */
                size_t a = *at;
                if (!blocks(s, at)) break;
                if (!s->metadata_ready && label == 0xff && *at - a >= 17 && p[a] == 11 &&
                    (!memcmp(p + a + 1, "NETSCAPE2.0", 11) ||
                     !memcmp(p + a + 1, "ANIMEXTS1.0", 11)) &&
                    p[a + 12] == 3 && p[a + 13] == 1) {
                    uint16_t repeats = le16(p + a + 14);
                    s->iterations = repeats ? (uint32_t)repeats + 1 : UINT32_MAX;
                }
            }
            continue;
        }
        if (tag != 0x2c) break;
        f->descriptor = *at - 1;
        size_t a = *at;
        if (!take(s, at, 9)) break;
        f->x = le16(p + a); f->y = le16(p + a + 2);
        f->width = le16(p + a + 4); f->height = le16(p + a + 6);
        if (!f->width || !f->height || f->x >= s->width || f->y >= s->height ||
            f->width > s->width - f->x || f->height > s->height - f->y) break;
        f->colors = s->global_colors;
        if (p[a + 8] & 0x80) {
            f->colors = (uint16_t)(2u << (p[a + 8] & 7));
            if (!take(s, at, 3u * f->colors)) break;
        }
        if (!f->colors || !take(s, at, 1)) break;
        f->code_size = p[*at - 1];
        if (f->code_size < 2 || f->code_size > 8) break;
        f->data = *at;
        if (!blocks(s, at)) break;
        f->end = *at;
        return PXA_STATUS_OK;
    }
    return PXA_STATUS_PROTOCOL_ERROR;
}

/* Validate code references and exact expanded length before LVGL's native
 * decoder runs. Reuse its dictionary as a length table, adding no workspace.
 * This emits no pixels. Bounds also prevent native 16-bit nextcode wrapping
 * in very long deferred-clear streams. Such streams fail explicitly. */
static pxa_status_t check_codes(gif_session_t *s, const gif_frame_t *f) {
    const uint8_t *p = s->input.data;
    size_t at = f->data;
    uint32_t bits = 0, available = 0, left = 0, total = 0, codes = 0;
    unsigned clear = 1u << f->code_size, next = clear + 2, width = f->code_size + 1;
    unsigned old_length = 0;
    for (;;) {
        while (available < width) {
            if (!left) {
                if (at >= f->end || !(left = p[at++])) return PXA_STATUS_PROTOCOL_ERROR;
            }
            if (at >= f->end) return PXA_STATUS_PROTOCOL_ERROR;
            bits |= (uint32_t)p[at++] << available; available += 8; --left;
        }
        unsigned code = bits & ((1u << width) - 1);
        bits >>= width; available -= width;
        if (code == clear) {
            next = clear + 2; width = f->code_size + 1; old_length = 0; codes = 0;
            continue;
        }
        if (code == clear + 1)
            return total == (uint32_t)f->width * f->height ? PXA_STATUS_OK : PXA_STATUS_PROTOCOL_ERROR;
        if (++codes >= 65000) return PXA_STATUS_UNSUPPORTED;
        unsigned length;
        if (code < clear) {
            if (code >= f->colors && !(f->transparent && code == f->transparent_index))
                return PXA_STATUS_PROTOCOL_ERROR;
            length = 1;
        } else if (code < next && code >= clear + 2 && old_length)
            length = s->native.usGIFTable[code];
        else if (code == next && old_length) length = old_length + 1;
        else return PXA_STATUS_PROTOCOL_ERROR;
        if (!length || length > (uint32_t)f->width * f->height - total)
            return PXA_STATUS_PROTOCOL_ERROR;
        total += length;
        if (old_length && next < 4096) {
            s->native.usGIFTable[next++] = (uint16_t)(old_length + 1);
            if (next == (1u << width) && width < 12) ++width;
        }
        old_length = length;
    }
}
static int32_t read_input(GIFFILE *file, uint8_t *out, int32_t n) {
    if (n <= 0 || file->iPos < 0 || file->iPos >= file->iSize) return 0;
    if (n > file->iSize - file->iPos) n = file->iSize - file->iPos;
    memcpy(out, file->pData + file->iPos, (size_t)n);
    file->iPos += n;
    return n;
}
static int32_t seek_input(GIFFILE *file, int32_t at) {
    if (at < 0) at = 0;
    if (at > file->iSize) at = file->iSize;
    return file->iPos = at;
}
static pxa_status_t fill(gif_session_t *s, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t rgba) {
    uint8_t color[4] = {(uint8_t)(rgba >> 24), (uint8_t)(rgba >> 16),
                         (uint8_t)(rgba >> 8), (uint8_t)rgba};
    pxa_image_tile_t tile = {x, y, w, h, 1, 1, 4, PXA_IMAGE_RGBA8888, color};
    return pxa_image_target_blit(s->input.target, &tile);
}
static void draw_row(GIFDRAW *draw) {
    gif_session_t *s = draw->pUser;
    if (s->status) return;
    ++s->rows;
    uint16_t w, h;
    pxa_image_target_dimensions(s->input.target, &w, &h);
    uint32_t y = (uint32_t)(draw->iY + draw->y);
    if (draw->y < 0 || draw->y >= draw->iHeight || y >= s->height) {
        s->status = PXA_STATUS_PROTOCOL_ERROR; return;
    }
    if ((y * h + s->height - 1) / s->height == ((y + 1) * h + s->height - 1) / s->height)
        return;
    uint8_t pixels[64 * 4];
    for (unsigned x = 0; x < (unsigned)draw->iWidth;) {
        if (s->current.transparent && draw->pPixels[x] == s->current.transparent_index) { ++x; continue; }
        unsigned start = x, n = 0;
        while (x < (unsigned)draw->iWidth && n < 64 &&
               !(s->current.transparent && draw->pPixels[x] == s->current.transparent_index)) {
            unsigned index = draw->pPixels[x++];
            if (index >= s->current.colors) { s->status = PXA_STATUS_PROTOCOL_ERROR; return; }
            memcpy(pixels + n * 4, draw->pPalette24 + index * 3, 3);
            pixels[n++ * 4 + 3] = 255;
        }
        pxa_image_tile_t tile = {(uint32_t)draw->iX + start, y, n, 1, n, 1, n * 4,
                                 PXA_IMAGE_RGBA8888, pixels};
        s->status = pxa_image_target_blit(s->input.target, &tile);
        if (s->status) return;
    }
}
static pxa_status_t open_gif(pxa_bytes_t signature, pxa_image_target_t *target,
                             const pxa_memory_allocator_t *temporary, void **out) {
    *out = NULL;
    if (signature.size < 6 || (memcmp(signature.data, "GIF87a", 6) &&
                               memcmp(signature.data, "GIF89a", 6))) return PXA_STATUS_UNSUPPORTED;
    gif_session_t *s = pxa_memory_allocate(temporary, sizeof(*s));
    if (!s) return PXA_STATUS_RESOURCE_LIMIT;
    memset(s, 0, sizeof(*s)); s->input.target = target; s->input.temporary = temporary;
    *out = s;
    return PXA_STATUS_OK;
}
static pxa_status_t write_gif(void *session, pxa_bytes_t bytes) {
    return pxa_image_buffer_write(&((gif_session_t *)session)->input, bytes);
}
static pxa_status_t finish_gif(void *session) {
    gif_session_t *s = session;
    const uint8_t *p = s->input.data;
    if (s->input.size < 14) return PXA_STATUS_PROTOCOL_ERROR;
    s->width = le16(p + 6); s->height = le16(p + 8);
    if (!s->width || !s->height || s->width > 4096 || s->height > 4096 ||
        (uint32_t)s->width * s->height > 1048576) return PXA_STATUS_LIMIT_EXCEEDED;
    if (s->width > LV_GIF_MAX_WIDTH || s->height > LV_GIF_MAX_HEIGHT) return PXA_STATUS_UNSUPPORTED;
    s->start = 13;
    if (p[10] & 0x80) {
        s->global_colors = (uint16_t)(2u << (p[10] & 7));
        if (!take(s, &s->start, 3u * s->global_colors) || p[11] >= s->global_colors)
            return PXA_STATUS_PROTOCOL_ERROR;
        const uint8_t *bg = p + 13 + p[11] * 3;
        s->background = (uint32_t)bg[0] << 24 | (uint32_t)bg[1] << 16 | (uint32_t)bg[2] << 8 | 255;
    }
    s->iterations = 1;
    s->alpha = !s->global_colors;
    size_t at = s->start;
    for (;;) {
        gif_frame_t frame;
        pxa_status_t status = frame_info(s, &at, &frame);
        if (status == PXA_STATUS_NOT_FOUND) break;
        if (status) return status;
        s->alpha |= frame.transparent;
        ++s->count;
    }
    if (!s->count) return PXA_STATUS_PROTOCOL_ERROR;
    s->metadata_ready = 1;
    s->cursor = s->start;
    return pxa_image_target_begin(s->input.target, s->width, s->height, s->alpha);
}
static pxa_status_t next_gif(void *session, pxa_image_frame_info_t *info) {
    gif_session_t *s = session;
    int restart = s->index == s->count;
    if (restart) {
        if (s->count == 1 || (s->iterations != UINT32_MAX && s->iteration + 1 >= s->iterations))
            return PXA_STATUS_NOT_FOUND;
        ++s->iteration; s->index = 0; s->cursor = s->start;
    }
    pxa_status_t status = frame_info(s, &s->cursor, &s->current);
    if (status) return status;
    /* Decode work is one requested frame, never every frame in a file at
     * open. Structural scanning above is bounded by compressed input size. */
    status = check_codes(s, &s->current);
    if (status) return status;
    const int final_frame = s->index + 1 == s->count && (s->count == 1 ||
        (s->iterations != UINT32_MAX && s->iteration + 1 >= s->iterations));
    const pxa_asset_object_t *base = !restart && s->previous.disposal == 3 ? s->restore : s->canvas;
    if (restart) base = NULL;
    if (s->canvas) {
        status = pxa_image_target_begin_frame(s->input.target, base);
        if (status) return status;
    }
    if (!s->index)
        status = fill(s, 0, 0, s->width, s->height, s->current.transparent ? 0 : s->background);
    else if (s->previous.disposal == 2)
        status = fill(s, s->previous.x, s->previous.y, s->previous.width, s->previous.height,
                      s->previous.transparent ? 0 : s->background);
    if (status) return status;
    pxa_asset_object_t *restore = NULL;
    if (s->current.disposal == 3 && !final_frame) {
        if (s->index && s->previous.disposal != 2 && base) {
            restore = (pxa_asset_object_t *)base;
            pxa_asset_object_retain(restore);
        } else {
            status = pxa_image_target_copy_frame(s->input.target, &restore);
            if (status) return status;
        }
    }
    pxa_asset_object_release(s->restore); s->restore = restore;
    GIF_begin(&s->native, GIF_PALETTE_RGB888);
    s->native.iCanvasWidth = s->width; s->native.iCanvasHeight = s->height;
    s->native.GIFFile.pData = s->input.data;
    s->native.GIFFile.iPos = (int32_t)s->current.descriptor;
    s->native.GIFFile.iSize = (int32_t)s->current.end + 1;
    s->native.pfnRead = read_input; s->native.pfnSeek = seek_input; s->native.pfnDraw = draw_row;
    if (s->global_colors) memcpy(s->native.pPalette, s->input.data + 13, s->global_colors * 3u);
    s->rows = 0; s->status = PXA_STATUS_OK;
    (void)GIF_playFrame(&s->native, NULL, s);
    if (s->status) return s->status;
    if (GIF_getLastError(&s->native) || s->rows != s->current.height) return PXA_STATUS_PROTOCOL_ERROR;
    pxa_asset_object_release(s->canvas);
    s->canvas = pxa_image_target_retain_frame(s->input.target);
    s->previous = s->current;
    ++s->index;
    info->delay_ms = s->current.delay; info->iteration = s->iteration;
    info->last = (uint8_t)final_frame;
    return PXA_STATUS_OK;
}
static void close_gif(void *session) {
    gif_session_t *s = session;
    pxa_asset_object_release(s->canvas); pxa_asset_object_release(s->restore);
    pxa_memory_release(s->input.data); pxa_memory_release(s);
}
const pxa_image_codec_t pxa_lvgl_gif_codec = {
    .open = open_gif, .write = write_gif, .finish = finish_gif, .close = close_gif, .next = next_gif
};
#else
static pxa_status_t no_gif(pxa_bytes_t bytes, pxa_image_target_t *target,
                           const pxa_memory_allocator_t *temporary, void **session) {
    (void)bytes; (void)target; (void)temporary; *session = NULL;
    return PXA_STATUS_UNSUPPORTED;
}
static pxa_status_t no_write(void *session, pxa_bytes_t bytes) {
    (void)session; (void)bytes; return PXA_STATUS_UNSUPPORTED;
}
static pxa_status_t no_finish(void *session) { (void)session; return PXA_STATUS_UNSUPPORTED; }
static void no_close(void *session) { (void)session; }
const pxa_image_codec_t pxa_lvgl_gif_codec = {
    .open = no_gif, .write = no_write, .finish = no_finish, .close = no_close
};
#endif

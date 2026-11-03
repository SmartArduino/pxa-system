/* SPDX-License-Identifier: MIT */
#ifndef PXA_GUEST_FONT_H
#define PXA_GUEST_FONT_H
#include "pxa_core.h"
#define PXA_FONT_SERVICE 3u
#define PXA_FONT_GLYPH 13u
#define PXA_FONT_MAX_BITMAP_BYTES 3072u
typedef struct {
    int32_t status;
    uint16_t line_height, baseline, advance, width, height;
    int16_t offset_x, offset_y;
    uint16_t flags;
    const uint8_t *coverage; /* packed A8; borrowed until the next event */
} pxa_font_glyph_t;
static inline int32_t pxa_font_request_glyph(uint64_t token, uint32_t codepoint, uint16_t pixels) {
    uint8_t p[8] = {0};
    if (!token || pixels < 8 || pixels > 48 || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u32(p, codepoint); pxa_store_u16(p + 4, pixels);
    return pxa_control_call(PXA_FONT_SERVICE, PXA_FONT_GLYPH, token, p, sizeof(p));
}
static inline int pxa_font_parse_glyph(const pxa_event_t *event, uint64_t token, pxa_font_glyph_t *out) {
    if (!event || !out || !token || event->token != token || event->service != PXA_FONT_SERVICE ||
        event->opcode != PXA_FONT_GLYPH || !event->payload || event->payload_size < 4) return 0;
    pxa_zero(out, sizeof(*out));out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status) return event->payload_size == 4;
    if (event->payload_size < 20) return 0;
    const uint8_t *p = event->payload + 4;
    out->line_height = pxa_load_u16(p); out->baseline = pxa_load_u16(p + 2);
    out->advance = pxa_load_u16(p + 4); out->width = pxa_load_u16(p + 6); out->height = pxa_load_u16(p + 8);
    out->offset_x = (int16_t)pxa_load_u16(p + 10); out->offset_y = (int16_t)pxa_load_u16(p + 12);
    out->flags = pxa_load_u16(p + 14);
    size_t bytes = (size_t)out->width * out->height;
    if (!out->line_height || out->line_height > 128 || out->baseline > out->line_height ||
        out->advance > 128 || out->width > 64 || out->height > 64 || bytes > PXA_FONT_MAX_BITMAP_BYTES ||
        event->payload_size != 20 + bytes || out->offset_x < -128 || out->offset_x > 128 ||
        out->offset_y < -128 || out->offset_y > 128 || (out->flags & ~1u)) return 0;
    out->coverage = p + 16; return 1;
}
#endif

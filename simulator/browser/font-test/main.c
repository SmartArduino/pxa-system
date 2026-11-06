/* SPDX-License-Identifier: MIT */
#include "pxa_font.h"
#include "pxa_log.h"
int32_t pxa_app_start(const uint8_t *config, uint32_t size) {
    (void)config; (void)size;
    if (pxa_font_request_glyph(99, 'A', 49) != PXA_STATUS_INVALID_ARGUMENT ||
        pxa_font_request_glyph(99, 0xd800, 16) != PXA_STATUS_INVALID_ARGUMENT) return -1;
    return pxa_font_request_glyph(101, 'A', 16);
}
int32_t pxa_app_on_event(const uint8_t *data, uint32_t size) {
    pxa_event_t event; pxa_font_glyph_t glyph;
    if (!pxa_parse_event(data, size, &event)) return -1;
    if (pxa_font_parse_glyph(&event, 101, &glyph)) {
        if (glyph.status || !glyph.advance || !glyph.width || !glyph.height ||
            glyph.baseline > glyph.line_height || glyph.flags) return -1;
        unsigned coverage = 0;
        for (unsigned i=0; i<(unsigned)glyph.width*glyph.height; ++i) coverage |= glyph.coverage[i];
        if (!coverage) return -1;
        pxa_log_write(2, "glyph:A");
        return pxa_font_request_glyph(102, ' ', 16);
    }
    if (pxa_font_parse_glyph(&event, 102, &glyph)) {
        if (glyph.status || !glyph.advance) return -1;
        pxa_log_write(2, "glyph:space");
    }
    return 0;
}
void pxa_app_stop(uint32_t reason) { (void)reason; }

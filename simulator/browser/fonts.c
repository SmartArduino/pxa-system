/* SPDX-License-Identifier: MIT */
#include <emscripten.h>
#include "lvgl.h"
#include "fonts.h"

static lv_font_t fonts[257];
EM_JS(int, font_available, (), { return typeof Module.browserFontMetrics === 'function'; });
EM_JS(int, font_metrics, (int size, int code, int *metrics), {
    return Module.browserFontMetrics(size,code,metrics);
});
EM_JS(void, font_bitmap, (int size, int code, uint8_t *out, int stride), {
    Module.browserFontBitmap(size,code,out,stride);
});
static bool glyph_metrics(const lv_font_t *font, lv_font_glyph_dsc_t *out,
                           uint32_t code, uint32_t next) {
    (void)next;
    if (code < 32 || code > 0x10ffff) return false;
    int32_t values[5];
    if (!font_metrics((uintptr_t)font->user_data,code,values)) return false;
    out->adv_w=values[0]; out->box_w=values[1]; out->box_h=values[2];
    out->ofs_x=values[3]; out->ofs_y=values[4]; out->gid.index=code;
    out->format=LV_FONT_GLYPH_FORMAT_A8;out->stride=0;
    return true;
}
static const void *glyph_bitmap(lv_font_glyph_dsc_t *glyph, lv_draw_buf_t *out) {
    if (!out || !glyph->box_w || !glyph->box_h) return NULL;
    font_bitmap((uintptr_t)glyph->resolved_font->user_data,glyph->gid.index,
                out->data,out->header.stride);
    return out;
}
static const void *font_for_size(void *context, uint16_t size) {
    (void)context;
    if (!size || size>256) return NULL;
    lv_font_t *f=&fonts[size];
    if (!f->get_glyph_dsc) {
        f->user_data=(void *)(uintptr_t)size;
        f->get_glyph_dsc=glyph_metrics; f->get_glyph_bitmap=glyph_bitmap;
        f->line_height=(size*5+3)/4; f->base_line=(size+3)/4;
        f->underline_position=-1;f->underline_thickness=1;
    }
    return f;
}
void browser_fonts_init(pxa_lvgl_ui_config_t *config) {
    if (!font_available()) return;
    config->theme.caption_font=font_for_size(NULL,12);
    config->theme.label_font=font_for_size(NULL,14);
    config->theme.body_font=font_for_size(NULL,16);
    config->theme.title_font=font_for_size(NULL,20);
    config->theme.headline_font=font_for_size(NULL,24);
    config->theme.display_font=font_for_size(NULL,28);
    config->sized_text_font=font_for_size;
}

#include "pxsys/reference_ime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned closed;
static void on_close(void *ctx) { ++*(unsigned *)ctx; }
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *pixels) {
    (void)area; (void)pixels; lv_display_flush_ready(d);
}
static void check_buttons(lv_obj_t *obj, const pxsys_theme_snapshot_t *theme) {
    if (lv_obj_check_type(obj, &lv_button_class)) {
        assert(lv_color_to_u32(lv_obj_get_style_bg_color(obj, 0)) ==
               lv_color_to_u32(lv_color_hex(theme->colors[PXSYS_COLOR_SURFACE_CONTAINER_HIGHEST])));
        assert(lv_color_to_u32(lv_obj_get_style_text_color(obj, LV_STATE_PRESSED)) ==
               lv_color_to_u32(lv_color_hex(theme->colors[PXSYS_COLOR_ON_PRIMARY_CONTAINER])));
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        check_buttons(lv_obj_get_child(obj, i), theme);
}
int main(void) {
    lv_init();
    lv_display_t *display = lv_display_create(296,240);
    static uint8_t buffer[296*10*4];
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    lv_obj_t *input = lv_textarea_create(lv_screen_active());
    lv_textarea_set_text(input,"西遊記 https://example.org/");
    pxsys_reference_ime_t *ime = pxsys_reference_ime_create(lv_screen_active(),input);
    assert(ime);
    pxsys_reference_ime_set_close_callback(ime,on_close,&closed);
    pxsys_theme_snapshot_t theme = {0};
    for (unsigned pass = 0; pass < 20; ++pass) {
        for (unsigned i = 0; i < PXSYS_COLOR_TOKEN_COUNT; ++i)
            theme.colors[i] = (pass & 1) ? (0x172030u+i*13u) : (0xe0e0e0u-i*11u);
        pxsys_reference_ime_set_theme(ime,&theme);
        check_buttons(pxsys_reference_ime_get_root(ime),&theme);
        assert(strcmp(lv_textarea_get_text(input),"西遊記 https://example.org/")==0);
        pxsys_reference_ime_set_mode(ime,(pxsys_reference_ime_mode_t)(pass%4));
        pxsys_reference_ime_show(ime);
        assert(pxsys_reference_ime_is_visible(ime));
        pxsys_reference_ime_hide(ime);
        assert(!pxsys_reference_ime_is_visible(ime));
    }
    assert(closed==20);
    /* Guest surface replacement deletes the target before the keyboard. */
    lv_obj_delete(input);
    pxsys_reference_ime_set_theme(ime,&theme);
    pxsys_reference_ime_destroy(ime);
    lv_display_delete(display);
    lv_deinit();
    puts("System IME: live theme, all modes, reopen, target deletion OK");
}

#include "pxsys/reference_ime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned closed;
static unsigned submitted;
static void on_submit(lv_event_t *event) { (void)event; ++submitted; }
static void on_close(void *ctx) { ++*(unsigned *)ctx; }
static lv_obj_t *find_keyboard(lv_obj_t *root) {
    if (lv_obj_check_type(root, &lv_keyboard_class)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        lv_obj_t *found = find_keyboard(lv_obj_get_child(root, i));
        if (found) return found;
    }
    return NULL;
}
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
    lv_obj_add_event_cb(input,on_submit,LV_EVENT_READY,NULL);
    lv_obj_t *keyboard = find_keyboard(pxsys_reference_ime_get_root(ime));
    assert(keyboard);
    static const char *const confirm_map[] = {LV_SYMBOL_OK, ""};
    static const lv_buttonmatrix_ctrl_t confirm_ctrl[] = {1};
    for (unsigned pass = 0; pass < 2; ++pass) {
        pxsys_reference_ime_set_mode(ime,pass ? PXSYS_REFERENCE_IME_MODE_CHINESE
                                              : PXSYS_REFERENCE_IME_MODE_ENGLISH);
        pxsys_reference_ime_show(ime);
        // Landscape/native maps use OK; compact K9 maps use DOWN.
        lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_USER_1,confirm_map,confirm_ctrl);
        lv_keyboard_set_mode(keyboard,LV_KEYBOARD_MODE_USER_1);
        lv_buttonmatrix_set_selected_button(keyboard,0);
        lv_obj_send_event(keyboard,LV_EVENT_VALUE_CHANGED,NULL);
        assert(!pxsys_reference_ime_is_visible(ime));
        assert(submitted==pass+1 && closed==21+pass);
        assert(strcmp(lv_textarea_get_text(input),"西遊記 https://example.org/")==0);
    }
    /* Guest surface replacement deletes the target before the keyboard. */
    lv_obj_delete(input);
    pxsys_reference_ime_set_theme(ime,&theme);
    pxsys_reference_ime_destroy(ime);
    lv_display_delete(display);
    lv_deinit();
    puts("System IME: live theme, all modes, reopen, target deletion OK");
}

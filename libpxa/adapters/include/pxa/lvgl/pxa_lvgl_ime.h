#ifndef PXA_LVGL_IME_H
#define PXA_LVGL_IME_H
#include "pxa/ime.h"
#include "pxa/lvgl/pxa_lvgl_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct pxa_lvgl_ime pxa_lvgl_ime_t;
/* UI-thread callbacks enqueue work for the runtime owner. They must never
 * enter a Guest synchronously. generation identifies this editor/caret. */
typedef pxa_status_t (*pxa_lvgl_ime_input_fn)(void *context, uint64_t generation,
    uint16_t action, uint8_t layout, uint8_t language, uint32_t value, pxa_bytes_t text);
/* active describes the editor; needs_provider is false for English/password
 * and native fallback. Switching language can cancel an in-flight load. */
typedef void (*pxa_lvgl_ime_focus_fn)(void *context, uint64_t generation, bool active, bool needs_provider);
typedef struct {
    uint32_t struct_size;
    /* Borrowed full-display overlay parent, alive until destroy; NULL selects
     * the top layer. Its padding does not alter display-safe coordinates. */
    void *parent;
    pxa_lvgl_ui_theme_t theme;
    /* Occupies existing padding before the aligned environment. */
    pxa_ime_preferences_t preferences;
    pxa_ui_environment_t environment;
    void *context;
    /* NULL selects the local built-in decoder with the same keyboard UI.
     * Non-NULL enqueues work for an external Provider. */
    pxa_lvgl_ime_input_fn input;
    pxa_lvgl_ime_focus_fn focus;
    /* Optional borrowed CJK font path. At most one smaller face is opened on
     * demand when all supplied DPI fonts exceed the available keyboard space. */
    const char *sized_text_font_path;
} pxa_lvgl_ime_config_t;
/* UI thread / platform execute lock only. Allocates widgets on demand, no
 * framebuffer, text atlas or dictionary. The object survives editor deletion. */
pxa_lvgl_ime_t *pxa_lvgl_ime_create(const pxa_lvgl_ime_config_t *config);
void pxa_lvgl_ime_destroy(pxa_lvgl_ime_t *ime);
pxa_status_t pxa_lvgl_ime_focus(pxa_lvgl_ime_t *ime, void *textarea);
void pxa_lvgl_ime_close(pxa_lvgl_ime_t *ime);
pxa_status_t pxa_lvgl_ime_present(pxa_lvgl_ime_t *ime, uint64_t generation,
    const pxa_ime_frame_t *frame);
bool pxa_lvgl_ime_owns_textarea(const void *textarea);
void *pxa_lvgl_ime_root(const pxa_lvgl_ime_t *ime);
/* Borrowed current-line view, or NULL when the actual editor is visible.
 * This is an input aid owned by the Host, never a second editable text model. */
void *pxa_lvgl_ime_editor_view(const pxa_lvgl_ime_t *ime);
bool pxa_lvgl_ime_active(const pxa_lvgl_ime_t *ime);
/* Defer costly provider/runtime startup until the first keyboard draw. The
 * focus callback wakes the owner again when this becomes true. */
bool pxa_lvgl_ime_loading_allowed(const pxa_lvgl_ime_t *ime);
typedef struct {
    bool drawn, ready;
    uint32_t first_draw_ms, ready_ms;
} pxa_lvgl_ime_timing_t;
void pxa_lvgl_ime_timing(const pxa_lvgl_ime_t *ime, pxa_lvgl_ime_timing_t *output);
uint64_t pxa_lvgl_ime_generation(const pxa_lvgl_ime_t *ime);
/* After focus(active=true, needs_provider=false), the runtime owner releases
 * the Chinese provider, then acknowledges under the UI lock. The generation
 * prevents an old unload from changing a newer editor. No Guest entry here. */
void pxa_lvgl_ime_provider_released(pxa_lvgl_ime_t *ime,uint64_t generation);
/* Applies on the next focus, preserving an in-progress composition. */
void pxa_lvgl_ime_set_preferences(pxa_lvgl_ime_t *ime,pxa_ime_preferences_t preferences);
void pxa_lvgl_ime_set_environment(pxa_lvgl_ime_t *ime, const pxa_ui_environment_t *environment);
void pxa_lvgl_ime_set_theme(pxa_lvgl_ime_t *ime, const pxa_lvgl_ui_theme_t *theme);
#ifdef __cplusplus
}
#endif
#endif

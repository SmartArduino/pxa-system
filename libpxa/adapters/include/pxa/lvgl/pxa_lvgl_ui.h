#ifndef PXA_LVGL_UI_H
#define PXA_LVGL_UI_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "pxa/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pxa_lvgl_ui_execute_callback_fn)(void *data);
typedef pxa_status_t (*pxa_lvgl_ui_execute_fn)(
    pxa_lvgl_ui_execute_callback_fn callback, void *callback_data,
    void *user_data);

typedef const void *(*pxa_lvgl_ui_resolve_asset_fn)(
    const uint8_t *path, size_t path_size, void *user_data);
typedef void (*pxa_lvgl_ui_release_asset_fn)(
    const void *source, void *user_data);
typedef void (*pxa_lvgl_ui_event_fn)(
    uint32_t surface, uint32_t node, pxa_ui_event_kind_t kind,
    uint16_t flags, const void *value, size_t value_size, void *user_data);
typedef uint64_t (*pxa_lvgl_ui_now_us_fn)(void *user_data);

typedef struct {
    uint32_t rgba[32];
    const void *caption_font;
    const void *body_font;
    const void *title_font;
    const void *icon_font;
} pxa_lvgl_ui_theme_t;

typedef struct {
    uint32_t struct_size;
    void *allocator_context;
    pxa_ui_allocate_fn allocate;
    pxa_ui_release_fn release;
    void *execute_user_data;
    pxa_lvgl_ui_execute_fn execute;
    pxa_lvgl_ui_theme_t theme;
    /* A successful resolve returns one retained reference. Canvas preparation
     * calls resolve outside execute; providers synchronize shared cache state.
     * Release can run inside execute when objects or frames are replaced. */
    pxa_lvgl_ui_resolve_asset_fn resolve_asset;
    pxa_lvgl_ui_release_asset_fn release_asset;
    void *asset_user_data;
    pxa_lvgl_ui_event_fn event_callback;
    pxa_lvgl_ui_now_us_fn now_us;
    void *callback_user_data;
    pxa_ui_environment_t primary_environment;
} pxa_lvgl_ui_config_t;

typedef struct pxa_lvgl_ui pxa_lvgl_ui_t;

typedef struct {
    const uint16_t *pixels;
    const uint8_t *alpha;
    uint32_t pixel_stride_bytes;
    uint32_t alpha_stride_bytes;
    int32_t x;
    int32_t y;
    uint16_t width;
    uint16_t height;
    uint64_t revision;
} pxa_lvgl_ui_alpha_plane_t;

void pxa_lvgl_ui_theme_init(pxa_lvgl_ui_theme_t *theme);
size_t pxa_lvgl_ui_workspace_size(void);
pxa_status_t pxa_lvgl_ui_init(void *workspace, size_t workspace_size,
                                 const pxa_lvgl_ui_config_t *config,
                                 pxa_lvgl_ui_t **output,
                                 pxa_ui_backend_t *backend);
pxa_status_t pxa_lvgl_ui_set_theme(
    pxa_lvgl_ui_t *ui, const pxa_lvgl_ui_theme_t *theme);
void pxa_lvgl_ui_reset(pxa_lvgl_ui_t *ui);
void pxa_lvgl_ui_deinit(pxa_lvgl_ui_t *ui);
bool pxa_lvgl_ui_alpha_plane(const pxa_lvgl_ui_t *ui,
                              pxa_lvgl_ui_alpha_plane_t *output);
/* Valid only while event_callback is running. Pointer events preserve the
 * LVGL input sample time instead of replacing it at the Host boundary. */
uint64_t pxa_lvgl_ui_event_timestamp_us(const pxa_lvgl_ui_t *ui);

#ifdef __cplusplus
}
#endif

#endif

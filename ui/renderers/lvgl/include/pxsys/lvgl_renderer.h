#ifndef PXSYS_LVGL_RENDERER_H
#define PXSYS_LVGL_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "pxsys/renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_LVGL_RENDERER_ID "lvgl"
#define PXSYS_LVGL_RENDERER_FEATURE_NATIVE_ROOT UINT64_C(1)

typedef pxsys_status_t (*pxsys_lvgl_transaction_fn)(
    void* context, lv_obj_t* root, const pxsys_ui_transaction_t* transaction);

typedef struct {
    uint32_t struct_size;
    size_t max_surfaces;
    lv_obj_t* parent;
    void* transaction_context;
    pxsys_lvgl_transaction_fn apply_transaction;
    pxsys_allocator_t allocator;
} pxsys_lvgl_renderer_config_t;

typedef struct pxsys_lvgl_renderer pxsys_lvgl_renderer_t;

void pxsys_lvgl_renderer_config_init(pxsys_lvgl_renderer_config_t* config);
pxsys_status_t pxsys_lvgl_renderer_create(const pxsys_lvgl_renderer_config_t* config,
                                          pxsys_lvgl_renderer_t** output);
pxsys_status_t pxsys_lvgl_renderer_destroy(pxsys_lvgl_renderer_t* renderer);
pxsys_status_t pxsys_lvgl_renderer_provider(pxsys_lvgl_renderer_t* renderer,
                                            pxsys_renderer_provider_t* provider);

/* Backend-specific native extension. The returned object remains owned by the
 * renderer and is valid until the corresponding surface is destroyed. */
pxsys_status_t pxsys_lvgl_renderer_surface_root(pxsys_lvgl_renderer_t* renderer,
                                                pxsys_surface_ref_t surface,
                                                lv_obj_t** root);

#ifdef __cplusplus
}
#endif

#endif

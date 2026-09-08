#ifndef PXSYS_RENDERER_HOST_H
#define PXSYS_RENDERER_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t struct_size;
    size_t max_renderer_id_bytes;
    pxsys_theme_snapshot_t initial_theme;
    pxsys_allocator_t allocator;
} pxsys_renderer_host_config_t;

typedef struct {
    uint32_t struct_size;
    pxsys_string_t renderer_id;
    pxsys_version_t version;
    uint64_t features;
    size_t surface_count;
} pxsys_renderer_info_t;

typedef struct pxsys_renderer_host pxsys_renderer_host_t;

void pxsys_renderer_host_config_init(pxsys_renderer_host_config_t* config);
pxsys_status_t pxsys_renderer_host_create(const pxsys_renderer_host_config_t* config,
                                          pxsys_renderer_host_t** output);
pxsys_status_t pxsys_renderer_host_destroy(pxsys_renderer_host_t* host);

pxsys_status_t pxsys_renderer_host_bind(pxsys_renderer_host_t* host,
                                        const pxsys_renderer_provider_t* provider);
pxsys_status_t pxsys_renderer_host_unbind(pxsys_renderer_host_t* host,
                                          pxsys_string_t renderer_id);
pxsys_status_t pxsys_renderer_host_info(const pxsys_renderer_host_t* host,
                                        pxsys_renderer_info_t* output);
size_t pxsys_renderer_host_surface_count(const pxsys_renderer_host_t* host);
pxsys_status_t pxsys_renderer_host_set_theme(pxsys_renderer_host_t* host,
                                             const pxsys_theme_snapshot_t* theme);

pxsys_status_t pxsys_renderer_surface_create(pxsys_renderer_host_t* host,
                                             const pxsys_surface_config_t* config,
                                             pxsys_surface_ref_t* surface);
pxsys_status_t pxsys_renderer_surface_destroy(pxsys_renderer_host_t* host,
                                              pxsys_surface_ref_t surface);
pxsys_status_t pxsys_renderer_surface_set_visible(pxsys_renderer_host_t* host,
                                                  pxsys_surface_ref_t surface,
                                                  int visible);
pxsys_status_t pxsys_renderer_apply(pxsys_renderer_host_t* host,
                                    pxsys_surface_ref_t surface,
                                    const pxsys_ui_transaction_t* transaction);

#ifdef __cplusplus
}
#endif

#endif

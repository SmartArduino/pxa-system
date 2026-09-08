#ifndef PXSYS_HEADLESS_RENDERER_H
#define PXSYS_HEADLESS_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_HEADLESS_RENDERER_ID "headless"

typedef struct {
    uint32_t struct_size;
    size_t max_surfaces;
    size_t max_transaction_bytes;
    pxsys_allocator_t allocator;
} pxsys_headless_renderer_config_t;

typedef struct {
    uint32_t struct_size;
    pxsys_surface_config_t config;
    uint64_t last_transaction_id;
    size_t command_bytes;
    int visible;
} pxsys_headless_surface_snapshot_t;

typedef struct pxsys_headless_renderer pxsys_headless_renderer_t;

void pxsys_headless_renderer_config_init(pxsys_headless_renderer_config_t* config);
pxsys_status_t pxsys_headless_renderer_create(const pxsys_headless_renderer_config_t* config,
                                              pxsys_headless_renderer_t** output);
pxsys_status_t pxsys_headless_renderer_destroy(pxsys_headless_renderer_t* renderer);
pxsys_status_t pxsys_headless_renderer_provider(pxsys_headless_renderer_t* renderer,
                                                pxsys_renderer_provider_t* provider);
pxsys_status_t pxsys_headless_renderer_snapshot(const pxsys_headless_renderer_t* renderer,
                                                pxsys_surface_ref_t surface,
                                                pxsys_headless_surface_snapshot_t* snapshot);

#ifdef __cplusplus
}
#endif

#endif

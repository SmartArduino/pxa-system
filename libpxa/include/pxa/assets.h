#ifndef PXA_ASSETS_H
#define PXA_ASSETS_H

#include "pxa/runtime.h"
#include "pxa/asset_cache.h"
#include "pxa/asset_read.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_ASSETS_SERVICE_ID UINT16_C(21)
#define PXA_ASSETS_SERVICE_MAJOR UINT16_C(2)
#define PXA_ASSETS_SERVICE_MINOR UINT16_C(1)
#define PXA_ASSETS_QUERY UINT16_C(1)
#define PXA_ASSETS_LOAD UINT16_C(2)
#define PXA_ASSETS_PREFETCH UINT16_C(3)
#define PXA_ASSETS_STATUS UINT16_C(4)
#define PXA_ASSETS_READ UINT16_C(5)
#define PXA_ASSETS_STATUS_BYTES 8u
#define PXA_ASSETS_INFO_BYTES 20u
#define PXA_ASSETS_LOAD_RESULT_BYTES 28u

/* Host callbacks are nonblocking metadata/cache operations. The backend maps
 * this runtime's component to its authenticated package and cache owner. It
 * must retain package/catalog storage through outstanding tickets and assets.
 * release drops a ticket, cancels orphaned work, and signals reclamation; final
 * pixel frees happen on its worker. No callback invokes Guest code. */
typedef struct {
    void *context;
    pxa_status_t (*find)(void *, pxa_component_t, pxa_bytes_t, pxa_asset_info_t *);
    pxa_status_t (*request)(void *, pxa_component_t, pxa_bytes_t, uint8_t,
                             pxa_asset_ticket_t *);
    pxa_status_t (*query)(void *, pxa_component_t, pxa_asset_ticket_t,
                           pxa_asset_request_state_t *);
    pxa_status_t (*acquire)(void *, pxa_component_t, pxa_asset_ticket_t,
                             pxa_raster_asset_t **);
    void (*release)(void *, pxa_component_t, pxa_asset_ticket_t);
    pxa_status_t (*prefetch)(void *, pxa_component_t, pxa_bytes_t, uint8_t,
                            pxa_asset_ticket_t *);
    pxa_status_t (*inspect)(void *, pxa_component_t, pxa_bytes_t,
                           pxa_asset_request_state_t *);
    pxa_status_t (*read)(void *, pxa_component_t, pxa_bytes_t,
                        uint32_t offset, uint32_t bytes, uint64_t *ticket);
    /* Nonblocking. OK borrows offset:u32|total:u32|data until read_release.
     * Core copies it before release on the serialized runtime thread. */
    pxa_status_t (*read_result)(void *, pxa_component_t, uint64_t, pxa_bytes_t *);
    void (*read_release)(void *, pxa_component_t, uint64_t);
} pxa_assets_backend_t;

typedef struct {
    uint32_t struct_size;
    uint16_t max_pending;
    uint16_t max_pending_per_component;
    uint16_t max_resources;
    uint16_t max_resources_per_component;
    pxa_assets_backend_t backend;
} pxa_assets_config_t;
typedef struct pxa_assets_service pxa_assets_service_t;

size_t pxa_assets_service_workspace_size(const pxa_assets_config_t *config);
pxa_status_t pxa_assets_service_init(void *workspace, size_t bytes,
    pxa_runtime_t *runtime, const pxa_assets_config_t *config,
    pxa_assets_service_t **output);
pxa_status_t pxa_assets_service_register(pxa_assets_service_t *service);
/* Run on the runtime thread after worker notification and after Core cancel.
 * Iterates a bounded request table, never waits for I/O. Resource close/stop
 * callbacks require this service and its backend to outlive the runtime. */
void pxa_assets_service_poll(pxa_assets_service_t *service);
/* Resolve once during binding/submit; caller owns the returned reference.
 * expected_kind is TEXTURE or PALETTE. Full generation and owner validated. */
pxa_status_t pxa_assets_acquire_handle(pxa_runtime_t *runtime,
    pxa_component_t component, pxa_handle64_t handle, uint8_t expected_kind,
    pxa_raster_asset_t **output);

#ifdef __cplusplus
}
#endif
#endif

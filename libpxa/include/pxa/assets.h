#ifndef PXA_ASSETS_H
#define PXA_ASSETS_H

#include "pxa/runtime.h"
#include "pxa/asset_cache.h"
#include "pxa/asset_read.h"
#include "pxa/image_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_ASSETS_SERVICE_ID UINT16_C(21)
#define PXA_ASSETS_SERVICE_MAJOR UINT16_C(2)
#define PXA_ASSETS_SERVICE_MINOR UINT16_C(6)
#define PXA_ASSETS_QUERY UINT16_C(1)
#define PXA_ASSETS_LOAD UINT16_C(2)
#define PXA_ASSETS_PREFETCH UINT16_C(3)
#define PXA_ASSETS_STATUS UINT16_C(4)
#define PXA_ASSETS_READ UINT16_C(5)
/* DECODE_IMAGE: source:u64|resource_type:u16|options:u16|max_width:u16|
 * max_height:u16|max_pixels:u32|max_encoded_bytes:u32 (24 bytes).
 * Source is an owned FILE or STREAM in this component. Host reads from its
 * current position without a Guest copy, yielding at WOULD_BLOCK. The caller
 * keeps it alive and exclusively readable until completion/cancel. Source is
 * never closed by this service. Returns LOAD result | source_width:u16 |
 * source_height:u16 | encoded_bytes:u32 (36 bytes). Core cancellation reclaims
 * the pending decoder; no separate decoder handle is published. The optional
 * image-frames extension below attaches state to the current image. */
#define PXA_ASSETS_FEATURE_IMAGE_DECODE UINT64_C(1)
#define PXA_ASSETS_FEATURE_IMAGE_FRAMES UINT64_C(2)
/* 2.6, also available as a backport feature: static decode accepts sides up to
 * 2048 and up to 1048576 pixels. Allocations still obey the Host/owner budget;
 * no storage is reserved at startup. Sequence frames remain <=65536 pixels.
 * Older Hosts reject the larger options; clients may retry with legacy limits. */
#define PXA_ASSETS_FEATURE_LARGE_IMAGES UINT64_C(4)
#define PXA_ASSETS_IMAGE_FORMATS_MASK UINT16_C(7)
#define PXA_ASSETS_IMAGE_WITH_FRAMES UINT16_C(256)
#define PXA_ASSETS_DECODE_IMAGE UINT16_C(6)
#define PXA_ASSETS_NEXT_IMAGE_FRAME UINT16_C(7)
#define PXA_ASSETS_IMAGE_RESULT_BYTES 36u
#define PXA_ASSETS_FRAME_RESULT_BYTES 48u
/* DECODE_IMAGE options:u16 replaces reserved; low 3 bits accept PNG/JPEG/GIF
 * (zero=any). WITH_FRAMES uses a 32-byte request, appending animation pixel
 * and encoded-byte ceilings, and returns image result | delay_ms:u32 |
 * iteration:u32 | flags:u32 (LAST=1). The image owns optional frame state.
 * NEXT_IMAGE_FRAME borrows that image:u64 and returns the same 48-byte result,
 * moving frame state to the new image. Old pixels remain immutable. Closing
 * either image releases only what it still owns; ordinary static images have
 * no frame state. One completion transfers exactly one image handle. */
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
    /* Optional, immutable through the last image reference. NULL disables
     * source-resource image decoding; no decoder/cache is allocated by init. */
    const pxa_memory_allocator_t *image_allocator;
    const pxa_memory_allocator_t *image_temporary_allocator;
    const pxa_image_backend_t *image_backend;
} pxa_assets_config_t;
typedef struct pxa_assets_service pxa_assets_service_t;

size_t pxa_assets_service_workspace_size(const pxa_assets_config_t *config);
pxa_status_t pxa_assets_service_init(void *workspace, size_t bytes,
    pxa_runtime_t *runtime, const pxa_assets_config_t *config,
    pxa_assets_service_t **output);
pxa_status_t pxa_assets_service_register(pxa_assets_service_t *service);
/* Run on the runtime thread after Guest callbacks, Core cancel and source I/O
 * notifications (including notifications without a completed Net request).
 * Nonzero requests another owner-loop turn: work yielded its slice or emitted
 * a completion. WOULD_BLOCK does not request a turn; wait for the source wake.
 * Never waits for I/O. Service/backend must outlive resource close/stop. */
int pxa_assets_service_poll(pxa_assets_service_t *service);
/* Resolve once during binding/submit; caller owns the returned reference.
 * expected_kind is TEXTURE, PALETTE, AUDIO or IMAGE. Owner/generation validated. */
pxa_status_t pxa_assets_acquire_handle(pxa_runtime_t *runtime,
    pxa_component_t component, pxa_handle64_t handle, uint8_t expected_kind,
    pxa_raster_asset_t **output);

#ifdef __cplusplus
}
#endif
#endif

#ifndef PXA_GAME_RENDER_H
#define PXA_GAME_RENDER_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/raster.h"
#include "pxa/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_GAME_RENDER_SERVICE_ID UINT16_C(18)
#define PXA_GAME_RENDER_SERVICE_MAJOR UINT16_C(0)
#define PXA_GAME_RENDER_SERVICE_MINOR UINT16_C(3)
#define PXA_GAME_RENDER_SERVICE_PATCH UINT16_C(0)

#define PXA_GAME_RENDER_CREATE_CONTEXT UINT16_C(1)
#define PXA_GAME_RENDER_CREATE_AUTO_CONTEXT UINT16_C(2)

#define PXA_GAME_RENDER_MAX_SCALE UINT8_C(4)
#define PXA_GAME_RENDER_SCALE_1X UINT8_C(1)
#define PXA_GAME_RENDER_SCALE_2X UINT8_C(2)
#define PXA_GAME_RENDER_SCALE_3X UINT8_C(3)
#define PXA_GAME_RENDER_SCALE_4X UINT8_C(4)
#define PXA_GAME_RENDER_SCALE_MASK(scale) \
    ((uint8_t)(UINT8_C(1) << ((uint8_t)(scale) - UINT8_C(1))))
#define PXA_GAME_RENDER_SCALE_MASK_1X \
    PXA_GAME_RENDER_SCALE_MASK(PXA_GAME_RENDER_SCALE_1X)
#define PXA_GAME_RENDER_SCALE_MASK_2X \
    PXA_GAME_RENDER_SCALE_MASK(PXA_GAME_RENDER_SCALE_2X)
#define PXA_GAME_RENDER_SCALE_MASK_3X \
    PXA_GAME_RENDER_SCALE_MASK(PXA_GAME_RENDER_SCALE_3X)
#define PXA_GAME_RENDER_SCALE_MASK_4X \
    PXA_GAME_RENDER_SCALE_MASK(PXA_GAME_RENDER_SCALE_4X)
#define PXA_GAME_RENDER_KNOWN_SCALE_MASK \
    (PXA_GAME_RENDER_SCALE_MASK_1X | PXA_GAME_RENDER_SCALE_MASK_2X | \
     PXA_GAME_RENDER_SCALE_MASK_3X | PXA_GAME_RENDER_SCALE_MASK_4X)

#define PXA_GAME_RENDER_FLAG_PREFER_DIRECT_SCANOUT UINT8_C(1)
#define PXA_GAME_RENDER_FLAG_KNOWN_MASK \
    PXA_GAME_RENDER_FLAG_PREFER_DIRECT_SCANOUT

#define PXA_GAME_RENDER_IO_UPLOAD UINT32_C(0x100)
#define PXA_GAME_RENDER_IO_SUBMIT UINT32_C(0x101)
#define PXA_GAME_RENDER_IO_TELEMETRY UINT32_C(0x102)
#define PXA_GAME_RENDER_TELEMETRY_BYTES ((size_t)104)

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t buffer_count;
    uint8_t flags;
} pxa_game_render_desc_t;

/* A target profile is supplied by the target board. The service uses it only
 * to negotiate and validate an integer render scale for the current display. */
typedef struct {
    uint16_t display_width;
    uint16_t display_height;
    uint8_t supported_scale_mask;
    uint8_t default_scale;
} pxa_game_render_target_profile_t;

/* A resolved target keeps panel dimensions, render resolution and input
 * coordinates in one contract. `render_scale` is an integer enlargement from
 * the render buffer to the display. */
typedef struct {
    uint16_t display_width;
    uint16_t display_height;
    uint16_t render_width;
    uint16_t render_height;
    uint8_t render_scale;
    uint8_t supported_scale_mask;
} pxa_game_render_target_t;

typedef pxa_status_t (*pxa_game_render_create_fn)(
    void *context, const pxa_game_render_desc_t *desc,
    uint64_t *provider_context, uint32_t *capabilities);
typedef pxa_status_t (*pxa_game_render_upload_fn)(
    void *context, uint64_t provider_context, const uint8_t *bytes,
    size_t size);
typedef pxa_status_t (*pxa_game_render_submit_fn)(
    void *context, uint64_t provider_context, const uint8_t *bytes,
    size_t size);
typedef pxa_status_t (*pxa_game_render_query_fn)(
    void *context, uint64_t provider_context,
    pxa_raster_telemetry_t *telemetry);
typedef void (*pxa_game_render_close_fn)(void *context,
                                         uint64_t provider_context);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_game_render_create_fn create;
    pxa_game_render_upload_fn upload;
    pxa_game_render_submit_fn submit;
    pxa_game_render_query_fn query;
    pxa_game_render_close_fn close;
} pxa_game_render_backend_t;

typedef struct {
    uint32_t struct_size;
    uint16_t max_contexts;
    uint16_t max_contexts_per_component;
    /* ABI compatibility fields. Context dimensions are no longer capped by
     * the service; backends reject only dimensions they cannot allocate. */
    uint16_t max_width;
    uint16_t max_height;
    uint8_t min_buffer_count;
    uint8_t max_buffer_count;
    uint8_t reserved[2];
    /* Optional board-owned profile for CREATE_AUTO_CONTEXT. A zero display
     * dimension disables the optional opcode without affecting legacy create. */
    pxa_game_render_target_profile_t auto_target_profile;
    pxa_game_render_backend_t backend;
} pxa_game_render_config_t;

typedef struct pxa_game_render_service pxa_game_render_service_t;

size_t pxa_game_render_service_workspace_size(
    const pxa_game_render_config_t *config);
pxa_status_t pxa_game_render_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_game_render_config_t *config,
    pxa_game_render_service_t **output);
pxa_status_t pxa_game_render_service_register(
    pxa_game_render_service_t *service);

#ifdef __cplusplus
}
#endif

#endif

#ifndef PXA_SURFACE_H
#define PXA_SURFACE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/raster.h"
#include "pxa/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_SURFACE_SERVICE_ID UINT16_C(16)
#define PXA_SURFACE_SERVICE_MAJOR UINT16_C(0)
#define PXA_SURFACE_SERVICE_MINOR UINT16_C(3)
#define PXA_SURFACE_SERVICE_PATCH UINT16_C(0)

#define PXA_SURFACE_CREATE UINT16_C(1)
#define PXA_SURFACE_CONFIGURE_LAYER UINT16_C(2)
#define PXA_SURFACE_QUEUE_FRAME UINT16_C(3)
#define PXA_SURFACE_QUERY_STATE UINT16_C(4)
#define PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS UINT16_C(5)
#define PXA_SURFACE_RELEASED UINT16_C(0x8001)

#define PXA_SURFACE_FORMAT_RGB565 UINT16_C(1)
#define PXA_SURFACE_FORMAT_ARGB8888_PREMULTIPLIED UINT16_C(2)
#define PXA_SURFACE_FLAG_PREMULTIPLIED_ALPHA UINT8_C(1)
/* Requests direct panel ownership when the Host can safely bypass its UI
 * compositor. The Host may still compose any frame when trusted UI is active. */
#define PXA_SURFACE_FLAG_PREFER_DIRECT_SCANOUT UINT8_C(2)
#define PXA_SURFACE_FLAG_GUEST_MAPPED UINT8_C(4)
#define PXA_SURFACE_FLAG_HOST_RASTER UINT8_C(8)
#define PXA_SURFACE_FLAG_KNOWN_MASK                                      \
    (PXA_SURFACE_FLAG_PREMULTIPLIED_ALPHA |                             \
     PXA_SURFACE_FLAG_PREFER_DIRECT_SCANOUT | PXA_SURFACE_FLAG_GUEST_MAPPED | \
     PXA_SURFACE_FLAG_HOST_RASTER)
#define PXA_SURFACE_MAX_DAMAGE_RECTS UINT8_C(8)
#define PXA_SURFACE_MAX_OPAQUE_UI_REGIONS UINT8_C(8)
#define PXA_SURFACE_STATE_FLAG_SUPPORTS_OPAQUE_UI_REGIONS UINT32_C(1)
#define PXA_SURFACE_STATE_FLAG_SUPPORTS_ALPHA_COMPOSITING UINT32_C(2)
#define PXA_SURFACE_STATE_FLAG_UI_ALPHA_PLANE_ACTIVE UINT32_C(4)
#define PXA_SURFACE_STATE_FLAG_SUPPORTS_GUEST_MAPPED UINT32_C(8)
#define PXA_SURFACE_STATE_FLAG_SUPPORTS_HOST_RASTER UINT32_C(16)
#define PXA_SURFACE_STATE_FLAG_RASTER_TEXTURED_QUAD UINT32_C(32)
#define PXA_SURFACE_STATE_FLAG_RASTER_ADDITIVE_SPRITE UINT32_C(64)

/* Surface-specific pxa_io operations. REGISTER retains the validated Guest
 * address for the Surface lifetime, so it is only supported by runtimes whose
 * linear-memory base is pinned. PXA_SURFACE_BUFFER_ALIGNMENT applies to the
 * Guest linear-memory offset; the translated native address only needs the
 * pixel format's natural alignment. ACQUIRE and PRESENT use the fixed records
 * below and never copy frame pixels. */
#define PXA_SURFACE_IO_REGISTER_BUFFERS UINT32_C(0x100)
#define PXA_SURFACE_IO_ACQUIRE UINT32_C(0x101)
#define PXA_SURFACE_IO_PRESENT UINT32_C(0x102)
#define PXA_SURFACE_IO_RASTER_UPLOAD UINT32_C(0x103)
#define PXA_SURFACE_IO_RASTER_SUBMIT UINT32_C(0x104)
#define PXA_SURFACE_IO_RASTER_TELEMETRY UINT32_C(0x105)
#define PXA_SURFACE_BUFFER_ALIGNMENT UINT32_C(64)
#define PXA_SURFACE_ACQUIRE_RECORD_BYTES ((size_t)4)
#define PXA_SURFACE_PRESENT_RECORD_BYTES ((size_t)16)
#define PXA_SURFACE_RELEASED_PAYLOAD_BYTES ((size_t)16)
#define PXA_SURFACE_RASTER_TELEMETRY_BYTES ((size_t)88)

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t format;
    uint8_t buffer_count;
    uint8_t flags;
} pxa_surface_desc_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint16_t width;
    uint16_t height;
    int16_t z;
    uint8_t visible;
    uint8_t reserved;
} pxa_surface_layer_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} pxa_surface_damage_rect_t;

typedef struct {
    uint64_t submitted_frames;
    uint64_t presented_frames;
    uint64_t dropped_frames;
    uint64_t replaced_frames;
    uint64_t released_frames;
    uint32_t free_buffers;
    uint32_t flags;
} pxa_surface_state_t;

typedef struct {
    uint8_t buffer_index;
    uint8_t reserved[3];
} pxa_surface_acquire_record_t;

typedef struct {
    uint8_t buffer_index;
    uint8_t reserved[7];
    uint64_t frame_id;
} pxa_surface_present_record_t;

typedef struct {
    uint8_t buffer_index;
    uint8_t reserved[7];
    uint64_t frame_id;
} pxa_surface_release_t;

typedef pxa_status_t (*pxa_surface_create_fn)(
    void *context, const pxa_surface_desc_t *desc,
    uint64_t *provider_surface, uint32_t *stride_bytes);
typedef pxa_status_t (*pxa_surface_write_fn)(
    void *context, uint64_t provider_surface, const uint8_t *pixels,
    size_t size);
typedef pxa_status_t (*pxa_surface_register_buffers_fn)(
    void *context, uint64_t provider_surface, uint8_t *pixels, size_t size);
typedef pxa_status_t (*pxa_surface_acquire_buffer_fn)(
    void *context, uint64_t provider_surface, uint8_t *buffer_index);
typedef pxa_status_t (*pxa_surface_present_buffer_fn)(
    void *context, uint64_t provider_surface, uint8_t buffer_index,
    uint64_t frame_id);
/* Peek is non-destructive. Consume is called only after the Runtime accepted
 * the reliable RELEASED event on its owner thread. */
typedef pxa_status_t (*pxa_surface_peek_release_fn)(
    void *context, uint64_t provider_surface, pxa_surface_release_t *release);
typedef void (*pxa_surface_consume_release_fn)(
    void *context, uint64_t provider_surface);
typedef pxa_status_t (*pxa_surface_raster_upload_fn)(
    void *context, uint64_t provider_surface, const uint8_t *bytes,
    size_t size);
typedef pxa_status_t (*pxa_surface_raster_submit_fn)(
    void *context, uint64_t provider_surface, const uint8_t *bytes,
    size_t size);
typedef pxa_status_t (*pxa_surface_raster_query_fn)(
    void *context, uint64_t provider_surface,
    pxa_raster_telemetry_t *telemetry);
typedef pxa_status_t (*pxa_surface_queue_fn)(
    void *context, uint64_t provider_surface, uint64_t frame_id,
    const pxa_surface_damage_rect_t *damage, uint8_t damage_count);
typedef pxa_status_t (*pxa_surface_configure_fn)(
    void *context, uint64_t provider_surface,
    const pxa_surface_layer_t *layer);
/* Regions are Surface-local and select the already-rendered LVGL pixels over
 * the opaque RGB565 Surface. Hosts without this optional profile leave this
 * callback NULL and report UNSUPPORTED for CONFIGURE_OPAQUE_UI_REGIONS. */
typedef pxa_status_t (*pxa_surface_configure_opaque_ui_regions_fn)(
    void *context, uint64_t provider_surface,
    const pxa_surface_damage_rect_t *regions, uint8_t region_count);
typedef pxa_status_t (*pxa_surface_query_fn)(
    void *context, uint64_t provider_surface, pxa_surface_state_t *state);
typedef void (*pxa_surface_close_fn)(void *context,
                                      uint64_t provider_surface);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_surface_create_fn create;
    pxa_surface_write_fn write;
    pxa_surface_queue_fn queue;
    pxa_surface_configure_fn configure;
    pxa_surface_configure_opaque_ui_regions_fn configure_opaque_ui_regions;
    pxa_surface_query_fn query;
    pxa_surface_close_fn close;
    pxa_surface_register_buffers_fn register_buffers;
    pxa_surface_acquire_buffer_fn acquire_buffer;
    pxa_surface_present_buffer_fn present_buffer;
    pxa_surface_peek_release_fn peek_release;
    pxa_surface_consume_release_fn consume_release;
    pxa_surface_raster_upload_fn raster_upload;
    pxa_surface_raster_submit_fn raster_submit;
    pxa_surface_raster_query_fn raster_query;
} pxa_surface_backend_t;

typedef struct {
    uint32_t struct_size;
    uint16_t max_surfaces;
    uint16_t max_surfaces_per_component;
    uint16_t max_width;
    uint16_t max_height;
    uint32_t max_frame_bytes;
    uint8_t min_buffer_count;
    uint8_t max_buffer_count;
    uint8_t reserved[2];
    pxa_surface_backend_t backend;
} pxa_surface_config_t;

typedef struct pxa_surface_service pxa_surface_service_t;

size_t pxa_surface_service_workspace_size(
    const pxa_surface_config_t *config);
pxa_status_t pxa_surface_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_surface_config_t *config, pxa_surface_service_t **output);
pxa_status_t pxa_surface_service_register(pxa_surface_service_t *service);
int pxa_surface_has_active_surfaces(
    const pxa_surface_service_t *service);
/* Call from the Runtime owner thread. Returns the number of RELEASED events
 * posted, or a negative status when posting the next reliable event failed. */
int32_t pxa_surface_service_flush_releases(pxa_surface_service_t *service);

#ifdef __cplusplus
}
#endif

#endif

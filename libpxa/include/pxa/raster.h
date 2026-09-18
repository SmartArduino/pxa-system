#ifndef PXA_RASTER_H
#define PXA_RASTER_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_RASTER_ABI_MAJOR UINT16_C(1)
#define PXA_RASTER_ABI_MINOR UINT16_C(2)
#define PXA_RASTER_DRAW_MAGIC UINT32_C(0x4c525850) /* PXRL */
#define PXA_RASTER_UPLOAD_MAGIC UINT32_C(0x52555850) /* PXUR */

#define PXA_RASTER_MAX_TEXTURES UINT8_C(16)
#define PXA_RASTER_PALETTE_COLORS UINT16_C(256)
#define PXA_RASTER_MAX_DRAW_BYTES UINT32_C(49152)
#define PXA_RASTER_MAX_COMMANDS UINT32_C(768)
#define PXA_RASTER_MAX_TEXTURE_DIMENSION UINT16_C(256)

#define PXA_RASTER_CAP_FLAT_QUAD UINT32_C(1)
#define PXA_RASTER_CAP_TEXTURED_QUAD UINT32_C(2)
#define PXA_RASTER_CAP_ADDITIVE_SPRITE UINT32_C(4)
#define PXA_RASTER_CAP_SPRITE_BATCH UINT32_C(8)
#define PXA_RASTER_CAP_TRIANGLE_BATCH UINT32_C(16)
/* Affine UV keeps the perspective-correct depth interpolant and only replaces
 * the per-texel perspective divide with a screen-linear UV. It is a bandwidth
 * optimisation for faces whose depth range is small. */
#define PXA_RASTER_CAP_AFFINE_UV UINT32_C(32)
#define PXA_RASTER_CAP_KNOWN_MASK                                      \
    (PXA_RASTER_CAP_FLAT_QUAD | PXA_RASTER_CAP_TEXTURED_QUAD |        \
     PXA_RASTER_CAP_ADDITIVE_SPRITE | PXA_RASTER_CAP_SPRITE_BATCH |   \
     PXA_RASTER_CAP_TRIANGLE_BATCH | PXA_RASTER_CAP_AFFINE_UV)

#define PXA_RASTER_UPLOAD_PALETTE_RGB565 UINT8_C(1)
#define PXA_RASTER_UPLOAD_TEXTURE_INDEX8 UINT8_C(2)
#define PXA_RASTER_UPLOAD_HEADER_BYTES UINT32_C(20)

#define PXA_RASTER_DRAW_HEADER_BYTES UINT32_C(32)
#define PXA_RASTER_RECORD_HEADER_BYTES UINT16_C(4)
#define PXA_RASTER_RECORD_CLEAR_RGB565 UINT8_C(1)
#define PXA_RASTER_RECORD_FLAT_QUAD UINT8_C(2)
#define PXA_RASTER_RECORD_TEXTURED_QUAD UINT8_C(3)
#define PXA_RASTER_RECORD_SPRITE UINT8_C(4)
#define PXA_RASTER_RECORD_SPRITE_BATCH UINT8_C(5)
#define PXA_RASTER_RECORD_TRIANGLE_BATCH UINT8_C(6)
#define PXA_RASTER_CLEAR_BYTES UINT16_C(8)
#define PXA_RASTER_FLAT_QUAD_BYTES UINT16_C(24)
#define PXA_RASTER_TEXTURED_QUAD_BYTES UINT16_C(56)
#define PXA_RASTER_SPRITE_BYTES UINT16_C(24)
#define PXA_RASTER_VERTEX_BYTES UINT16_C(12)
#define PXA_RASTER_SPRITE_BATCH_HEADER_BYTES UINT16_C(12)
#define PXA_RASTER_SPRITE_INSTANCE_BYTES UINT16_C(16)
#define PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES UINT16_C(12)

#define PXA_RASTER_QUAD_SOLID_COLOR UINT8_C(1)
/* Textured quads only. Requires PXA_RASTER_CAP_AFFINE_UV. Depth stays
 * perspective-correct so occlusion is unchanged. */
#define PXA_RASTER_QUAD_AFFINE_UV UINT8_C(2)

#define PXA_RASTER_SPRITE_TRANSPARENT_INDEX0 UINT8_C(1)
#define PXA_RASTER_SPRITE_SOLID_COLOR UINT8_C(2)
#define PXA_RASTER_SPRITE_ADDITIVE UINT8_C(4)

typedef struct {
    uint8_t kind;
    uint8_t slot;
    uint16_t width;
    uint16_t height;
    const uint8_t *payload;
    uint32_t payload_bytes;
} pxa_raster_upload_view_t;

typedef struct {
    const uint8_t *pixels;
    uint16_t width;
    uint16_t height;
} pxa_raster_texture_t;

typedef struct {
    pxa_raster_texture_t textures[PXA_RASTER_MAX_TEXTURES];
    const uint16_t *palette;
    uint32_t capabilities;
} pxa_raster_resources_t;

typedef struct {
    uint16_t *pixels;
    uint16_t *depth_pixels;
    uint32_t stride_pixels;
    uint32_t depth_stride_pixels;
    uint16_t width;
    uint16_t height;
} pxa_raster_target_t;

typedef struct {
    uint64_t submitted_frames;
    uint64_t draw_list_bytes;
    uint64_t covered_pixels;
    uint64_t host_raster_us;
    uint64_t queue_wait_us;
    uint64_t present_us;
    uint64_t dropped_frames;
    uint32_t clear_commands;
    uint32_t flat_quad_commands;
    uint32_t textured_quad_commands;
    uint32_t sprite_commands;
    uint32_t rejected_lists;
    uint32_t last_draw_list_bytes;
    uint32_t last_covered_pixels;
    uint32_t last_host_raster_us;
    uint64_t rendered_frames;
    uint64_t visible_frames;
} pxa_raster_telemetry_t;

typedef struct {
    uint16_t abi_minor;
    uint32_t total_size;
    uint32_t required_capabilities;
    uint32_t command_count;
    uint64_t frame_id;
} pxa_raster_draw_list_view_t;

pxa_status_t pxa_raster_decode_upload(const uint8_t *bytes, size_t size,
                                      pxa_raster_upload_view_t *output);
pxa_status_t pxa_raster_validate_draw_list(
    const uint8_t *bytes, size_t size, const pxa_raster_target_t *target,
    const pxa_raster_resources_t *resources,
    pxa_raster_draw_list_view_t *output);
void pxa_raster_execute_draw_list(const uint8_t *bytes,
                                  const pxa_raster_draw_list_view_t *list,
                                  const pxa_raster_target_t *target,
                                  const pxa_raster_resources_t *resources,
                                  pxa_raster_telemetry_t *telemetry);

#ifdef __cplusplus
}
#endif

#endif

#ifndef PXA_RASTER_H
#define PXA_RASTER_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_RASTER_ABI_MAJOR UINT16_C(1)
#define PXA_RASTER_ABI_MINOR UINT16_C(7)
#define PXA_RASTER_DRAW_MAGIC UINT32_C(0x4c525850) /* PXRL */
#define PXA_RASTER_UPLOAD_MAGIC UINT32_C(0x52555850) /* PXUR */

#define PXA_RASTER_MAX_TEXTURES UINT8_C(48)
#define PXA_RASTER_PALETTE_COLORS UINT16_C(256)
#define PXA_RASTER_MAX_DRAW_BYTES UINT32_C(49152)
#define PXA_RASTER_MAX_COMMANDS UINT32_C(1024)
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
/* The Host accepts texture slots up to PXA_RASTER_MAX_TEXTURES instead of the
 * original 16. Guests must only address slots >= 16 when this is advertised
 * so an older Host keeps working. */
#define PXA_RASTER_CAP_TEXTURE_SLOTS_48 UINT32_C(64)
#define PXA_RASTER_CAP_PAINTER_POLYGON UINT32_C(128)
#define PXA_RASTER_CAP_LIT_PALETTE_DEPTH UINT32_C(256)
#define PXA_RASTER_CAP_DEPTH_CUTOUT UINT32_C(512)
#define PXA_RASTER_CAP_FIXED_ALPHA_BLEND UINT32_C(1024)
#define PXA_RASTER_CAP_COVERAGE_MASK UINT32_C(2048)
/* Sprite paths: PALETTE_RAMP indexes the palette's full-light row (pre-blended
 * antialiasing for a known background) and TEXEL_ALPHA blends by the texel
 * (antialiasing over any background). */
#define PXA_RASTER_CAP_SPRITE_PALETTE_RAMP UINT32_C(4096)
#define PXA_RASTER_CAP_SPRITE_TEXEL_ALPHA UINT32_C(8192)
/* Painter polygons carry per-vertex depth and the Host interpolates u/z, v/z
 * and 1/z so a face keeps its texture straight at any angle. A Guest that
 * sees this stops subdividing faces to bound the affine error; a Host without
 * it ignores the depth and keeps the screen-linear mapping. */
#define PXA_RASTER_CAP_PAINTER_PERSPECTIVE UINT32_C(16384)
#define PXA_RASTER_CAP_PAINTER_DEPTH UINT32_C(32768)
#define PXA_RASTER_CAP_KNOWN_MASK                                      \
    (PXA_RASTER_CAP_FLAT_QUAD | PXA_RASTER_CAP_TEXTURED_QUAD |        \
     PXA_RASTER_CAP_ADDITIVE_SPRITE | PXA_RASTER_CAP_SPRITE_BATCH |   \
     PXA_RASTER_CAP_TRIANGLE_BATCH | PXA_RASTER_CAP_AFFINE_UV |       \
     PXA_RASTER_CAP_TEXTURE_SLOTS_48 | PXA_RASTER_CAP_PAINTER_POLYGON | \
     PXA_RASTER_CAP_LIT_PALETTE_DEPTH | PXA_RASTER_CAP_DEPTH_CUTOUT | \
     PXA_RASTER_CAP_FIXED_ALPHA_BLEND | PXA_RASTER_CAP_COVERAGE_MASK | \
     PXA_RASTER_CAP_SPRITE_PALETTE_RAMP | PXA_RASTER_CAP_SPRITE_TEXEL_ALPHA | \
     PXA_RASTER_CAP_PAINTER_PERSPECTIVE | PXA_RASTER_CAP_PAINTER_DEPTH)

#define PXA_RASTER_UPLOAD_PALETTE_RGB565 UINT8_C(1)
#define PXA_RASTER_UPLOAD_TEXTURE_INDEX8 UINT8_C(2)
#define PXA_RASTER_UPLOAD_LIT_PALETTE_RGB565 UINT8_C(3)
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
/* Convex painter polygon: affine scanlines, no depth test/write, and `light`
 * selects a row in the uploaded lit palette. For solid records, solid_color's
 * low byte is a palette index. */
#define PXA_RASTER_QUAD_PAINTER UINT8_C(4)
/* Textured polygons skip texel index 0. Painter polygons have always
 * supported this; depth-tested polygons require PXA_RASTER_CAP_DEPTH_CUTOUT
 * and skip both the color and depth writes. */
#define PXA_RASTER_QUAD_TRANSPARENT_INDEX0 UINT8_C(8)
/* Depth-tested textured polygon whose light value selects a row in the
 * uploaded lit palette. With PAINTER, selects the scanline depth kernel and
 * requires PXA_RASTER_CAP_PAINTER_DEPTH. */
#define PXA_RASTER_QUAD_LIT_PALETTE UINT8_C(16)
/* Fixed 75% RGB565 source-over blend. Textured polygons only; depth-tested
 * polygons test but do not update depth so back-to-front translucent faces
 * can accumulate without an alpha buffer. */
#define PXA_RASTER_QUAD_BLEND_75 UINT8_C(32)
/* Front-to-back painter coverage. Opaque/cutout texels claim one bit per
 * pixel. Blended quads mark a second bit-plane and are resolved later. */
#define PXA_RASTER_QUAD_COVERAGE_MASK UINT8_C(64)
#define PXA_RASTER_QUAD_COVERAGE_RESOLVE UINT8_C(128)

#define PXA_RASTER_SPRITE_TRANSPARENT_INDEX0 UINT8_C(1)
#define PXA_RASTER_SPRITE_SOLID_COLOR UINT8_C(2)
#define PXA_RASTER_SPRITE_ADDITIVE UINT8_C(4)
/* The texel indexes the palette's full-light row offset by `solid_color`, so a
 * coverage atlas can carry pre-blended ink/background ramps. */
#define PXA_RASTER_SPRITE_PALETTE_RAMP UINT8_C(8)
/* The texel is 0..255 coverage and the source colour is blended with the
 * destination per pixel. */
#define PXA_RASTER_SPRITE_TEXEL_ALPHA UINT8_C(16)

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
    uint16_t palette_light_levels;
    uint32_t capabilities;
} pxa_raster_resources_t;

typedef struct {
    uint16_t *pixels;
    uint16_t *depth_pixels;
    uint32_t stride_pixels;
    uint32_t depth_stride_pixels;
    uint16_t width;
    uint16_t height;
    /* Leading commands already materialized by a platform accelerator. */
    uint32_t prefilled_commands;
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
/* Executes only rows [row_begin, row_end). Multiple callers may execute
 * disjoint row ranges of the same validated list and target concurrently. */
void pxa_raster_execute_draw_list_rows(
    const uint8_t *bytes, const pxa_raster_draw_list_view_t *list,
    const pxa_raster_target_t *target,
    const pxa_raster_resources_t *resources, uint16_t row_begin,
    uint16_t row_end, pxa_raster_telemetry_t *telemetry);

#ifdef __cplusplus
}
#endif

#endif

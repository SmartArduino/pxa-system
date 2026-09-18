#ifndef VOXEL_CRAFT_RASTER_H
#define VOXEL_CRAFT_RASTER_H

#include <stdint.h>

#include "game.h"
#include "render.h"

typedef struct {
    uint32_t cached_quads;
    uint32_t candidate_quads;
    uint32_t submitted_quads;
    uint32_t backface_culled;
    uint32_t frustum_culled;
    uint32_t clipped_quads;
    uint32_t dropped_quads;
    uint32_t affine_quads;
    uint32_t draw_list_bytes;
    uint32_t covered_pixel_budget;
} voxel_raster_stats_t;

void voxel_raster_reset(void);
void voxel_raster_set_capabilities(uint32_t capabilities);
int voxel_raster_upload_assets(uint32_t surface_handle);
int32_t voxel_raster_render(uint32_t surface_handle, uint64_t frame_id,
                            const player_t *player, uint8_t quality,
                            const hud_state_t *hud);
void voxel_raster_get_stats(voxel_raster_stats_t *stats);

#endif

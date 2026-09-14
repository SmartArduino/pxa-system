#include "voxel_raster.h"

#include <assert.h>
#include <string.h>

#include "pxa_raster.h"

chunk_t *g_chunk_grid[GRID_W][GRID_W];
int16_t g_chunk_origin_cx;
int16_t g_chunk_origin_cz;
uint32_t g_world_seed;
particle_t g_particles[MAX_PARTICLES];
mob_t g_mobs[MAX_MOBS];
static chunk_t chunks[GRID_COUNT];
static uint32_t submitted_bytes;
static uint32_t submitted_commands;

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    (void)data;
    (void)length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    assert(handle == 3 && data != NULL);
    if (operation == PXA_SURFACE_IO_RASTER_SUBMIT) {
        assert(pxa_read_u32(data) == PXA_RASTER_DRAW_MAGIC);
        submitted_bytes = length;
        submitted_commands = pxa_read_u32(data + 16);
    }
    return (int32_t)length;
}

int render_scene_width(void) { return 148; }
int render_scene_height(void) { return 120; }
uint16_t render_block_color(int block) {
    return block == BLOCK_STONE ? UINT16_C(0x8410) : UINT16_C(0xffff);
}

int main(void) {
    player_t player = {8.0F, 6.0F, -2.0F, 0, 0, 0, 0, 0, 0, 0, 0};
    hud_state_t hud;
    voxel_raster_stats_t stats;
    uint32_t world_candidates;
    int grid_z;
    memset(chunks, 0, sizeof(chunks));
    memset(&hud, 0, sizeof(hud));
    g_chunk_origin_cx = -3;
    g_chunk_origin_cz = -3;
    for (grid_z = 0; grid_z < GRID_W; ++grid_z) {
        int grid_x;
        for (grid_x = 0; grid_x < GRID_W; ++grid_x) {
            chunk_t *chunk = &chunks[grid_z * GRID_W + grid_x];
            chunk->cx = (int16_t)(g_chunk_origin_cx + grid_x);
            chunk->cz = (int16_t)(g_chunk_origin_cz + grid_z);
            g_chunk_grid[grid_z][grid_x] = chunk;
        }
    }
    chunks[3 * GRID_W + 3].loaded = 1;
    chunks[3 * GRID_W + 3].revision = 1;
    for (int y = 0; y < 4; ++y)
        for (int z = 0; z < CHUNK_SIZE; ++z)
            for (int x = 0; x < CHUNK_SIZE; ++x)
                chunks[3 * GRID_W + 3]
                    .blocks[(y << 8) | (z << CHUNK_BITS) | x] = BLOCK_STONE;

    voxel_raster_reset();
    voxel_raster_set_capabilities(
        PXA_SURFACE_STATE_FLAG_SUPPORTS_HOST_RASTER |
        PXA_SURFACE_STATE_FLAG_RASTER_TEXTURED_QUAD);
    assert(voxel_raster_upload_assets(3));
    assert(voxel_raster_render(3, 1, &player, QUALITY_BALANCED, &hud) > 0);
    voxel_raster_get_stats(&stats);
    world_candidates = stats.candidate_quads;
    assert(stats.cached_quads <= 6);
    assert(stats.candidate_quads != 0 && stats.submitted_quads != 0);
    assert(stats.submitted_quads < CHUNK_SIZE * CHUNK_SIZE);
    assert(submitted_commands > stats.submitted_quads &&
           submitted_commands <= stats.submitted_quads + 32);
    assert(submitted_bytes == stats.draw_list_bytes &&
           submitted_bytes < 4096);
    g_mobs[0].x = 8.0F;
    g_mobs[0].y = 6.0F;
    g_mobs[0].z = 3.0F;
    g_mobs[0].alive = 1;
    g_mobs[0].kind = MOB_SLIME;
    assert(voxel_raster_render(3, 2, &player, QUALITY_BALANCED, &hud) > 0);
    voxel_raster_get_stats(&stats);
    assert(stats.candidate_quads == world_candidates + 1u);
    return 0;
}

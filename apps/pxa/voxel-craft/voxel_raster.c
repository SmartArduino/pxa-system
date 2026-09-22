#include "voxel_raster.h"

#include <stddef.h>
#include <stdint.h>

#include "block_textures.h"
#include "pxa_raster.h"
#include "rc_math.h"
#include "voxel_sky.h"

#define VOXEL_MESH_QUADS_PER_CHUNK 512u
#define VOXEL_MESH_MAX_SPAN 16
#define VOXEL_MESH_SLICES_PER_STEP 4u
#define VOXEL_RASTER_CANDIDATES 640u
#define VOXEL_RASTER_TEXTURED_BLOCKS 15u
#define VOXEL_RASTER_FONT_SLOT 15u
#define VOXEL_RASTER_FONT_SLOT_EXTENDED 45u
#define VOXEL_RASTER_FONT_GLYPHS 42u
#define VOXEL_RASTER_FONT_WIDTH (VOXEL_RASTER_FONT_GLYPHS * 4u)
#define VOXEL_RASTER_NEAR 0.08F
#define VOXEL_RASTER_TAN_HALF 0.70F
#define VOXEL_RASTER_PARTICLE_LIMIT 32u
/* Enough headroom that the visible set rarely hits the terrain candidate
 * limit: a saturated limit drops the farthest faces, which pop in and out as
 * the camera turns. */
#define VOXEL_RASTER_PERFORMANCE_CANDIDATES 320u
#define VOXEL_RASTER_PERFORMANCE_FOG 26.0F
#define VOXEL_RASTER_HYBRID_DEPTH_Q8 (8u * 256u)
#define VOXEL_RASTER_PAINTER_SPLIT_DEPTH_Q8 (2u * 256u)
#define VOXEL_RASTER_PAINTER_SPLIT_SCREEN_Q4 (8 * 16)
#define VOXEL_RASTER_PAINTER_SPLIT_MAX_PARTS 4u
#define VOXEL_RASTER_UNDERWATER_TINT UINT16_C(0x008a)
#define VOXEL_RASTER_UNDERWATER_CLEAR UINT16_C(0x118d)

#define HUD_PANEL UINT16_C(0x2945)
#define HUD_PANEL_LIGHT UINT16_C(0x5aeb)
#define HUD_SHADOW UINT16_C(0x18c3)
#define HUD_TEXT UINT16_C(0xffff)
#define HUD_SELECT UINT16_C(0xfec8)
#define HUD_GREEN UINT16_C(0x5eea)
/* RGB565 of the menu background 0x142c50 used by the mapped renderer. */
#define MENU_BG UINT16_C(0x116a)
#define HUD_MUTED UINT16_C(0x94b2)
/* Round and rectangular UI buttons share this slate face; edges and the top
 * highlight are derived from it so one colour styles the whole control set. */
#define HUD_BTN_FACE UINT16_C(0x322c)
#define HUD_BTN_ACTIVE UINT16_C(0x4cac)

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t z;
    uint8_t u_length;
    uint8_t v_length;
    uint8_t axis;
    int8_t sign;
    uint8_t block;
} mesh_quad_t;

typedef struct {
    const chunk_t *chunk;
    const chunk_t *build_chunk;
    uint32_t revision;
    uint32_t build_revision;
    int16_t chunk_cx;
    int16_t chunk_cz;
    int16_t build_chunk_cx;
    int16_t build_chunk_cz;
    uint16_t quad_count;
    uint16_t dropped;
    uint16_t build_quad_count;
    uint16_t build_dropped;
    uint8_t build_pass;
    uint8_t build_slice;
    uint8_t building;
    mesh_quad_t quads[VOXEL_MESH_QUADS_PER_CHUNK];
} chunk_mesh_t;

typedef struct {
    pxa_raster_vertex_t vertices[4];
    uint16_t nearest_depth_q8;
    uint16_t farthest_depth_q8;
    uint16_t sort_depth_q8;
    uint16_t color;
    uint8_t texture_slot;
    uint8_t textured;
    uint8_t affine;
    uint8_t transparent_index0;
    uint8_t blend_75;
} projected_quad_t;

typedef struct {
    float fx;
    float fy;
    float fz;
    float rx;
    float rz;
    float ux;
    float uy;
    float uz;
    float x;
    float y;
    float z;
    float tan_x;
    float tan_y;
    uint16_t width;
    uint16_t height;
    float fog_end;
} raster_camera_t;

typedef struct {
    const chunk_t *chunk;
    chunk_mesh_t *mesh;
    float depth;
} visible_chunk_t;

static chunk_mesh_t g_meshes[GRID_COUNT];
static chunk_mesh_t *g_building_mesh;
static mesh_quad_t g_mesh_build_quads[VOXEL_MESH_QUADS_PER_CHUNK];
static projected_quad_t g_candidates[VOXEL_RASTER_CANDIDATES];
static uint16_t g_sort_order[VOXEL_RASTER_CANDIDATES];
static uint8_t g_draw_list[PXA_RASTER_MAX_DRAW_BYTES];
static uint8_t g_upload[PXA_RASTER_UPLOAD_HEADER_BYTES +
                        VOXEL_RASTER_FONT_WIDTH * 5u];
static uint8_t g_font_texture[VOXEL_RASTER_FONT_WIDTH * 5u];
/* Painter polygons select one pre-lit palette row per pixel. */
#define VOXEL_RASTER_LIGHT_LEVELS 16u
static uint16_t
    g_lit_palette[VOXEL_RASTER_LIGHT_LEVELS * PXA_RASTER_PALETTE_COLORS];
static uint8_t g_palette_upload[PXA_RASTER_UPLOAD_HEADER_BYTES +
                                VOXEL_RASTER_LIGHT_LEVELS *
                                    PXA_RASTER_PALETTE_COLORS * 2u];
static voxel_raster_stats_t g_stats;
static uint32_t g_raster_capabilities;
static uint8_t g_mesh_cache_warmed;
static int16_t g_water_uv_offset_q4;

static const char kFontCharacters[] =
    "0123456789.:-/+ABCDEFGHIJKLMNOPQRSTUVWXYZ ";
static const uint8_t kFontRows[VOXEL_RASTER_FONT_GLYPHS][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7},
    {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7},
    {7, 5, 7, 1, 7}, {0, 0, 0, 0, 2}, {0, 2, 0, 2, 0},
    {0, 0, 7, 0, 0}, {1, 1, 2, 4, 4}, {0, 2, 7, 2, 0},
    {7, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {7, 4, 4, 4, 7},
    {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4},
    {7, 4, 5, 5, 7}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 7},
    {1, 1, 1, 5, 7}, {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7},
    {5, 7, 7, 5, 5}, {5, 7, 7, 7, 5}, {7, 5, 5, 5, 7},
    {7, 5, 7, 4, 4}, {7, 5, 5, 7, 1}, {6, 5, 6, 5, 5},
    {7, 4, 7, 1, 7}, {7, 2, 2, 2, 2}, {5, 5, 5, 5, 7},
    {5, 5, 5, 5, 2}, {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5},
    {5, 5, 2, 2, 2}, {7, 1, 2, 4, 7}, {0, 0, 0, 0, 0},
};

static int block_uses_cutout(int block) {
    return block == BLOCK_LEAVES;
}

static int block_is_translucent(int block) {
    return block == BLOCK_WATER;
}

static int block_is_transparent(int block) {
    return block_uses_cutout(block) || block_is_translucent(block);
}

static int block_face_visible(int block, int neighbor) {
    const int transparent = block_is_transparent(block);
    const int neighbor_transparent = block_is_transparent(neighbor);
    if (block == BLOCK_AIR) return 0;
    if (neighbor == BLOCK_AIR) return 1;
    if (!transparent) return neighbor_transparent;
    if (!neighbor_transparent || block == neighbor) return 0;
    /* Emit only one side of a boundary between different transparent
     * materials. */
    return block < neighbor;
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((uint16_t)(red >> 3) << 11) |
                      ((uint16_t)(green >> 2) << 5) | (blue >> 3));
}

/* Extended slots give every block its own top/side/bottom 16x16 tile. Older
 * Hosts only have 16 slots, so they fall back to one side tile per block. */
static uint8_t texture_slot_for(uint8_t block, uint8_t kind) {
    if ((g_raster_capabilities & PXA_RASTER_CAP_TEXTURE_SLOTS_48) != 0)
        return (uint8_t)((block - 1u) * 3u + kind);
    return (uint8_t)(block - 1u);
}

static uint8_t font_slot(void) {
    return (g_raster_capabilities & PXA_RASTER_CAP_TEXTURE_SLOTS_48) != 0
               ? VOXEL_RASTER_FONT_SLOT_EXTENDED
               : VOXEL_RASTER_FONT_SLOT;
}

void voxel_raster_set_capabilities(uint32_t capabilities) {
    g_raster_capabilities = capabilities;
}

/* Same rounding as the Host's light_rgb565(), so the pre-lit rows match the
 * per-pixel shading of the depth path. */
static uint16_t shade_rgb565(uint16_t color, uint32_t intensity) {
    uint32_t red = (color >> 11) * intensity + 127u;
    uint32_t green = ((color >> 5) & 63u) * intensity + 127u;
    uint32_t blue = (color & 31u) * intensity + 127u;
    red = (red + 1u + ((red + 1u) >> 8)) >> 8;
    green = (green + 1u + ((green + 1u) >> 8)) >> 8;
    blue = (blue + 1u + ((blue + 1u) >> 8)) >> 8;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static void build_lit_palette(void) {
    const uint16_t *base = block_texture_palette();
    uint32_t level;
    uint32_t index;
    for (level = 0; level < VOXEL_RASTER_LIGHT_LEVELS; ++level) {
        /* Row 0 stays the full-bright palette: textured sprites (item icons)
         * sample palette[texel] and have no light row of their own, while
         * painter polygons select darker rows through light_row(). */
        const uint32_t intensity =
            (VOXEL_RASTER_LIGHT_LEVELS - 1u - level) * 255u /
            (VOXEL_RASTER_LIGHT_LEVELS - 1u);
        uint16_t *row =
            &g_lit_palette[level * PXA_RASTER_PALETTE_COLORS];
        for (index = 0; index < PXA_RASTER_PALETTE_COLORS; ++index)
            row[index] = shade_rgb565(base[index], intensity);
    }
}

/* Maps a 0..255 face light to a pre-lit palette row index (bright faces use
 * row 0 so sprite sampling keeps the unlit palette). */
static uint8_t light_row(uint8_t light) {
    const uint32_t level =
        ((uint32_t)light * (VOXEL_RASTER_LIGHT_LEVELS - 1u) + 127u) / 255u;
    return (uint8_t)(VOXEL_RASTER_LIGHT_LEVELS - 1u - level);
}

int voxel_raster_upload_assets(uint32_t surface_handle) {
    const block_index_set_t *indices = block_texture_indices();
    const uint16_t *palette = block_texture_palette();
    const int extended =
        (g_raster_capabilities & PXA_RASTER_CAP_TEXTURE_SLOTS_48) != 0;
    uint8_t block;
    int32_t result;
    if ((g_raster_capabilities & PXA_RASTER_CAP_LIT_PALETTE_DEPTH) != 0) {
        build_lit_palette();
        result = pxa_raster_upload_lit_palette_rgb565(
            surface_handle, VOXEL_RASTER_LIGHT_LEVELS, g_lit_palette,
            g_palette_upload, sizeof(g_palette_upload));
        if (result !=
            (int32_t)(PXA_RASTER_UPLOAD_HEADER_BYTES +
                      VOXEL_RASTER_LIGHT_LEVELS *
                          PXA_RASTER_PALETTE_COLORS * 2u))
            return 0;
    } else {
        result = pxa_raster_upload_palette_rgb565(
            surface_handle, palette, g_upload, sizeof(g_upload));
        if (result != (int32_t)(PXA_RASTER_UPLOAD_HEADER_BYTES + 512u))
            return 0;
    }
    for (block = 0; block < VOXEL_RASTER_TEXTURED_BLOCKS; ++block) {
        const uint8_t kinds = extended ? 3u : 1u;
        uint8_t kind;
        for (kind = 0; kind < kinds; ++kind) {
            const uint8_t texture_kind =
                extended ? kind : BLOCK_TEXTURE_SIDE;
            const uint8_t slot =
                texture_slot_for((uint8_t)(block + 1u), texture_kind);
            result = pxa_raster_upload_texture_index8(
                surface_handle, slot, 16, 16, indices[block + 1][texture_kind],
                g_upload, sizeof(g_upload));
            if (result != (int32_t)(PXA_RASTER_UPLOAD_HEADER_BYTES + 256u))
                return 0;
        }
    }
    pxa_raster_zero_bytes(g_font_texture, sizeof(g_font_texture));
    for (block = 0; block < VOXEL_RASTER_FONT_GLYPHS; ++block) {
        uint8_t row;
        for (row = 0; row < 5; ++row) {
            uint8_t column;
            for (column = 0; column < 3; ++column) {
                if ((kFontRows[block][row] & (4u >> column)) != 0)
                    g_font_texture[(size_t)row * VOXEL_RASTER_FONT_WIDTH +
                                   (size_t)block * 4u + column] = 255;
            }
        }
    }
    result = pxa_raster_upload_texture_index8(
        surface_handle, font_slot(), VOXEL_RASTER_FONT_WIDTH, 5,
        g_font_texture, g_upload, sizeof(g_upload));
    if (result != (int32_t)sizeof(g_upload)) return 0;
    return 1;
}

static int local_block(const chunk_t *chunk, int x, int y, int z) {
    if ((unsigned)x >= CHUNK_SIZE || (unsigned)y >= CHUNK_HEIGHT ||
        (unsigned)z >= CHUNK_SIZE)
        return BLOCK_AIR;
    return chunk->blocks[(y << 8) | (z << CHUNK_BITS) | x];
}

static int neighbor_block(const chunk_t *chunk, int x, int y, int z) {
    if ((unsigned)x < CHUNK_SIZE && (unsigned)y < CHUNK_HEIGHT &&
        (unsigned)z < CHUNK_SIZE)
        return local_block(chunk, x, y, z);
    return game_block_fast((int)chunk->cx * CHUNK_SIZE + x, y,
                           (int)chunk->cz * CHUNK_SIZE + z);
}

static void append_mesh_quad(chunk_mesh_t *mesh, int axis, int sign,
                             int slice, int u, int v, int u_length,
                             int v_length, int block) {
    mesh_quad_t *quad;
    if (mesh->build_quad_count >= VOXEL_MESH_QUADS_PER_CHUNK) {
        ++mesh->build_dropped;
        return;
    }
    quad = &g_mesh_build_quads[mesh->build_quad_count++];
    pxa_raster_zero_bytes(quad, sizeof(*quad));
    quad->axis = (uint8_t)axis;
    quad->sign = (int8_t)sign;
    quad->block = (uint8_t)block;
    quad->u_length = (uint8_t)u_length;
    quad->v_length = (uint8_t)v_length;
    if (axis == 0) {
        quad->x = (uint8_t)(slice + (sign > 0));
        quad->y = (uint8_t)v;
        quad->z = (uint8_t)u;
    } else if (axis == 1) {
        quad->x = (uint8_t)u;
        quad->y = (uint8_t)(slice + (sign > 0));
        quad->z = (uint8_t)v;
    } else {
        quad->x = (uint8_t)u;
        quad->y = (uint8_t)v;
        quad->z = (uint8_t)(slice + (sign > 0));
    }
}

static void append_mesh_rect(chunk_mesh_t *mesh, int axis, int sign,
                             int slice, int u, int v, int u_length,
                             int v_length, int block) {
    int v_offset;
    /* Bound UV ranges and clipping expansion while retaining most of greedy
     * meshing's command-count reduction. */
    for (v_offset = 0; v_offset < v_length; v_offset += VOXEL_MESH_MAX_SPAN) {
        int u_offset;
        const int rect_height = v_length - v_offset > VOXEL_MESH_MAX_SPAN
                                    ? VOXEL_MESH_MAX_SPAN
                                    : v_length - v_offset;
        for (u_offset = 0; u_offset < u_length;
             u_offset += VOXEL_MESH_MAX_SPAN) {
            const int rect_width = u_length - u_offset > VOXEL_MESH_MAX_SPAN
                                       ? VOXEL_MESH_MAX_SPAN
                                       : u_length - u_offset;
            append_mesh_quad(mesh, axis, sign, slice, u + u_offset,
                             v + v_offset, rect_width, rect_height, block);
        }
    }
}

static void build_axis_face_slices(chunk_mesh_t *mesh, const chunk_t *chunk,
                                   int axis, int sign, int first_slice,
                                   int last_slice) {
    uint8_t mask[CHUNK_SIZE * CHUNK_HEIGHT];
    const int axis_length = axis == 1 ? CHUNK_HEIGHT : CHUNK_SIZE;
    const int u_length = CHUNK_SIZE;
    const int v_length = axis == 1 ? CHUNK_SIZE : CHUNK_HEIGHT;
    int slice;
    if (last_slice > axis_length) last_slice = axis_length;
    for (slice = first_slice; slice < last_slice; ++slice) {
        int v;
        pxa_raster_zero_bytes(mask, sizeof(mask));
        for (v = 0; v < v_length; ++v) {
            int u;
            for (u = 0; u < u_length; ++u) {
                int x = axis == 0 ? slice : u;
                int y = axis == 1 ? slice : v;
                int z = axis == 2 ? slice : (axis == 1 ? v : u);
                const int block = local_block(chunk, x, y, z);
                if (axis == 0) x += sign;
                if (axis == 1) y += sign;
                if (axis == 2) z += sign;
                if (block_face_visible(
                        block, neighbor_block(chunk, x, y, z)))
                    mask[v * u_length + u] = (uint8_t)block;
            }
        }
        for (v = 0; v < v_length; ++v) {
            int u = 0;
            while (u < u_length) {
                const uint8_t block = mask[v * u_length + u];
                int width = 1;
                int height = 1;
                int row;
                int column;
                if (block == 0) {
                    ++u;
                    continue;
                }
                while (u + width < u_length &&
                       mask[v * u_length + u + width] == block)
                    ++width;
                while (v + height < v_length) {
                    for (column = 0; column < width; ++column) {
                        if (mask[(v + height) * u_length + u + column] != block)
                            break;
                    }
                    if (column != width) break;
                    ++height;
                }
                append_mesh_rect(mesh, axis, sign, slice, u, v, width,
                                 height, block);
                for (row = 0; row < height; ++row)
                    for (column = 0; column < width; ++column)
                        mask[(v + row) * u_length + u + column] = 0;
                u += width;
            }
        }
    }
}

static void begin_mesh_rebuild(chunk_mesh_t *mesh, const chunk_t *chunk) {
    if (g_building_mesh != NULL && g_building_mesh != mesh)
        g_building_mesh->building = 0;
    g_building_mesh = mesh;
    mesh->build_chunk = chunk;
    mesh->build_revision = chunk->revision;
    mesh->build_chunk_cx = chunk->cx;
    mesh->build_chunk_cz = chunk->cz;
    mesh->build_quad_count = 0;
    mesh->build_dropped = 0;
    mesh->build_pass = 0;
    mesh->build_slice = 0;
    mesh->building = 1;
}

static int advance_mesh_rebuild(chunk_mesh_t *mesh, const chunk_t *chunk) {
    int axis;
    int axis_length;
    int sign;
    if (g_building_mesh != mesh || !mesh->building ||
        mesh->build_chunk != chunk ||
        mesh->build_revision != chunk->revision ||
        mesh->build_chunk_cx != chunk->cx ||
        mesh->build_chunk_cz != chunk->cz) {
        begin_mesh_rebuild(mesh, chunk);
    }
    axis = mesh->build_pass >> 1;
    sign = (mesh->build_pass & 1u) != 0 ? 1 : -1;
    axis_length = axis == 1 ? CHUNK_HEIGHT : CHUNK_SIZE;
    build_axis_face_slices(
        mesh, chunk, axis, sign, mesh->build_slice,
        mesh->build_slice + VOXEL_MESH_SLICES_PER_STEP);
    mesh->build_slice = (uint8_t)(
        mesh->build_slice + VOXEL_MESH_SLICES_PER_STEP);
    if (mesh->build_slice < axis_length) return 0;
    mesh->build_slice = 0;
    if (++mesh->build_pass < 6u) return 0;
    mesh->chunk = mesh->build_chunk;
    mesh->revision = mesh->build_revision;
    mesh->chunk_cx = mesh->build_chunk_cx;
    mesh->chunk_cz = mesh->build_chunk_cz;
    mesh->quad_count = mesh->build_quad_count;
    mesh->dropped = mesh->build_dropped;
    for (uint16_t index = 0; index < mesh->quad_count; ++index)
        mesh->quads[index] = g_mesh_build_quads[index];
    mesh->building = 0;
    g_building_mesh = NULL;
    return 1;
}

static void rebuild_mesh(chunk_mesh_t *mesh, const chunk_t *chunk) {
    begin_mesh_rebuild(mesh, chunk);
    while (!advance_mesh_rebuild(mesh, chunk)) {
    }
}

static chunk_mesh_t *mesh_for_chunk(const chunk_t *chunk,
                                    uint8_t preferred_index) {
    chunk_mesh_t *empty = NULL;
    uint8_t index;
    for (index = 0; index < GRID_COUNT; ++index) {
        if (g_meshes[index].chunk == chunk ||
            (g_meshes[index].building &&
             g_meshes[index].build_chunk == chunk))
            return &g_meshes[index];
        if (empty == NULL && g_meshes[index].chunk == NULL)
            empty = &g_meshes[index];
    }
    /* The pool and cache have the same size, so this is only reachable for
     * synthetic callers that replace every resident chunk pointer at once. */
    return empty != NULL ? empty : &g_meshes[preferred_index];
}

void voxel_raster_reset(void) {
    pxa_raster_zero_bytes(g_meshes, sizeof(g_meshes));
    pxa_raster_zero_bytes(&g_stats, sizeof(g_stats));
    g_building_mesh = NULL;
    g_mesh_cache_warmed = 0;
}

static void build_camera(raster_camera_t *camera, const player_t *player,
                         uint8_t quality) {
    const float cy = rc_cos(player->yaw);
    const float sy = rc_sin(player->yaw);
    const float cp = rc_cos(player->pitch);
    const float sp = rc_sin(player->pitch);
    camera->fx = sy * cp;
    camera->fy = sp;
    camera->fz = cy * cp;
    camera->rx = cy;
    camera->rz = -sy;
    camera->ux = -sy * sp;
    camera->uy = cp;
    camera->uz = -cy * sp;
    camera->x = player->x;
    camera->y = player->y + EYE_HEIGHT;
    camera->z = player->z;
    camera->width = (uint16_t)render_scene_width();
    camera->height = (uint16_t)render_scene_height();
    camera->tan_y = VOXEL_RASTER_TAN_HALF;
    camera->tan_x = VOXEL_RASTER_TAN_HALF *
                    (float)camera->width / camera->height;
    camera->fog_end = quality >= QUALITY_PERFORMANCE
                          ? VOXEL_RASTER_PERFORMANCE_FOG
                      : quality >= QUALITY_BALANCED  ? 38.0F
                                                     : 46.0F;
    /* Submerged: pull the fog in so terrain fades like murky water. */
    if (game_block(rc_floor_int(player->x),
                   rc_floor_int(player->y + EYE_HEIGHT),
                   rc_floor_int(player->z)) == BLOCK_WATER)
        camera->fog_end = 14.0F;
}

static int chunk_visible(const raster_camera_t *camera, const chunk_t *chunk) {
    const float center_x = (float)chunk->cx * CHUNK_SIZE + CHUNK_SIZE * 0.5F;
    const float center_y = CHUNK_HEIGHT * 0.5F;
    const float center_z = (float)chunk->cz * CHUNK_SIZE + CHUNK_SIZE * 0.5F;
    const float dx = center_x - camera->x;
    const float dy = center_y - camera->y;
    const float dz = center_z - camera->z;
    const float depth = dx * camera->fx + dy * camera->fy + dz * camera->fz;
    const float right = dx * camera->rx + dz * camera->rz;
    const float up = dx * camera->ux + dy * camera->uy + dz * camera->uz;
    const float radius = 17.0F;
    return depth + radius > VOXEL_RASTER_NEAR &&
           depth - radius < camera->fog_end &&
           rc_fabs(right) <= depth * camera->tan_x + radius &&
           rc_fabs(up) <= depth * camera->tan_y + radius;
}

static float chunk_depth(const raster_camera_t *camera, const chunk_t *chunk) {
    const float center_x = (float)chunk->cx * CHUNK_SIZE + CHUNK_SIZE * 0.5F;
    const float center_y = CHUNK_HEIGHT * 0.5F;
    const float center_z = (float)chunk->cz * CHUNK_SIZE + CHUNK_SIZE * 0.5F;
    return (center_x - camera->x) * camera->fx +
           (center_y - camera->y) * camera->fy +
           (center_z - camera->z) * camera->fz;
}

static void sort_visible_chunks(visible_chunk_t *chunks, uint8_t count) {
    uint8_t index;
    /* Near chunks provide the first-person silhouettes. Prioritising them
     * before the draw-list budget is consumed avoids traversal-order holes. */
    for (index = 1; index < count; ++index) {
        visible_chunk_t value = chunks[index];
        uint8_t cursor = index;
        while (cursor != 0 && chunks[cursor - 1u].depth > value.depth) {
            chunks[cursor] = chunks[cursor - 1u];
            --cursor;
        }
        chunks[cursor] = value;
    }
}

static void quad_corners(const chunk_t *chunk, const mesh_quad_t *quad,
                         float corners[4][3]) {
    const float bx = (float)chunk->cx * CHUNK_SIZE + quad->x;
    const float by = quad->y;
    const float bz = (float)chunk->cz * CHUNK_SIZE + quad->z;
    const float du = quad->u_length;
    const float dv = quad->v_length;
    if (quad->axis == 0) {
        corners[0][0] = corners[1][0] = corners[2][0] = corners[3][0] = bx;
        corners[0][1] = corners[3][1] = by;
        corners[1][1] = corners[2][1] = by + dv;
        corners[0][2] = corners[1][2] = bz;
        corners[2][2] = corners[3][2] = bz + du;
    } else if (quad->axis == 1) {
        corners[0][1] = corners[1][1] = corners[2][1] = corners[3][1] = by;
        corners[0][0] = corners[3][0] = bx;
        corners[1][0] = corners[2][0] = bx + du;
        corners[0][2] = corners[1][2] = bz;
        corners[2][2] = corners[3][2] = bz + dv;
    } else {
        corners[0][2] = corners[1][2] = corners[2][2] = corners[3][2] = bz;
        corners[0][0] = corners[3][0] = bx;
        corners[1][0] = corners[2][0] = bx + du;
        corners[0][1] = corners[1][1] = by;
        corners[2][1] = corners[3][1] = by + dv;
    }
}

typedef struct {
    float right;
    float up;
    float depth;
    float u_q4;
    float v_q4;
} clip_vertex_t;

enum {
    CLIP_NEAR = 0,
    CLIP_FAR,
    CLIP_LEFT,
    CLIP_RIGHT,
    CLIP_BOTTOM,
    CLIP_TOP,
    CLIP_PLANE_COUNT,
};

#define VOXEL_CLIP_VERTICES 12u

static float clip_distance(const raster_camera_t *camera,
                           const clip_vertex_t *vertex, uint8_t plane) {
    if (plane == CLIP_NEAR) return vertex->depth - VOXEL_RASTER_NEAR;
    if (plane == CLIP_FAR) return camera->fog_end - vertex->depth;
    if (plane == CLIP_LEFT)
        return vertex->right + vertex->depth * camera->tan_x;
    if (plane == CLIP_RIGHT)
        return vertex->depth * camera->tan_x - vertex->right;
    if (plane == CLIP_BOTTOM)
        return vertex->up + vertex->depth * camera->tan_y;
    return vertex->depth * camera->tan_y - vertex->up;
}

static uint8_t clip_outcode(const raster_camera_t *camera,
                            const clip_vertex_t *vertex) {
    const float horizontal = vertex->depth * camera->tan_x;
    const float vertical = vertex->depth * camera->tan_y;
    uint8_t code = 0;
    if (vertex->depth < VOXEL_RASTER_NEAR) code |= 1u << CLIP_NEAR;
    if (vertex->depth > camera->fog_end) code |= 1u << CLIP_FAR;
    if (vertex->right < -horizontal) code |= 1u << CLIP_LEFT;
    if (vertex->right > horizontal) code |= 1u << CLIP_RIGHT;
    if (vertex->up < -vertical) code |= 1u << CLIP_BOTTOM;
    if (vertex->up > vertical) code |= 1u << CLIP_TOP;
    return code;
}

static clip_vertex_t clip_intersection(const clip_vertex_t *a,
                                       const clip_vertex_t *b, float a_distance,
                                       float b_distance) {
    clip_vertex_t result;
    const float denominator = a_distance - b_distance;
    const float t = denominator == 0.0F ? 0.0F : a_distance / denominator;
    result.right = a->right + (b->right - a->right) * t;
    result.up = a->up + (b->up - a->up) * t;
    result.depth = a->depth + (b->depth - a->depth) * t;
    result.u_q4 = a->u_q4 + (b->u_q4 - a->u_q4) * t;
    result.v_q4 = a->v_q4 + (b->v_q4 - a->v_q4) * t;
    return result;
}

static uint8_t clip_polygon(const raster_camera_t *camera,
                            const clip_vertex_t *input, uint8_t input_count,
                            clip_vertex_t *output, uint8_t plane) {
    const clip_vertex_t *previous;
    float previous_distance;
    uint8_t output_count = 0;
    uint8_t index;
    if (input_count == 0) return 0;
    previous = &input[input_count - 1u];
    previous_distance = clip_distance(camera, previous, plane);
    for (index = 0; index < input_count; ++index) {
        const clip_vertex_t *current = &input[index];
        const float current_distance = clip_distance(camera, current, plane);
        const uint8_t previous_inside = previous_distance >= 0.0F;
        const uint8_t current_inside = current_distance >= 0.0F;
        if (previous_inside != current_inside &&
            output_count < VOXEL_CLIP_VERTICES) {
            output[output_count++] = clip_intersection(
                previous, current, previous_distance, current_distance);
        }
        if (current_inside && output_count < VOXEL_CLIP_VERTICES)
            output[output_count++] = *current;
        previous = current;
        previous_distance = current_distance;
    }
    return output_count;
}

static void finish_projected_primitive(
    const raster_camera_t *camera, const mesh_quad_t *quad,
    const pxa_raster_vertex_t *vertices, const uint8_t indices[4],
    uint8_t unique_vertices, projected_quad_t *projected) {
    float depth = 0.0F;
    uint16_t min_depth = UINT16_MAX;
    uint16_t max_depth = 0;
    int32_t min_x = INT32_MAX;
    int32_t max_x = INT32_MIN;
    int32_t min_y = INT32_MAX;
    int32_t max_y = INT32_MIN;
    uint8_t light;
    uint8_t index;
    for (index = 0; index < unique_vertices; ++index) {
        const uint16_t vertex_depth = vertices[indices[index]].depth_q8;
        depth += (float)vertex_depth / 256.0F;
        if (vertex_depth < min_depth) min_depth = vertex_depth;
        if (vertex_depth > max_depth) max_depth = vertex_depth;
    }
    depth /= unique_vertices;
    for (index = 0; index < 4; ++index) {
        const pxa_raster_vertex_t *vertex = &vertices[indices[index]];
        if (vertex->x_q4 < min_x) min_x = vertex->x_q4;
        if (vertex->x_q4 > max_x) max_x = vertex->x_q4;
        if (vertex->y_q4 < min_y) min_y = vertex->y_q4;
        if (vertex->y_q4 > max_y) max_y = vertex->y_q4;
    }
    light = quad->axis == 1 ? (quad->sign > 0 ? 255u : 116u)
                            : (quad->axis == 0 ? 190u : 160u);
    if (depth > 14.0F) {
        float fog = (depth - 14.0F) / (camera->fog_end - 14.0F);
        if (fog > 1.0F) fog = 1.0F;
        light = (uint8_t)((float)light * (1.0F - fog * 0.65F));
    }
    for (index = 0; index < 4; ++index) {
        projected->vertices[index] = vertices[indices[index]];
        projected->vertices[index].light = light;
        if (quad->block == BLOCK_WATER) {
            projected->vertices[index].u_q4 = (int16_t)(
                projected->vertices[index].u_q4 + g_water_uv_offset_q4);
            projected->vertices[index].v_q4 = (int16_t)(
                projected->vertices[index].v_q4 +
                (g_water_uv_offset_q4 >> 1));
        }
    }
    projected->nearest_depth_q8 = min_depth;
    projected->farthest_depth_q8 = max_depth;
    /* Preserve the tuned fallback for small unsplit faces. Adaptive painter
     * pieces override this with their bounded interval midpoint below. */
    projected->sort_depth_q8 = quad->axis == 1 ? max_depth : min_depth;
    projected->textured = quad->block <= VOXEL_RASTER_TEXTURED_BLOCKS;
    projected->transparent_index0 = block_uses_cutout(quad->block);
    projected->blend_75 = block_is_translucent(quad->block);
    projected->color = render_block_color(quad->block);
    projected->affine = 0;
    if (projected->textured) {
        const uint8_t kind =
            quad->axis == 1
                ? (quad->sign > 0 ? BLOCK_TEXTURE_TOP : BLOCK_TEXTURE_BOTTOM)
                : BLOCK_TEXTURE_SIDE;
        projected->texture_slot = texture_slot_for(
            (uint8_t)quad->block, kind);
        /* Faces below roughly 4x4 screen pixels skip the texture and use the
         * block colour; the texel detail is invisible at that size. */
        if (!projected->transparent_index0 && !projected->blend_75 &&
            (max_x - min_x < 4 * 16 || max_y - min_y < 4 * 16)) {
            projected->textured = 0;
            return;
        }
        /* Affine UV is only worth its warping when the depth range across the
         * face stays small relative to its merged texel span. The bound keeps
         * the worst-case affine error near half a texel. */
        {
            const uint8_t span = quad->u_length > quad->v_length
                                     ? quad->u_length
                                     : quad->v_length;
            if (min_depth != 0 &&
                (uint32_t)(max_depth - min_depth) * 8u * span <= min_depth)
                projected->affine = 1;
        }
    }
}

static uint8_t project_quad_raw(const raster_camera_t *camera,
                                const chunk_t *chunk,
                                const mesh_quad_t *quad,
                                projected_quad_t *output, uint16_t capacity,
                                uint8_t *clipped_out) {
    float corners[4][3];
    clip_vertex_t clip_a[VOXEL_CLIP_VERTICES];
    clip_vertex_t clip_b[VOXEL_CLIP_VERTICES];
    clip_vertex_t *input = clip_a;
    clip_vertex_t *clipped = clip_b;
    pxa_raster_vertex_t vertices[VOXEL_CLIP_VERTICES];
    float face_position;
    float normal_dot;
    uint8_t count = 4;
    uint8_t primitive_count;
    uint8_t was_clipped = 0;
    uint8_t outside_any = 0;
    uint8_t outside_all = (uint8_t)((1u << CLIP_PLANE_COUNT) - 1u);
    uint8_t plane;
    uint8_t index;
    if (clipped_out != NULL) *clipped_out = 0;
    if (capacity == 0) return 0;
    face_position = quad->axis == 0
                        ? (float)chunk->cx * CHUNK_SIZE + quad->x
                    : quad->axis == 1
                        ? quad->y
                        : (float)chunk->cz * CHUNK_SIZE + quad->z;
    normal_dot = quad->sign *
        (quad->axis == 0 ? camera->x - face_position
         : quad->axis == 1 ? camera->y - face_position
                           : camera->z - face_position);
    /* Water surface caps are two-sided so the lake surface is visible from
     * below. Vertical water walls stay culled: from inside a lake they would
     * otherwise turn into a flat blue wall at the far shore. */
    if (normal_dot <= 0.0F &&
        !(quad->block == BLOCK_WATER && quad->axis == 1)) {
        ++g_stats.backface_culled;
        return 0;
    }
    quad_corners(chunk, quad, corners);
    for (index = 0; index < 4; ++index) {
        const float dx = corners[index][0] - camera->x;
        const float dy = corners[index][1] - camera->y;
        const float dz = corners[index][2] - camera->z;
        input[index].right = dx * camera->rx + dz * camera->rz;
        input[index].up =
            dx * camera->ux + dy * camera->uy + dz * camera->uz;
        input[index].depth =
            dx * camera->fx + dy * camera->fy + dz * camera->fz;
        {
            const uint8_t outcode = clip_outcode(camera, &input[index]);
            outside_any |= outcode;
            outside_all &= outcode;
        }
    }
    if (outside_all != 0) {
        ++g_stats.frustum_culled;
        return 0;
    }
    {
        /* Texture V grows downwards inside a tile, so V is flipped against
         * the world axis the face runs along: the highest world corner
         * samples row 0. This matches the ray caster's
         * `ty = 15 - frac(v)` and keeps grass fringes and table edges up.
         * Extended slots give every face kind its own 16x16 texture, so V
         * never leaves the tile when a face spans several blocks. */
        if (quad->axis == 0) {
            input[0].u_q4 = input[1].u_q4 = 0.0F;
            input[2].u_q4 = input[3].u_q4 = quad->u_length * 256.0F;
            input[0].v_q4 = input[3].v_q4 = quad->v_length * 256.0F;
            input[1].v_q4 = input[2].v_q4 = 0.0F;
        } else if (quad->axis == 1) {
            input[0].u_q4 = input[3].u_q4 = 0.0F;
            input[1].u_q4 = input[2].u_q4 = quad->u_length * 256.0F;
            input[0].v_q4 = input[1].v_q4 = quad->v_length * 256.0F;
            input[2].v_q4 = input[3].v_q4 = 0.0F;
        } else {
            input[0].u_q4 = input[3].u_q4 = 0.0F;
            input[1].u_q4 = input[2].u_q4 = quad->u_length * 256.0F;
            input[0].v_q4 = input[1].v_q4 = quad->v_length * 256.0F;
            input[2].v_q4 = input[3].v_q4 = 0.0F;
        }
    }
    for (plane = 0; outside_any != 0 && plane < CLIP_PLANE_COUNT; ++plane) {
        uint8_t needs_clip = 0;
        clip_vertex_t *swap;
        if ((outside_any & (1u << plane)) == 0) continue;
        for (index = 0; index < count; ++index) {
            if (clip_distance(camera, &input[index], plane) < 0.0F) {
                needs_clip = 1;
                break;
            }
        }
        if (!needs_clip) continue;
        was_clipped = 1;
        count = clip_polygon(camera, input, count, clipped, plane);
        if (count < 3) break;
        swap = input;
        input = clipped;
        clipped = swap;
    }
    if (count < 3) {
        ++g_stats.frustum_culled;
        return 0;
    }
    if (was_clipped) {
        ++g_stats.clipped_quads;
        if (clipped_out != NULL) *clipped_out = 1;
    }
    for (index = 0; index < count; ++index) {
        float screen_x = camera->width * 0.5F +
            input[index].right / input[index].depth *
                (camera->width * 0.5F) / camera->tan_x;
        float screen_y = camera->height * 0.5F -
            input[index].up / input[index].depth *
                (camera->height * 0.5F) / camera->tan_y;
        if (screen_x < 0.0F) screen_x = 0.0F;
        if (screen_x > camera->width) screen_x = camera->width;
        if (screen_y < 0.0F) screen_y = 0.0F;
        if (screen_y > camera->height) screen_y = camera->height;
        vertices[index].x_q4 = (int16_t)(screen_x * 16.0F + 0.5F);
        vertices[index].y_q4 = (int16_t)(screen_y * 16.0F + 0.5F);
        vertices[index].u_q4 = (int16_t)(input[index].u_q4 + 0.5F);
        vertices[index].v_q4 = (int16_t)(input[index].v_q4 + 0.5F);
        vertices[index].depth_q8 =
            (uint16_t)(input[index].depth * 256.0F + 0.5F);
    }
    primitive_count = count <= 4 ? 1u : (uint8_t)(count - 2u);
    if (primitive_count > capacity) {
        g_stats.dropped_quads += primitive_count - capacity;
        primitive_count = capacity;
    }
    if (count <= 4) {
        uint8_t indices[4] = {0, 1, 2, 2};
        if (count == 4) indices[3] = 3;
        finish_projected_primitive(camera, quad, vertices, indices, count,
                                   &output[0]);
    } else {
        for (index = 0; index < primitive_count; ++index) {
            const uint8_t indices[4] = {
                0, (uint8_t)(index + 1u), (uint8_t)(index + 2u),
                (uint8_t)(index + 2u)};
            finish_projected_primitive(camera, quad, vertices, indices, 3,
                                       &output[index]);
        }
    }
    return primitive_count;
}

static int projected_quad_needs_split(const projected_quad_t *quad) {
    int32_t min_x = INT32_MAX;
    int32_t max_x = INT32_MIN;
    int32_t min_y = INT32_MAX;
    int32_t max_y = INT32_MIN;
    uint8_t index;
    if (quad->nearest_depth_q8 < VOXEL_RASTER_HYBRID_DEPTH_Q8 ||
        (uint32_t)quad->farthest_depth_q8 - quad->nearest_depth_q8 <=
            VOXEL_RASTER_PAINTER_SPLIT_DEPTH_Q8)
        return 0;
    for (index = 0; index < 4; ++index) {
        const pxa_raster_vertex_t *vertex = &quad->vertices[index];
        if (vertex->x_q4 < min_x) min_x = vertex->x_q4;
        if (vertex->x_q4 > max_x) max_x = vertex->x_q4;
        if (vertex->y_q4 < min_y) min_y = vertex->y_q4;
        if (vertex->y_q4 > max_y) max_y = vertex->y_q4;
    }
    return max_x - min_x >= VOXEL_RASTER_PAINTER_SPLIT_SCREEN_Q4 ||
           max_y - min_y >= VOXEL_RASTER_PAINTER_SPLIT_SCREEN_Q4;
}

static void painter_depth_spans(const raster_camera_t *camera,
                                const mesh_quad_t *quad, float *u_span,
                                float *v_span) {
    float u_forward;
    float v_forward;
    if (quad->axis == 0) {
        u_forward = camera->fz;
        v_forward = camera->fy;
    } else if (quad->axis == 1) {
        u_forward = camera->fx;
        v_forward = camera->fz;
    } else {
        u_forward = camera->fx;
        v_forward = camera->fy;
    }
    *u_span = rc_fabs(u_forward) * quad->u_length;
    *v_span = rc_fabs(v_forward) * quad->v_length;
}

static void painter_split_counts(const raster_camera_t *camera,
                                 const mesh_quad_t *quad,
                                 uint8_t max_parts, uint8_t *u_parts,
                                 uint8_t *v_parts) {
    float u_span;
    float v_span;
    painter_depth_spans(camera, quad, &u_span, &v_span);
    *u_parts = 1;
    *v_parts = 1;
    while ((uint8_t)(*u_parts * *v_parts) < max_parts &&
           u_span * *v_parts + v_span * *u_parts >
               ((float)VOXEL_RASTER_PAINTER_SPLIT_DEPTH_Q8 / 256.0F) *
                   *u_parts * *v_parts) {
        const uint8_t can_split_u =
            *u_parts < quad->u_length &&
            (uint8_t)((*u_parts + 1u) * *v_parts) <= max_parts;
        const uint8_t can_split_v =
            *v_parts < quad->v_length &&
            (uint8_t)(*u_parts * (*v_parts + 1u)) <= max_parts;
        if (!can_split_u && !can_split_v) break;
        /* Split the axis with the larger remaining depth contribution. The
         * cross multiplication avoids soft floating-point division. */
        if (can_split_u &&
            (!can_split_v ||
             u_span * *v_parts >= v_span * *u_parts)) {
            ++*u_parts;
        } else {
            ++*v_parts;
        }
    }
}

static mesh_quad_t mesh_quad_part(const mesh_quad_t *quad, uint8_t u_part,
                                  uint8_t u_parts, uint8_t v_part,
                                  uint8_t v_parts) {
    mesh_quad_t part = *quad;
    const uint8_t u_start =
        (uint8_t)((uint16_t)quad->u_length * u_part / u_parts);
    const uint8_t u_end =
        (uint8_t)((uint16_t)quad->u_length * (u_part + 1u) / u_parts);
    const uint8_t v_start =
        (uint8_t)((uint16_t)quad->v_length * v_part / v_parts);
    const uint8_t v_end =
        (uint8_t)((uint16_t)quad->v_length * (v_part + 1u) / v_parts);
    part.u_length = (uint8_t)(u_end - u_start);
    part.v_length = (uint8_t)(v_end - v_start);
    if (quad->axis == 0) {
        part.z = (uint8_t)(part.z + u_start);
        part.y = (uint8_t)(part.y + v_start);
    } else if (quad->axis == 1) {
        part.x = (uint8_t)(part.x + u_start);
        part.z = (uint8_t)(part.z + v_start);
    } else {
        part.x = (uint8_t)(part.x + u_start);
        part.y = (uint8_t)(part.y + v_start);
    }
    return part;
}

static uint8_t project_quad(const raster_camera_t *camera,
                            const chunk_t *chunk, const mesh_quad_t *quad,
                            projected_quad_t *output, uint16_t capacity,
                            uint8_t adaptive_painter) {
    uint8_t clipped = 0;
    uint8_t count = project_quad_raw(camera, chunk, quad, output, capacity,
                                     &clipped);
    uint8_t u_parts;
    uint8_t v_parts;
    uint8_t total = 0;
    uint8_t v_part;
    if (!adaptive_painter || clipped || count != 1u || capacity < 2u ||
        !projected_quad_needs_split(&output[0]))
        return count;
    painter_split_counts(
        camera, quad,
        capacity < VOXEL_RASTER_PAINTER_SPLIT_MAX_PARTS
            ? (uint8_t)capacity
            : VOXEL_RASTER_PAINTER_SPLIT_MAX_PARTS,
        &u_parts, &v_parts);
    if (u_parts == 1u && v_parts == 1u) return count;
    for (v_part = 0; v_part < v_parts; ++v_part) {
        uint8_t u_part;
        for (u_part = 0; u_part < u_parts; ++u_part) {
            const mesh_quad_t part = mesh_quad_part(
                quad, u_part, u_parts, v_part, v_parts);
            const uint8_t first = total;
            const uint8_t added = project_quad_raw(
                camera, chunk, &part, &output[total],
                (uint16_t)(capacity - total), NULL);
            uint8_t index;
            total = (uint8_t)(total + added);
            for (index = first; index < total; ++index) {
                projected_quad_t *piece = &output[index];
                piece->sort_depth_q8 = (uint16_t)(
                    piece->nearest_depth_q8 +
                    (piece->farthest_depth_q8 - piece->nearest_depth_q8) / 2u);
            }
        }
    }
    if (total > 1u) g_stats.painter_splits += total - 1u;
    return total != 0u ? total : count;
}

static int project_billboard(const raster_camera_t *camera, float x, float y,
                             float z, float width, float height,
                             uint16_t color, projected_quad_t *projected) {
    const float half_width = width * 0.5F;
    float corners[4][3];
    float min_depth = 1.0e9F;
    float min_x = 1.0e9F;
    float min_y = 1.0e9F;
    float max_x = -1.0e9F;
    float max_y = -1.0e9F;
    uint8_t index;
    corners[0][0] = corners[1][0] = x - camera->rx * half_width;
    corners[0][2] = corners[1][2] = z - camera->rz * half_width;
    corners[2][0] = corners[3][0] = x + camera->rx * half_width;
    corners[2][2] = corners[3][2] = z + camera->rz * half_width;
    corners[0][1] = corners[3][1] = y;
    corners[1][1] = corners[2][1] = y + height;
    for (index = 0; index < 4; ++index) {
        const float dx = corners[index][0] - camera->x;
        const float dy = corners[index][1] - camera->y;
        const float dz = corners[index][2] - camera->z;
        const float depth = dx * camera->fx + dy * camera->fy +
                            dz * camera->fz;
        const float right = dx * camera->rx + dz * camera->rz;
        const float up = dx * camera->ux + dy * camera->uy +
                         dz * camera->uz;
        float screen_x;
        float screen_y;
        if (depth < VOXEL_RASTER_NEAR || depth > camera->fog_end) return 0;
        screen_x = camera->width * 0.5F +
                   right / depth * (camera->width * 0.5F) / camera->tan_x;
        screen_y = camera->height * 0.5F -
                   up / depth * (camera->height * 0.5F) / camera->tan_y;
        projected->vertices[index].x_q4 = (int16_t)(screen_x * 16.0F);
        projected->vertices[index].y_q4 = (int16_t)(screen_y * 16.0F);
        projected->vertices[index].u_q4 = 0;
        projected->vertices[index].v_q4 = 0;
        projected->vertices[index].light = 255;
        projected->vertices[index].depth_q8 =
            (uint16_t)(depth * 256.0F + 0.5F);
        if (depth < min_depth) min_depth = depth;
        if (screen_x < min_x) min_x = screen_x;
        if (screen_x > max_x) max_x = screen_x;
        if (screen_y < min_y) min_y = screen_y;
        if (screen_y > max_y) max_y = screen_y;
    }
    if (max_x < 0.0F || max_y < 0.0F || min_x >= camera->width ||
        min_y >= camera->height)
        return 0;
    projected->nearest_depth_q8 =
        (uint16_t)(min_depth * 256.0F + 0.5F);
    projected->farthest_depth_q8 = projected->nearest_depth_q8;
    /* Keep billboards at their nearest depth so a mob immediately behind a
     * wall cannot move ahead of it in painter order. */
    projected->sort_depth_q8 = projected->nearest_depth_q8;
    projected->color = color;
    projected->texture_slot = 0;
    projected->textured = 0;
    projected->affine = 0;
    projected->transparent_index0 = 0;
    projected->blend_75 = 0;
    return 1;
}

static void append_entities(const raster_camera_t *camera,
                            uint32_t candidate_limit,
                            uint32_t *candidate_count) {
    static const uint8_t mob_red[MOB_KIND_COUNT] = {104, 232, 232, 110};
    static const uint8_t mob_green[MOB_KIND_COUNT] = {188, 232, 150, 80};
    static const uint8_t mob_blue[MOB_KIND_COUNT] = {104, 225, 160, 60};
    uint32_t particle_count = 0;
    int index;
    for (index = 0; index < MAX_MOBS && *candidate_count < candidate_limit;
         ++index) {
        const mob_t *mob = &g_mobs[index];
        const uint8_t kind = mob->kind < MOB_KIND_COUNT ? mob->kind : MOB_SLIME;
        uint16_t color;
        if (!mob->alive) continue;
        color = mob->hurt > 0.0F
                    ? rgb565(255, 72, 72)
                    : rgb565(mob_red[kind], mob_green[kind], mob_blue[kind]);
        if (project_billboard(camera, mob->x, mob->y, mob->z,
                              MOB_HALF * 2.0F, MOB_HEIGHT, color,
                              &g_candidates[*candidate_count]))
            ++*candidate_count;
    }
    for (index = 0; index < MAX_PARTICLES &&
                    particle_count < VOXEL_RASTER_PARTICLE_LIMIT &&
                    *candidate_count < candidate_limit;
         ++index) {
        const particle_t *particle = &g_particles[index];
        const uint16_t color = render_block_color(particle->block);
        if (particle->life <= 0.0F) continue;
        if (project_billboard(camera, particle->x, particle->y, particle->z,
                              0.10F, 0.10F, color,
                              &g_candidates[*candidate_count])) {
            ++*candidate_count;
            ++particle_count;
        }
    }
}

static void sort_candidates(uint32_t count, uint8_t back_to_front) {
    uint32_t gap;
    uint32_t index;
    if (count > VOXEL_RASTER_CANDIDATES) count = VOXEL_RASTER_CANDIDATES;
    for (index = 0; index < count; ++index)
        g_sort_order[index] = (uint16_t)index;
    /* Chunk traversal already emits near chunks first. Native depth testing
     * makes exact polygon ordering unnecessary. Painter fallback uses Q8
     * integer keys so sorting does not add floating-point comparisons. */
    if (!back_to_front) return;
    for (gap = count / 2u; gap != 0; gap /= 2u) {
        for (index = gap; index < count; ++index) {
            const uint16_t value = g_sort_order[index];
            const uint16_t depth = g_candidates[value].sort_depth_q8;
            uint32_t cursor = index;
            while (cursor >= gap &&
                   g_candidates[g_sort_order[cursor - gap]].sort_depth_q8 <
                       depth) {
                g_sort_order[cursor] = g_sort_order[cursor - gap];
                cursor -= gap;
            }
            g_sort_order[cursor] = value;
        }
    }
}

static int projected_quad_has_area(const projected_quad_t *quad) {
    int64_t area = 0;
    uint8_t corner;
    if (quad == NULL) return 0;
    for (corner = 0; corner < 4; ++corner) {
        const pxa_raster_vertex_t *a = &quad->vertices[corner];
        const pxa_raster_vertex_t *b = &quad->vertices[(corner + 1u) & 3u];
        area += (int64_t)a->x_q4 * b->y_q4 -
                (int64_t)b->x_q4 * a->y_q4;
    }
    return area != 0;
}

static int append_projected_quad(pxa_raster_draw_list_t *list,
                                 const projected_quad_t *quad,
                                 uint8_t painter,
                                 uint8_t lit_palette_depth) {
    if (quad->textured &&
        (g_raster_capabilities & PXA_RASTER_CAP_TEXTURED_QUAD) != 0) {
        pxa_raster_vertex_t vertices[4];
        uint8_t flags = 0;
        uint8_t corner;
        for (corner = 0; corner < 4; ++corner)
            vertices[corner] = quad->vertices[corner];
        if (painter) {
            const uint8_t row = light_row(vertices[0].light);
            flags = PXA_RASTER_QUAD_PAINTER;
            if (quad->transparent_index0)
                flags |= PXA_RASTER_QUAD_TRANSPARENT_INDEX0;
            if (quad->blend_75 &&
                (g_raster_capabilities &
                 PXA_RASTER_CAP_FIXED_ALPHA_BLEND) != 0)
                flags |= PXA_RASTER_QUAD_BLEND_75;
            for (corner = 0; corner < 4; ++corner) {
                vertices[corner].light = row;
                vertices[corner].depth_q8 = 0;
            }
        } else {
            if (quad->affine &&
                (g_raster_capabilities & PXA_RASTER_CAP_AFFINE_UV) != 0)
                flags |= PXA_RASTER_QUAD_AFFINE_UV;
            if (lit_palette_depth) {
                const uint8_t row = light_row(vertices[0].light);
                flags |= PXA_RASTER_QUAD_LIT_PALETTE;
                for (corner = 0; corner < 4; ++corner)
                    vertices[corner].light = row;
            }
            if (quad->transparent_index0 &&
                (g_raster_capabilities & PXA_RASTER_CAP_DEPTH_CUTOUT) != 0)
                flags |= PXA_RASTER_QUAD_TRANSPARENT_INDEX0;
            if (quad->blend_75 &&
                (g_raster_capabilities &
                 PXA_RASTER_CAP_FIXED_ALPHA_BLEND) != 0)
                flags |= PXA_RASTER_QUAD_BLEND_75;
        }
        return pxa_raster_textured_quad_flags(
            list, vertices, quad->texture_slot, flags);
    }
    if (!painter &&
        (g_raster_capabilities & PXA_RASTER_CAP_TEXTURED_QUAD) != 0)
        return pxa_raster_solid_depth_quad(list, quad->vertices, quad->color);
    {
        int16_t xy[8];
        uint8_t corner;
        for (corner = 0; corner < 4; ++corner) {
            xy[corner * 2u] = quad->vertices[corner].x_q4;
            xy[corner * 2u + 1u] = quad->vertices[corner].y_q4;
        }
        return pxa_raster_flat_quad(list, xy, quad->color);
    }
}

static int font_character_index(char character) {
    uint8_t index;
    for (index = 0; index < VOXEL_RASTER_FONT_GLYPHS; ++index)
        if (kFontCharacters[index] == character) return index;
    return VOXEL_RASTER_FONT_GLYPHS - 1u;
}

static int raster_text_width(const char *text, int scale) {
    int length = 0;
    while (text != NULL && text[length] != '\0') ++length;
    return length * 4 * scale;
}

static char *append_u32_text(char *output, uint32_t value) {
    char reverse[10];
    uint8_t count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0 && count < sizeof(reverse));
    while (count != 0) *output++ = reverse[--count];
    return output;
}

static void append_rect(pxa_raster_draw_list_t *list, int x, int y, int width,
                        int height, uint16_t color) {
    int16_t quad[8];
    quad[0] = quad[6] = (int16_t)(x * 16);
    quad[1] = quad[3] = (int16_t)(y * 16);
    quad[2] = quad[4] = (int16_t)((x + width) * 16);
    quad[5] = quad[7] = (int16_t)((y + height) * 16);
    (void)pxa_raster_flat_quad(list, quad, color);
}

static void append_sky_rect(pxa_raster_draw_list_t *list,
                            const raster_camera_t *camera, int x, int y,
                            int width, int height, uint16_t color) {
    int right = x + width;
    int bottom = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > camera->width) right = camera->width;
    if (bottom > camera->height) bottom = camera->height;
    if (x < right && y < bottom)
        append_rect(list, x, y, right - x, bottom - y, color);
}

static int project_sky_direction(const raster_camera_t *camera,
                                 float dx, float dy, float dz,
                                 int *screen_x, int *screen_y) {
    const float depth = dx * camera->fx + dy * camera->fy +
                        dz * camera->fz;
    const float right = dx * camera->rx + dz * camera->rz;
    const float up = dx * camera->ux + dy * camera->uy +
                     dz * camera->uz;
    if (depth <= 0.15F) return 0;
    *screen_x = (int)((float)camera->width * 0.5F +
                      right / depth * (float)camera->width *
                          0.5F / camera->tan_x);
    *screen_y = (int)((float)camera->height * 0.5F -
                      up / depth * (float)camera->height *
                          0.5F / camera->tan_y);
    return 1;
}

static void append_sky(pxa_raster_draw_list_t *list,
                       const raster_camera_t *camera, uint8_t gradient) {
    const int bands = 10;
    if (gradient) {
        for (int band = 0; band < bands; ++band) {
            const int top = band * camera->height / bands;
            const int bottom = (band + 1) * camera->height / bands;
            const float vertical =
                (1.0F - (float)(top + bottom) / camera->height) *
                camera->tan_y;
            const float elevation = (camera->fy + camera->uy * vertical) /
                                    rc_sqrt(1.0F + vertical * vertical);
            int r = elevation >= 0.0F ? 150 - (int)(elevation * 95.0F)
                                       : 150 - (int)(elevation * 30.0F);
            int g = elevation >= 0.0F ? 200 - (int)(elevation * 75.0F)
                                       : 200 + (int)(elevation * 25.0F);
            int b = elevation >= 0.0F ? 252 - (int)(elevation * 30.0F)
                                       : 240 + (int)(elevation * 30.0F);
            r = rc_clampi(r, 0, 255);
            g = rc_clampi(g, 0, 255);
            b = rc_clampi(b, 0, 255);
            append_sky_rect(list, camera, 0, top, camera->width,
                            bottom - top, rgb565((uint8_t)r, (uint8_t)g,
                                                (uint8_t)b));
        }
    }
    for (int index = 0; index < VOXEL_SKY_CLOUD_COUNT; ++index) {
        int x;
        int y;
        const float altitude = index & 1 ? 0.32F : 0.42F;
        if (!project_sky_direction(camera,
                                   kVoxelSkyCloudDirections[index][0],
                                   altitude,
                                   kVoxelSkyCloudDirections[index][1],
                                   &x, &y) || x < -40 ||
            x > camera->width + 40 || y < -15 ||
            y > camera->height + 15)
            continue;
        append_sky_rect(list, camera, x - 15, y, 30, 5,
                        UINT16_C(0xd75d));
        append_sky_rect(list, camera, x - 18, y - 4, 36, 5,
                        UINT16_C(0xf7de));
        append_sky_rect(list, camera, x - 9, y - 8, 18, 5,
                        UINT16_C(0xffff));
    }
    {
        int x;
        int y;
        if (project_sky_direction(camera, VOXEL_SKY_SUN_X,
                                  VOXEL_SKY_SUN_Y, VOXEL_SKY_SUN_Z,
                                  &x, &y)) {
            append_sky_rect(list, camera, x - 11, y - 11, 22, 22,
                            UINT16_C(0xff5a));
            append_sky_rect(list, camera, x - 7, y - 7, 14, 14,
                            UINT16_C(0xffdf));
            append_sky_rect(list, camera, x - 5, y - 5, 10, 10,
                            UINT16_C(0xffff));
        }
    }
}

typedef struct {
    uint16_t width;
    uint16_t height;
    const render_layout_t *layout;
} raster_ui_t;

static int ui_x(const raster_ui_t *ui, int value) {
    return value * (int)ui->width / ui->layout->screen_w;
}

static int ui_y(const raster_ui_t *ui, int value) {
    return value * (int)ui->height / ui->layout->screen_h;
}

static void append_ui_rect(pxa_raster_draw_list_t *list, const raster_ui_t *ui,
                           int x, int y, int rect_width, int rect_height,
                           uint16_t color) {
    int right;
    int bottom;
    if (rect_width <= 0 || rect_height <= 0) return;
    right = ui_x(ui, x + rect_width);
    bottom = ui_y(ui, y + rect_height);
    x = ui_x(ui, x);
    y = ui_y(ui, y);
    if (right <= x) right = x + 1;
    if (bottom <= y) bottom = y + 1;
    append_rect(list, x, y, right - x, bottom - y, color);
}

static void append_ui_outline(pxa_raster_draw_list_t *list,
                              const raster_ui_t *ui, int x, int y,
                              int rect_width, int rect_height,
                              int thickness, uint16_t color) {
    append_ui_rect(list, ui, x, y, rect_width, thickness, color);
    append_ui_rect(list, ui, x, y + rect_height - thickness, rect_width,
                   thickness, color);
    append_ui_rect(list, ui, x, y, thickness, rect_height, color);
    append_ui_rect(list, ui, x + rect_width - thickness, y, thickness,
                   rect_height, color);
}

static void append_ui_text(pxa_raster_draw_list_t *list, const raster_ui_t *ui,
                           int x, int y, const char *text, uint16_t color,
                           int scale) {
    int cursor_x = ui_x(ui, x);
    while (text != NULL && *text != '\0') {
        const int glyph = font_character_index(*text++);
        const int mapped_x = ui_x(ui, x);
        const int next_x = ui_x(ui, x + 3 * scale);
        const int next_y = ui_y(ui, y + 5 * scale);
        const int mapped_y = ui_y(ui, y);
        const uint16_t glyph_width = (uint16_t)(next_x - mapped_x >= 2
                                                     ? next_x - mapped_x
                                                     : 2);
        const uint16_t glyph_height = (uint16_t)(next_y - mapped_y >= 3
                                                      ? next_y - mapped_y
                                                      : 3);
        if (glyph != VOXEL_RASTER_FONT_GLYPHS - 1u) {
            /* A 3x5 source glyph must not be reduced to a single sampled
             * column at 2x dynamic resolution: that turns most letters into
             * dots after the Host scales the scene back up. */
            (void)pxa_raster_sprite(
                list, font_slot(),
                PXA_RASTER_SPRITE_TRANSPARENT_INDEX0 |
                    PXA_RASTER_SPRITE_SOLID_COLOR,
                g_raster_capabilities, (int16_t)cursor_x, (int16_t)mapped_y,
                glyph_width, glyph_height, (uint16_t)(glyph * 4), 0, 3, 5,
                color);
        }
        {
            const int next_cursor = ui_x(ui, x + 4 * scale);
            const int advance = next_cursor - mapped_x;
            cursor_x += advance >= (int)glyph_width ? advance : glyph_width;
        }
        x += 4 * scale;
    }
}

static int ui_text_width(const raster_ui_t *ui, const char *text, int scale) {
    int width = 0;
    while (text != NULL && *text++ != '\0') {
        const int x0 = ui_x(ui, 0);
        const int x1 = ui_x(ui, 4 * scale);
        const int advance = x1 - x0;
        width += advance >= 2 ? advance : 2;
    }
    return width;
}

/* Lightens (percent > 0) or darkens (percent < 0) a 5/6/5 colour. */
static uint16_t shade565(uint16_t color, int percent) {
    uint32_t red = (color >> 11) & 31u;
    uint32_t green = (color >> 5) & 63u;
    uint32_t blue = color & 31u;
    if (percent >= 0) {
        const uint32_t amount = (uint32_t)percent;
        red += (31u - red) * amount / 100u;
        green += (63u - green) * amount / 100u;
        blue += (31u - blue) * amount / 100u;
    } else {
        const uint32_t amount = (uint32_t)(-percent);
        red -= red * amount / 100u;
        green -= green * amount / 100u;
        blue -= blue * amount / 100u;
    }
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static int button_half_width(int radius, int dy) {
    const int inner = radius * radius - dy * dy;
    return inner > 0 ? (int)(rc_sqrt((float)inner) + 0.5F) : 0;
}

/* A round HUD button: drop shadow, shaded face and a lighter top third, drawn
 * as horizontal spans so it stays smooth at every resolution. */
static void append_ui_button(pxa_raster_draw_list_t *list,
                             const raster_ui_t *ui, int cx, int cy,
                             int radius, uint16_t fill, uint8_t active) {
    const int mx = ui_x(ui, cx);
    const int my = ui_y(ui, cy);
    const uint16_t face = active ? HUD_BTN_ACTIVE : fill;
    const uint16_t edge = shade565(face, -58);
    int mr = ui_x(ui, cx + radius) - mx;
    int step;
    int dy;
    if (mr < 4) mr = 4;
    step = 1 + mr / 10;
    /* Drop shadow: the face silhouette offset down and right. */
    for (dy = -mr + step; dy <= mr + 1; dy += step) {
        const int half = button_half_width(mr, dy - 1 - step / 2);
        if (half <= 1) continue;
        append_rect(list, mx - half + 1, my + dy, half * 2, step, edge);
    }
    /* Flat face: the offset shadow alone gives the button its depth, so no
     * highlight band crosses it. */
    for (dy = -mr; dy <= mr; dy += step) {
        const int center = dy + step / 2;
        const int half = button_half_width(mr, center);
        if (half <= 1) continue;
        append_rect(list, mx - half, my + dy, half * 2, step, face);
    }
}

static void append_ui_arrow(pxa_raster_draw_list_t *list, const raster_ui_t *ui,
                            int cx, int cy, int down) {
    const int s = ui->layout->ui_scale;
    int index;
    for (index = 0; index < 6 * s; ++index) {
        const int half = down ? 6 * s - 1 - index : index;
        append_ui_rect(list, ui, cx - half, cy - 2 * s + index,
                       half * 2 + 1, s, HUD_TEXT);
    }
}

static void append_ui_item(pxa_raster_draw_list_t *list, const raster_ui_t *ui,
                           int x, int y, int size, uint8_t item) {
    const int mapped_x = ui_x(ui, x);
    const int mapped_y = ui_y(ui, y);
    const int mapped_right = ui_x(ui, x + size);
    const int mapped_bottom = ui_y(ui, y + size);
    const uint16_t width = (uint16_t)(mapped_right > mapped_x
                                          ? mapped_right - mapped_x
                                          : 1);
    const uint16_t height = (uint16_t)(mapped_bottom > mapped_y
                                           ? mapped_bottom - mapped_y
                                           : 1);
    if (item == BLOCK_AIR) return;
    if (item <= VOXEL_RASTER_TEXTURED_BLOCKS) {
        /* Inventory icons show the side tile of the item's block. */
        (void)pxa_raster_sprite(list,
                                texture_slot_for(item, BLOCK_TEXTURE_SIDE), 0,
                                g_raster_capabilities, (int16_t)mapped_x,
                                (int16_t)mapped_y, width, height, 0, 0, 16,
                                16, 0);
    } else if (item < BLOCK_TYPE_COUNT) {
        append_ui_rect(list, ui, x, y, size, size, render_block_color(item));
    } else {
        const uint16_t tool = item <= ITEM_WOOD_SWORD ? UINT16_C(0xaeaa)
                                                       : UINT16_C(0x9cf3);
        append_ui_rect(list, ui, x + size / 2 - 1, y + 2, 3, size - 4, tool);
        append_ui_rect(list, ui, x + 2, y + 3, size - 4, 3, tool);
    }
}

static void append_ui_count(pxa_raster_draw_list_t *list, const raster_ui_t *ui,
                            int x, int y, uint8_t count, int scale) {
    char number[4];
    char *out = number;
    if (count == 0) return;
    out = append_u32_text(out, count);
    *out = '\0';
    append_ui_text(list, ui, x - ui_text_width(ui, number, scale),
                   y - (scale > 1 ? 2 * scale : 0), number, HUD_TEXT, scale);
}

static void append_hud(pxa_raster_draw_list_t *list, const hud_state_t *hud,
                       uint16_t width, uint16_t height) {
    raster_ui_t ui;
    char status[32];
    char *out;
    int slot;
    const render_layout_t *layout;
    int s;
    int center_x;
    int center_y;
    if (hud == NULL || hud->layout.screen_w <= 0 || hud->layout.screen_h <= 0)
        return;
    layout = &hud->layout;
    s = layout->ui_scale;
    center_x = layout->view_x + layout->view_w / 2;
    center_y = layout->view_y + layout->view_h / 2;
    ui.width = width;
    ui.height = height;
    ui.layout = layout;

    /* Crosshair matches the mapped renderer, but is emitted as compact quads. */
    append_ui_rect(list, &ui, center_x - 8, center_y, 6, 1, HUD_SHADOW);
    append_ui_rect(list, &ui, center_x + 2, center_y, 7, 1, HUD_SHADOW);
    append_ui_rect(list, &ui, center_x, center_y - 8, 1, 6, HUD_SHADOW);
    append_ui_rect(list, &ui, center_x, center_y + 2, 1, 7, HUD_SHADOW);
    append_ui_rect(list, &ui, center_x - 7, center_y, 5, 1, HUD_TEXT);
    append_ui_rect(list, &ui, center_x + 3, center_y, 5, 1, HUD_TEXT);
    append_ui_rect(list, &ui, center_x, center_y - 7, 1, 5, HUD_TEXT);
    append_ui_rect(list, &ui, center_x, center_y + 3, 1, 5, HUD_TEXT);

    for (slot = 0; slot < HOTBAR_SLOTS; ++slot) {
        const int slot_size = layout->hotbar_slot;
        const int padding = 2 * layout->ui_scale;
        const int x = layout->hotbar_x + slot * slot_size;
        const int y = layout->hotbar_y;
        append_ui_rect(list, &ui, x, y, slot_size, slot_size, HUD_PANEL);
        append_ui_outline(list, &ui, x, y, slot_size, slot_size,
                          layout->ui_scale, HUD_SHADOW);
        if (slot == hud->hotbar_selected)
            append_ui_outline(list, &ui, x, y - 2 * layout->ui_scale,
                              slot_size,
                              slot_size + 2 * layout->ui_scale,
                              2 * layout->ui_scale, HUD_SELECT);
        append_ui_item(list, &ui, x + padding, y + padding,
                       slot_size - 2 * padding,
                       hud->hotbar_items[slot]);
        append_ui_count(list, &ui, x + slot_size - padding,
                        y + slot_size - 7 * layout->ui_scale,
                        hud->hotbar_counts[slot], layout->ui_scale);
    }

    append_ui_button(list, &ui, layout->jump_x, layout->jump_y,
                     layout->jump_r, HUD_BTN_FACE, hud->jump_held);
    append_ui_arrow(list, &ui, layout->jump_x, layout->jump_y - 3 * s, 0);
    append_ui_button(list, &ui, layout->action_x, layout->action_y,
                     layout->action_r, HUD_BTN_FACE,
                     (uint8_t)(hud->action_held || hud->action_mode != 0));
    if (hud->action_mode == 2) {
        append_ui_rect(list, &ui, layout->action_x - 7 * s,
                       layout->action_y - 5 * s, 15 * s, 3 * s, HUD_TEXT);
        append_ui_rect(list, &ui, layout->action_x - 6 * s,
                       layout->action_y - 2 * s, 3 * s, 7 * s, HUD_TEXT);
        append_ui_rect(list, &ui, layout->action_x + 4 * s,
                       layout->action_y - 2 * s, 3 * s, 7 * s, HUD_TEXT);
    } else {
        append_ui_rect(list, &ui, layout->action_x - 2 * s,
                       layout->action_y - 6 * s, 3 * s, 14 * s, HUD_TEXT);
        append_ui_rect(list, &ui, layout->action_x - 8 * s,
                       layout->action_y - 4 * s, 14 * s, 3 * s, HUD_TEXT);
    }
    append_ui_button(list, &ui, layout->place_x, layout->place_y,
                     layout->place_r, HUD_BTN_FACE, 0);
    append_ui_rect(list, &ui, layout->place_x - 7 * s,
                   layout->place_y - s, 15 * s, 3 * s, HUD_TEXT);
    append_ui_rect(list, &ui, layout->place_x - s,
                   layout->place_y - 7 * s, 3 * s, 15 * s, HUD_TEXT);
    if (hud->flying) {
        append_ui_button(list, &ui, layout->down_x, layout->down_y,
                         layout->down_r, HUD_BTN_FACE, hud->down_held);
        append_ui_arrow(list, &ui, layout->down_x, layout->down_y - 3 * s, 1);
    }
    append_ui_button(list, &ui, layout->fly_x, layout->fly_y, layout->fly_r,
                     HUD_BTN_FACE, hud->flying);
    append_ui_text(list, &ui, layout->fly_x - 3 * s,
                   layout->fly_y - 3 * s, "F", HUD_TEXT, s);
    append_ui_button(list, &ui, layout->menu_x, layout->menu_y,
                     layout->menu_r, HUD_BTN_FACE, 0);
    append_ui_rect(list, &ui, layout->menu_x - 6 * s,
                   layout->menu_y - 5 * s, 13 * s, 2 * s, HUD_TEXT);
    append_ui_rect(list, &ui, layout->menu_x - 6 * s,
                   layout->menu_y - s, 13 * s, 2 * s, HUD_TEXT);
    append_ui_rect(list, &ui, layout->menu_x - 6 * s,
                   layout->menu_y + 3 * s, 13 * s, 2 * s, HUD_TEXT);
    append_ui_button(list, &ui, layout->bag_x, layout->bag_y, layout->bag_r,
                     HUD_BTN_FACE, hud->inventory_open);
    append_ui_outline(list, &ui, layout->bag_x - 6 * s,
                      layout->bag_y - 4 * s, 13 * s, 9 * s, s, HUD_TEXT);
    append_ui_rect(list, &ui, layout->bag_x - 3 * s,
                   layout->bag_y - 6 * s, 7 * s, 2 * s, HUD_TEXT);

    if (hud->move_active) {
        /* Virtual stick: a square ring in place of the Canvas ellipse. */
        const int reach = 36 * s;
        const int cx = hud->move_origin_x;
        const int cy = hud->move_origin_y;
        append_ui_rect(list, &ui, cx - reach, cy - reach, reach * 2, 2 * s,
                       HUD_SHADOW);
        append_ui_rect(list, &ui, cx - reach, cy + reach - 2 * s, reach * 2,
                       2 * s, HUD_SHADOW);
        append_ui_rect(list, &ui, cx - reach, cy - reach, 2 * s, reach * 2,
                       HUD_SHADOW);
        append_ui_rect(list, &ui, cx + reach - 2 * s, cy - reach, 2 * s,
                       reach * 2, HUD_SHADOW);
        append_ui_button(list, &ui, cx + hud->move_dx, cy + hud->move_dy,
                         10 * s, HUD_BTN_FACE, 0);
    }

    if (hud->show_performance) {
        out = status;
        if (ui.width * 3u < (uint32_t)layout->screen_w * 2u) {
            *out++ = 'F';
        } else {
            *out++ = 'G'; *out++ = 'F'; *out++ = 'P'; *out++ = 'S'; *out++ = ' ';
        }
        out = append_u32_text(out, (hud->fps_x10 + 5u) / 10u);
        *out++ = ' ';
        if (ui.width * 3u >= (uint32_t)layout->screen_w * 2u) {
            if (hud->quality_manual) { *out++ = 'M'; *out++ = 'A'; *out++ = 'N'; }
            else { *out++ = 'A'; *out++ = 'U'; *out++ = 'T'; *out++ = 'O'; }
            *out++ = ' ';
        }
        *out++ = (char)('0' + hud->quality);
        *out++ = 'X';
        *out = '\0';
        append_ui_text(list, &ui,
                       ((int)ui.width - ui_text_width(&ui, status, 1)) / 2 *
                           layout->screen_w / ui.width,
                       layout->view_y + 4, status, HUD_TEXT, 1);
    }
    if (hud->mine_progress > 0.01F) {
        int progress = (int)(44.0F * hud->mine_progress);
        if (progress > 44) progress = 44;
        append_ui_rect(list, &ui, center_x - 23, center_y + 13, 46, 7,
                       HUD_SHADOW);
        append_ui_rect(list, &ui, center_x - 22, center_y + 14, progress, 5,
                       HUD_SELECT);
    }
    if (hud->toast != NULL && hud->toast[0] != '\0') {
        const int text_width = raster_text_width(hud->toast, 1);
        const int x = center_x - text_width / 2;
        const int y = layout->hotbar_y - 14;
        append_ui_rect(list, &ui, x - 4, y - 3, text_width + 8, 11,
                       HUD_SHADOW);
        append_ui_text(list, &ui, x, y, hud->toast, HUD_TEXT, 1);
    }
}

/* Button captions for the main menu, pause and settings screens. */
static const char *menu_button_label(const menu_state_t *menu, int index,
                                     char *dynamic) {
    if (menu->screen == 0) {
        static const char *const labels[] = {"NEW GAME", "LOAD SAVE",
                                             "SETTINGS"};
        return labels[index];
    }
    if (menu->screen == 2) {
        static const char *const labels[] = {"RESUME", "SAVE GAME",
                                             "SETTINGS", "MAIN MENU"};
        return labels[index];
    }
    if (index == 0) {
        char *out = dynamic;
        *out++ = 'Q';
        *out++ = 'U';
        *out++ = 'A';
        *out++ = 'L';
        *out++ = 'I';
        *out++ = 'T';
        *out++ = 'Y';
        *out++ = ':';
        *out++ = ' ';
        if (menu->quality_manual == 0) {
            *out++ = 'A';
            *out++ = 'U';
            *out++ = 'T';
            *out++ = 'O';
        } else {
            *out++ = (char)('0' + menu->quality);
            *out++ = 'X';
        }
        *out = '\0';
        return dynamic;
    }
    if (index == 1)
        return menu->show_performance ? "PERFORMANCE: ON" : "PERFORMANCE: OFF";
    if (index == 2) return "SAVE GAME";
    if (index == 3) return "DELETE SAVE";
    return "BACK";
}

/* Menus are drawn into the raster draw list at the scene resolution, so the
 * main menu, pause and settings screens do not need a Canvas overlay. */
static void append_menu(pxa_raster_draw_list_t *list, const menu_state_t *menu,
                        uint16_t width, uint16_t height) {
    raster_ui_t ui;
    const render_layout_t *layout = &g_layout;
    const int s = layout->ui_scale;
    const int body_scale = s;
    const int title_scale = 2 * s;
    int center_x;
    int index;
    char label[32];
    if (menu == NULL) return;
    render_menu_layout(menu);
    center_x = layout->view_x + layout->view_w / 2;
    ui.width = width;
    ui.height = height;
    ui.layout = layout;
    if (!menu->overlay) {
        append_ui_rect(list, &ui, 0, 0, layout->screen_w, layout->screen_h,
                       MENU_BG);
    }
    {
        const char *title = menu->screen == 2   ? "PAUSED"
                            : menu->screen == 1 ? "SETTINGS"
                                                : "VOXEL CRAFT";
        const int title_width = ui_text_width(&ui, title, title_scale);
        const int title_x = center_x - title_width / 2;
        const int title_y = layout->view_y + 18 * s;
        if (menu->overlay) {
            append_ui_rect(list, &ui, title_x - 8 * s, title_y - 4 * s,
                           title_width + 16 * s, 5 * title_scale + 8 * s,
                           HUD_SHADOW);
        }
        append_ui_text(list, &ui, title_x, title_y, title, HUD_SELECT,
                       title_scale);
    }
    if (menu->screen == 0) {
        char *out = label;
        char *end;
        *out++ = 'S';
        *out++ = 'E';
        *out++ = 'E';
        *out++ = 'D';
        *out++ = ' ';
        end = append_u32_text(out, menu->seed);
        *end = '\0';
        append_ui_text(list, &ui,
                       center_x - ui_text_width(&ui, label, body_scale) / 2,
                       layout->view_y + 54 * s, label, HUD_MUTED, body_scale);
    }
    for (index = 0; index < g_menu_button_count; ++index) {
        const menu_button_t *button = &g_menu_buttons[index];
        const char *text = menu_button_label(menu, index, label);
        const int text_width = ui_text_width(&ui, text, body_scale);
        const uint16_t face =
            button->enabled ? HUD_BTN_FACE : shade565(HUD_BTN_FACE, -45);
        const uint16_t ink = button->enabled ? HUD_TEXT : HUD_MUTED;
        /* Offset shadow plus a flat face and an accent outline: no highlight
         * band on the surface. */
        append_ui_rect(list, &ui, button->x + 2 * s, button->y + 3 * s,
                       button->w, button->h, shade565(face, -62));
        append_ui_rect(list, &ui, button->x, button->y, button->w,
                       button->h, face);
        append_ui_outline(list, &ui, button->x, button->y, button->w,
                          button->h, 2 * s,
                          button->enabled ? HUD_SELECT : HUD_SHADOW);
        append_ui_text(list, &ui,
                       button->x + (button->w - text_width) / 2,
                       button->y + (button->h - 5 * body_scale) / 2, text,
                       ink, body_scale);
    }
    if (menu->screen == 0) {
        static const char hint[] = "TAP TO SELECT";
        append_ui_text(list, &ui,
                       center_x - ui_text_width(&ui, hint, body_scale) / 2,
                       layout->view_y + layout->view_h - 26 * s, hint,
                       HUD_MUTED, body_scale);
    }
    if (menu->toast != NULL && menu->toast[0] != '\0') {
        const int text_width = ui_text_width(&ui, menu->toast, body_scale);
        const int x = center_x - text_width / 2;
        const int y = layout->view_y + layout->view_h - 28 * s;
        append_ui_rect(list, &ui, x - 4 * s, y - 3 * s, text_width + 8 * s,
                       5 * body_scale + 6 * s, HUD_SHADOW);
        append_ui_text(list, &ui, x, y, menu->toast, HUD_TEXT, body_scale);
    }
}

static void append_outline_line(pxa_raster_draw_list_t *list, float x0,
                                float y0, float x1, float y1) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = rc_sqrt(dx * dx + dy * dy);
    float nx;
    float ny;
    int16_t xy[8];
    if (length < 0.5F) return;
    nx = -dy / length * 0.55F;
    ny = dx / length * 0.55F;
    xy[0] = (int16_t)((x0 + nx) * 16.0F);
    xy[1] = (int16_t)((y0 + ny) * 16.0F);
    xy[2] = (int16_t)((x1 + nx) * 16.0F);
    xy[3] = (int16_t)((y1 + ny) * 16.0F);
    xy[4] = (int16_t)((x1 - nx) * 16.0F);
    xy[5] = (int16_t)((y1 - ny) * 16.0F);
    xy[6] = (int16_t)((x0 - nx) * 16.0F);
    xy[7] = (int16_t)((y0 - ny) * 16.0F);
    (void)pxa_raster_flat_quad(list, xy, UINT16_C(0x0000));
}

/* The targeted block outline. Host flat quads have no depth, so only the
 * faces that point at the camera are outlined; that reads as a solid 3D box
 * instead of a see-through wireframe. */
static void append_block_outline(pxa_raster_draw_list_t *list,
                                 const raster_camera_t *camera,
                                 const ray_hit_t *target) {
    static const uint8_t kFaceCorners[6][4] = {
        {0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4},
        {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6},
    };
    float screen_x[8];
    float screen_y[8];
    uint8_t projectable[8];
    uint8_t index;
    float bx;
    float by;
    float bz;
    if (target == NULL || !target->hit) return;
    bx = (float)target->x;
    by = (float)target->y;
    bz = (float)target->z;
    for (index = 0; index < 8; ++index) {
        const float wx = bx + (float)(index & 1u);
        const float wy = by + (float)((index >> 1) & 1u);
        const float wz = bz + (float)((index >> 2) & 1u);
        const float dx = wx - camera->x;
        const float dy = wy - camera->y;
        const float dz = wz - camera->z;
        const float depth = dx * camera->fx + dy * camera->fy + dz * camera->fz;
        const float right = dx * camera->rx + dz * camera->rz;
        const float up = dx * camera->ux + dy * camera->uy + dz * camera->uz;
        if (depth < VOXEL_RASTER_NEAR) {
            projectable[index] = 0;
            continue;
        }
        projectable[index] = 1;
        screen_x[index] = camera->width * 0.5F +
                          right / depth * (camera->width * 0.5F) /
                              camera->tan_x;
        screen_y[index] = camera->height * 0.5F -
                          up / depth * (camera->height * 0.5F) /
                              camera->tan_y;
    }
    for (index = 0; index < 6; ++index) {
        uint8_t visible;
        uint8_t edge;
        switch (index) {
        case 0: visible = camera->x < bx; break;
        case 1: visible = camera->x > bx + 1.0F; break;
        case 2: visible = camera->y < by; break;
        case 3: visible = camera->y > by + 1.0F; break;
        case 4: visible = camera->z < bz; break;
        default: visible = camera->z > bz + 1.0F; break;
        }
        if (!visible) continue;
        for (edge = 0; edge < 4; ++edge) {
            const uint8_t a = kFaceCorners[index][edge];
            const uint8_t b = kFaceCorners[index][(edge + 1u) & 3u];
            if (!projectable[a] || !projectable[b]) continue;
            append_outline_line(list, screen_x[a], screen_y[a], screen_x[b],
                                screen_y[b]);
        }
    }
}

static void append_inv_slot(pxa_raster_draw_list_t *list,
                            const raster_ui_t *ui, int x, int y,
                            const item_stack_t *slot, int highlight) {
    const render_layout_t *layout = ui->layout;
    const int size = layout->hotbar_slot;
    const int pad = 2 * layout->ui_scale;
    append_ui_rect(list, ui, x, y, size, size, HUD_PANEL);
    append_ui_outline(list, ui, x, y, size, size, layout->ui_scale,
                      highlight ? HUD_SELECT : HUD_SHADOW);
    if (slot->item != BLOCK_AIR) {
        append_ui_item(list, ui, x + pad, y + pad, size - 2 * pad, slot->item);
        append_ui_count(list, ui, x + size - pad, y + size - pad, slot->count,
                        layout->ui_scale);
    }
}

/* Inventory and crafting table, drawn at the real scene resolution. */
static void append_inventory(pxa_raster_draw_list_t *list,
                             const hud_state_t *hud, uint16_t width,
                             uint16_t height) {
    raster_ui_t ui;
    const render_layout_t *layout = &g_layout;
    const int s = layout->ui_scale;
    const int slot_size = layout->hotbar_slot;
    const int padding = 2 * s;
    const int craft = hud->craft_table ? 3 : 2;
    int row;
    int column;
    char title[24];
    if (hud == NULL) return;
    ui.width = width;
    ui.height = height;
    ui.layout = layout;
    append_ui_rect(list, &ui, 0, 0, layout->screen_w, layout->screen_h,
                   MENU_BG);
    append_ui_rect(list, &ui, g_inv_layout.panel_x, g_inv_layout.panel_y,
                   g_inv_layout.panel_w, g_inv_layout.panel_h, HUD_PANEL);
    append_ui_outline(list, &ui, g_inv_layout.panel_x, g_inv_layout.panel_y,
                      g_inv_layout.panel_w, g_inv_layout.panel_h, 2 * s,
                      HUD_PANEL_LIGHT);
    {
        const char *label = hud->craft_table ? "CRAFTING" : "INVENTORY";
        static const char hint[] = "TAP MOVE  HOLD SPLIT";
        int index = 0;
        while (label[index] != '\0' && index < (int)sizeof(title) - 1) {
            title[index] = label[index];
            ++index;
        }
        title[index] = '\0';
        append_ui_text(list, &ui,
                       g_inv_layout.panel_x +
                           (g_inv_layout.panel_w -
                            ui_text_width(&ui, title, 2 * s)) /
                               2,
                       g_inv_layout.panel_y + 8 * s, title, HUD_TEXT, 2 * s);
        append_ui_text(list, &ui,
                       g_inv_layout.panel_x +
                           (g_inv_layout.panel_w -
                            ui_text_width(&ui, hint, s)) /
                               2,
                       g_inv_layout.panel_y + 22 * s, hint, HUD_MUTED, s);
    }
    for (row = 0; row < craft; ++row) {
        for (column = 0; column < craft; ++column) {
            const int x = g_inv_layout.craft_x + column * slot_size;
            const int y = g_inv_layout.craft_y + row * slot_size;
            const item_stack_t *slot =
                hud->craft_table ? &g_table_craft[row * craft + column]
                                 : &g_craft[row * craft + column];
            append_inv_slot(list, &ui, x, y, slot, 0);
        }
    }
    {
        const item_stack_t *result =
            hud->craft_table ? &g_table_result : &g_craft_result;
        const int arrow_x = g_inv_layout.craft_x + craft * slot_size + 4 * s;
        const int arrow_y = g_inv_layout.craft_y + (craft * slot_size) / 2;
        append_outline_line(list, (float)ui_x(&ui, arrow_x),
                            (float)ui_y(&ui, arrow_y),
                            (float)ui_x(&ui, arrow_x + 10 * s),
                            (float)ui_y(&ui, arrow_y));
        append_outline_line(list, (float)ui_x(&ui, arrow_x + 6 * s),
                            (float)ui_y(&ui, arrow_y - 4 * s),
                            (float)ui_x(&ui, arrow_x + 10 * s),
                            (float)ui_y(&ui, arrow_y));
        append_outline_line(list, (float)ui_x(&ui, arrow_x + 6 * s),
                            (float)ui_y(&ui, arrow_y + 4 * s),
                            (float)ui_x(&ui, arrow_x + 10 * s),
                            (float)ui_y(&ui, arrow_y));
        append_inv_slot(list, &ui, g_inv_layout.result_x,
                        g_inv_layout.result_y, result,
                        result->item != BLOCK_AIR ? 1 : 0);
    }
    for (row = 0; row < 3; ++row) {
        for (column = 0; column < 9; ++column) {
            append_inv_slot(list, &ui,
                            g_inv_layout.main_x + column * slot_size,
                            g_inv_layout.main_y + row * slot_size,
                            &g_inventory[HOTBAR_SLOTS + row * 9 + column], 0);
        }
    }
    for (column = 0; column < 9; ++column) {
        append_inv_slot(list, &ui,
                        g_inv_layout.inv_hotbar_x + column * slot_size,
                        g_inv_layout.inv_hotbar_y, &g_inventory[column],
                        column == hud->hotbar_selected ? 1 : 0);
    }
    append_outline_line(list, (float)ui_x(&ui, g_inv_layout.close_x - 7 * s),
                        (float)ui_y(&ui, g_inv_layout.close_y - 7 * s),
                        (float)ui_x(&ui, g_inv_layout.close_x + 7 * s),
                        (float)ui_y(&ui, g_inv_layout.close_y + 7 * s));
    append_outline_line(list, (float)ui_x(&ui, g_inv_layout.close_x - 7 * s),
                        (float)ui_y(&ui, g_inv_layout.close_y + 7 * s),
                        (float)ui_x(&ui, g_inv_layout.close_x + 7 * s),
                        (float)ui_y(&ui, g_inv_layout.close_y - 7 * s));
    if (hud->cursor_item != BLOCK_AIR && hud->cursor_count != 0) {
        const int x = hud->pointer_x - slot_size / 2;
        const int y = hud->pointer_y - slot_size - 6 * s;
        append_ui_rect(list, &ui, x, y, slot_size, slot_size, HUD_PANEL);
        append_ui_item(list, &ui, x + padding, y + padding,
                       slot_size - 2 * padding, hud->cursor_item);
        append_ui_count(list, &ui, x + slot_size - padding,
                        y + slot_size - padding, hud->cursor_count, s);
    }
}

int32_t voxel_raster_render(uint32_t surface_handle, uint64_t frame_id,
                            const player_t *player, uint8_t quality,
                            const hud_state_t *hud, const menu_state_t *menu,
                            const ray_hit_t *target) {
    raster_camera_t camera;
    pxa_raster_draw_list_t list;
    visible_chunk_t visible_chunks[GRID_COUNT];
    uint32_t candidate_count = 0;
    uint32_t candidate_limit = quality >= QUALITY_PERFORMANCE
                                   ? VOXEL_RASTER_PERFORMANCE_CANDIDATES
                               : quality >= QUALITY_BALANCED  ? 480u
                                                              : 620u;
    uint32_t mesh_limit;
    uint8_t mesh_build_budget = 1;
    /* World edits invalidate the previous mesh; rebuild those synchronously so
     * a placed or broken block appears at once instead of after the
     * incremental streaming budget finishes the whole chunk. */
    uint8_t edit_build_budget = 3;
    uint8_t visible_count = 0;
    int lit_palette_depth;
    int hybrid_painter;
    int grid_z;
    if (surface_handle == 0 || frame_id == 0 || player == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    g_water_uv_offset_q4 = (int16_t)((frame_id * 2u) & 255u);
    pxa_raster_zero_bytes(&g_stats, sizeof(g_stats));
    if (menu != NULL && !menu->overlay) {
        /* Full-screen menu: no 3D scene behind it, just the UI. */
        const uint16_t width = (uint16_t)render_scene_width();
        const uint16_t height = (uint16_t)render_scene_height();
        pxa_raster_draw_list_begin(&list, g_draw_list, sizeof(g_draw_list),
                                   frame_id);
        (void)pxa_raster_clear(&list, MENU_BG);
        append_menu(&list, menu, width, height);
        g_stats.draw_list_bytes = list.length;
        g_stats.covered_pixel_budget = (uint32_t)width * height;
        return pxa_raster_submit(surface_handle, &list);
    }
    if (hud != NULL && hud->inventory_open) {
        /* The inventory is a full-screen panel; drawing it without the 3D
         * scene keeps it sharp at the real resolution. */
        const uint16_t width = (uint16_t)render_scene_width();
        const uint16_t height = (uint16_t)render_scene_height();
        pxa_raster_draw_list_begin(&list, g_draw_list, sizeof(g_draw_list),
                                   frame_id);
        (void)pxa_raster_clear(&list, MENU_BG);
        append_inventory(&list, hud, width, height);
        g_stats.draw_list_bytes = list.length;
        g_stats.covered_pixel_budget = (uint32_t)width * height;
        return pxa_raster_submit(surface_handle, &list);
    }
    build_camera(&camera, player, quality);
    lit_palette_depth =
        (g_raster_capabilities & PXA_RASTER_CAP_LIT_PALETTE_DEPTH) != 0;
    hybrid_painter =
        lit_palette_depth &&
        (g_raster_capabilities & PXA_RASTER_CAP_PAINTER_POLYGON) != 0;
    /* A full-resolution frame makes both Guest projection and Host fill cost
     * substantially more expensive. Keep near terrain responsive instead of
     * spending the frame budget on distant faces. */
    if (camera.width >= 240u && candidate_limit > 320u)
        candidate_limit = 320u;
    if (g_mesh_cache_warmed && g_building_mesh != NULL) {
        const chunk_t *building_chunk = g_building_mesh->build_chunk;
        if (building_chunk == NULL || !building_chunk->loaded ||
            g_building_mesh->build_revision != building_chunk->revision ||
            g_building_mesh->build_chunk_cx != building_chunk->cx ||
            g_building_mesh->build_chunk_cz != building_chunk->cz) {
            g_building_mesh->building = 0;
            g_building_mesh = NULL;
        } else {
            --mesh_build_budget;
            ++g_stats.mesh_build_passes;
            if (advance_mesh_rebuild(g_building_mesh, building_chunk))
                ++g_stats.rebuilt_chunks;
        }
    }
    for (grid_z = 0; grid_z < GRID_W; ++grid_z) {
        int grid_x;
        for (grid_x = 0; grid_x < GRID_W; ++grid_x) {
            const int mesh_index = grid_z * GRID_W + grid_x;
            const chunk_t *chunk = g_chunk_grid[grid_z][grid_x];
            chunk_mesh_t *mesh;
            if (chunk == NULL || !chunk->loaded || !chunk_visible(&camera, chunk)) {
                ++g_stats.frustum_culled;
                continue;
            }
            mesh = mesh_for_chunk(chunk, (uint8_t)mesh_index);
            if (mesh->chunk != chunk || mesh->revision != chunk->revision ||
                mesh->chunk_cx != chunk->cx || mesh->chunk_cz != chunk->cz) {
                if (!g_mesh_cache_warmed) {
                    rebuild_mesh(mesh, chunk);
                    ++g_stats.rebuilt_chunks;
                } else if (mesh->chunk == chunk && mesh->revision != 0 &&
                           edit_build_budget != 0) {
                    /* An edit invalidated a mesh that was already built:
                     * rebuild it in one go so the change shows this frame. */
                    rebuild_mesh(mesh, chunk);
                    --edit_build_budget;
                    ++g_stats.rebuilt_chunks;
                } else if (mesh->chunk != chunk &&
                           chunk_depth(&camera, chunk) < 28.0F &&
                           edit_build_budget != 0) {
                    /* A chunk streamed in right in front of the player:
                     * build it now instead of over dozens of frames. */
                    rebuild_mesh(mesh, chunk);
                    --edit_build_budget;
                    ++g_stats.rebuilt_chunks;
                } else if (g_building_mesh == NULL &&
                           mesh_build_budget != 0) {
                    begin_mesh_rebuild(mesh, chunk);
                    --mesh_build_budget;
                    ++g_stats.mesh_build_passes;
                    if (advance_mesh_rebuild(mesh, chunk))
                        ++g_stats.rebuilt_chunks;
                }
                /* A revision rebuild keeps the previous mesh visible. A cache
                 * slot reassigned to another chunk has no valid fallback. */
                if (mesh->chunk != chunk || mesh->chunk_cx != chunk->cx ||
                    mesh->chunk_cz != chunk->cz)
                    continue;
            }
            g_stats.cached_quads += mesh->quad_count;
            g_stats.dropped_quads += mesh->dropped;
            visible_chunks[visible_count].chunk = chunk;
            visible_chunks[visible_count].mesh = mesh;
            visible_chunks[visible_count].depth = chunk_depth(&camera, chunk);
            ++visible_count;
        }
    }
    g_mesh_cache_warmed = 1;
    sort_visible_chunks(visible_chunks, visible_count);
    /* Keep space for actors and particles. Without this reservation, a dense
     * terrain mesh consumes the whole transport budget before entities run. */
    mesh_limit = candidate_limit > 20u ? candidate_limit - 20u : candidate_limit;
    for (uint8_t chunk_index = 0;
         chunk_index < visible_count && candidate_count < mesh_limit;
         ++chunk_index) {
        const visible_chunk_t *visible = &visible_chunks[chunk_index];
        uint16_t quad_index;
        for (quad_index = 0; quad_index < visible->mesh->quad_count;
             ++quad_index) {
            if (candidate_count >= mesh_limit ||
                candidate_count >= VOXEL_RASTER_CANDIDATES) {
                ++g_stats.dropped_quads;
                break;
            }
            candidate_count += project_quad(
                &camera, visible->chunk,
                &visible->mesh->quads[quad_index],
                &g_candidates[candidate_count],
                (uint16_t)(mesh_limit - candidate_count),
                (uint8_t)hybrid_painter);
        }
    }
    append_entities(&camera, candidate_limit, &candidate_count);
    g_stats.candidate_quads = candidate_count;
    sort_candidates(
        candidate_count,
        hybrid_painter ||
            (g_raster_capabilities & PXA_RASTER_CAP_FIXED_ALPHA_BLEND) != 0 ||
            (g_raster_capabilities & PXA_RASTER_CAP_TEXTURED_QUAD) == 0);
    pxa_raster_draw_list_begin(&list, g_draw_list, sizeof(g_draw_list), frame_id);
    {
        /* A submerged camera sees a deep water backdrop instead of sky. */
        const int submerged =
            game_block(rc_floor_int(player->x),
                       rc_floor_int(player->y + EYE_HEIGHT),
                       rc_floor_int(player->z)) == BLOCK_WATER;
        const uint16_t clear = submerged ? VOXEL_RASTER_UNDERWATER_CLEAR
                                         : UINT16_C(0x9e5f);
        (void)pxa_raster_clear(&list, clear);
        if (!submerged &&
            (g_raster_capabilities & PXA_RASTER_CAP_FLAT_QUAD) != 0)
            append_sky(&list, &camera, quality < QUALITY_PERFORMANCE);
    }
    {
        uint8_t list_full = 0;
        const uint8_t fixed_blend =
            (g_raster_capabilities & PXA_RASTER_CAP_FIXED_ALPHA_BLEND) != 0;
        const uint8_t passes =
            hybrid_painter ? (fixed_blend ? 3u : 2u)
                           : (fixed_blend ? 2u : 1u);
        /* Keep far geometry in one painter-sorted pass. Near geometry uses
         * depth, but translucent water must follow every opaque/cutout face:
         * blended fragments test depth without writing it, so a later opaque
         * face would otherwise punch an untinted polygon through the water. */
        for (uint8_t pass = 0; pass < passes && !list_full; ++pass) {
            for (uint32_t order = 0; order < candidate_count; ++order) {
                projected_quad_t *quad =
                    &g_candidates[g_sort_order[order]];
                const uint8_t painter =
                    hybrid_painter &&
                    quad->nearest_depth_q8 >= VOXEL_RASTER_HYBRID_DEPTH_Q8;
                uint8_t target_pass;
                int added;
                if (painter)
                    target_pass = 0u;
                else if (fixed_blend && quad->blend_75)
                    target_pass = hybrid_painter ? 2u : 1u;
                else
                    target_pass = hybrid_painter ? 1u : 0u;
                if (pass != target_pass) continue;
                if (!projected_quad_has_area(quad)) {
                    ++g_stats.dropped_quads;
                    continue;
                }
                added = append_projected_quad(
                    &list, quad, painter, (uint8_t)lit_palette_depth);
                if (!added) {
                    ++g_stats.dropped_quads;
                    list_full = 1;
                    break;
                }
                ++g_stats.submitted_quads;
                if (painter)
                    ++g_stats.painter_quads;
                else if ((g_raster_capabilities &
                          PXA_RASTER_CAP_TEXTURED_QUAD) != 0)
                    ++g_stats.depth_quads;
                if (!painter && quad->textured && quad->affine &&
                    (g_raster_capabilities & PXA_RASTER_CAP_AFFINE_UV) != 0)
                    ++g_stats.affine_quads;
            }
        }
    }
    /* Underwater: a two-sided water volume alone still looks like clear air.
     * Add a fullscreen additive blue tint when the eye is inside water so the
     * submersion reads like the ray caster's water fill. */
    if ((g_raster_capabilities & PXA_RASTER_CAP_ADDITIVE_SPRITE) != 0 &&
        game_block(rc_floor_int(player->x),
                   rc_floor_int(player->y + EYE_HEIGHT),
                   rc_floor_int(player->z)) == BLOCK_WATER) {
        (void)pxa_raster_sprite(
            &list, 0,
            PXA_RASTER_SPRITE_SOLID_COLOR | PXA_RASTER_SPRITE_ADDITIVE,
            g_raster_capabilities, 0, 0, camera.width, camera.height, 0, 0, 1,
            1, VOXEL_RASTER_UNDERWATER_TINT);
    }
    if ((g_raster_capabilities & PXA_RASTER_CAP_FLAT_QUAD) != 0 &&
        menu == NULL)
        append_block_outline(&list, &camera, target);
    if (menu != NULL)
        append_menu(&list, menu, camera.width, camera.height);
    else
        append_hud(&list, hud, camera.width, camera.height);
    g_stats.draw_list_bytes = list.length;
    g_stats.covered_pixel_budget = (uint32_t)camera.width * camera.height;
    return pxa_raster_submit(surface_handle, &list);
}

void voxel_raster_get_stats(voxel_raster_stats_t *stats) {
    if (stats != NULL) *stats = g_stats;
}

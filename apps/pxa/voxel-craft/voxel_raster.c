#include "voxel_raster.h"

#include <stddef.h>

#include "pxa_raster.h"
#include "rc_math.h"

#define VOXEL_MESH_QUADS_PER_CHUNK 256u
#define VOXEL_RASTER_CANDIDATES 640u
#define VOXEL_RASTER_TEXTURE_SLOTS 15u
#define VOXEL_RASTER_FONT_SLOT 15u
#define VOXEL_RASTER_FONT_GLYPHS 42u
#define VOXEL_RASTER_FONT_WIDTH (VOXEL_RASTER_FONT_GLYPHS * 4u)
#define VOXEL_RASTER_NEAR 0.08F
#define VOXEL_RASTER_TAN_HALF 0.70F
#define VOXEL_RASTER_PARTICLE_LIMIT 32u

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
    uint32_t revision;
    uint16_t quad_count;
    uint16_t dropped;
    mesh_quad_t quads[VOXEL_MESH_QUADS_PER_CHUNK];
} chunk_mesh_t;

typedef struct {
    pxa_raster_vertex_t vertices[4];
    float depth;
    uint16_t color;
    uint8_t texture_slot;
    uint8_t textured;
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

static chunk_mesh_t g_meshes[GRID_COUNT];
static projected_quad_t g_candidates[VOXEL_RASTER_CANDIDATES];
static uint8_t g_draw_list[PXA_RASTER_MAX_DRAW_BYTES];
static uint8_t g_upload[PXA_RASTER_UPLOAD_HEADER_BYTES +
                        VOXEL_RASTER_FONT_WIDTH * 5u];
static uint16_t g_palette[256];
static uint8_t g_texture[16 * 16];
static uint8_t g_font_texture[VOXEL_RASTER_FONT_WIDTH * 5u];
static voxel_raster_stats_t g_stats;
static uint32_t g_raster_capabilities;

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

static int block_occludes(int block) { return block != BLOCK_AIR; }

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((uint16_t)(red >> 3) << 11) |
                      ((uint16_t)(green >> 2) << 5) | (blue >> 3));
}

void voxel_raster_set_capabilities(uint32_t surface_state_flags) {
    g_raster_capabilities = PXA_RASTER_CAP_FLAT_QUAD;
    if ((surface_state_flags &
         PXA_SURFACE_STATE_FLAG_RASTER_TEXTURED_QUAD) != 0)
        g_raster_capabilities |= PXA_RASTER_CAP_TEXTURED_QUAD;
    if ((surface_state_flags &
         PXA_SURFACE_STATE_FLAG_RASTER_ADDITIVE_SPRITE) != 0)
        g_raster_capabilities |= PXA_RASTER_CAP_ADDITIVE_SPRITE;
}

static uint8_t rgb565_to_rgb332(uint16_t color) {
    return (uint8_t)((((color >> 11) & 31u) >> 2) << 5 |
                     (((color >> 5) & 63u) >> 3) << 2 |
                     ((color & 31u) >> 3));
}

static uint16_t rgb332_to_rgb565(uint8_t color) {
    const uint16_t red = (uint16_t)((color >> 5) & 7u);
    const uint16_t green = (uint16_t)((color >> 2) & 7u);
    const uint16_t blue = (uint16_t)(color & 3u);
    return (uint16_t)(((red * 31u / 7u) << 11) |
                      ((green * 63u / 7u) << 5) |
                      (blue * 31u / 3u));
}

static uint16_t vary_rgb565(uint16_t color, int delta) {
    int red = (int)((color >> 11) & 31u) + delta;
    int green = (int)((color >> 5) & 63u) + delta * 2;
    int blue = (int)(color & 31u) + delta;
    if (red < 0) red = 0;
    if (red > 31) red = 31;
    if (green < 0) green = 0;
    if (green > 63) green = 63;
    if (blue < 0) blue = 0;
    if (blue > 31) blue = 31;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

int voxel_raster_upload_assets(uint32_t surface_handle) {
    uint16_t index;
    uint8_t slot;
    int32_t result;
    for (index = 0; index < 256; ++index)
        g_palette[index] = rgb332_to_rgb565((uint8_t)index);
    result = pxa_raster_upload_palette_rgb565(
        surface_handle, g_palette, g_upload, sizeof(g_upload));
    if (result != (int32_t)(PXA_RASTER_UPLOAD_HEADER_BYTES + 512u)) return 0;
    for (slot = 0; slot < VOXEL_RASTER_TEXTURE_SLOTS; ++slot) {
        const int block = slot + 1;
        const uint16_t base = render_block_color(block);
        uint16_t pixel;
        for (pixel = 0; pixel < 256; ++pixel) {
            const int x = pixel & 15;
            const int y = pixel >> 4;
            const int pattern = ((x * 13 + y * 7 + block * 11) & 7) - 3;
            int delta = pattern / 2;
            if (block == BLOCK_BRICK && (y % 5 == 0 ||
                                         (x + ((y / 5) & 1) * 4) % 8 == 0))
                delta = -4;
            if (block == BLOCK_WOOD && (x % 5 == 0)) delta = -3;
            if (block == BLOCK_GLASS && (x == 0 || y == 0 || x == 15 || y == 15))
                delta = 5;
            g_texture[pixel] = rgb565_to_rgb332(vary_rgb565(base, delta));
        }
        result = pxa_raster_upload_texture_index8(
            surface_handle, slot, 16, 16, g_texture, g_upload,
            sizeof(g_upload));
        if (result != (int32_t)(PXA_RASTER_UPLOAD_HEADER_BYTES + 256u))
            return 0;
    }
    pxa_raster_zero_bytes(g_font_texture, sizeof(g_font_texture));
    for (slot = 0; slot < VOXEL_RASTER_FONT_GLYPHS; ++slot) {
        uint8_t row;
        for (row = 0; row < 5; ++row) {
            uint8_t column;
            for (column = 0; column < 3; ++column) {
                if ((kFontRows[slot][row] & (4u >> column)) != 0)
                    g_font_texture[(size_t)row * VOXEL_RASTER_FONT_WIDTH +
                                   (size_t)slot * 4u + column] = 255;
            }
        }
    }
    result = pxa_raster_upload_texture_index8(
        surface_handle, VOXEL_RASTER_FONT_SLOT, VOXEL_RASTER_FONT_WIDTH, 5,
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
    if (mesh->quad_count >= VOXEL_MESH_QUADS_PER_CHUNK) {
        ++mesh->dropped;
        return;
    }
    quad = &mesh->quads[mesh->quad_count++];
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

static void build_axis_faces(chunk_mesh_t *mesh, const chunk_t *chunk,
                             int axis, int sign) {
    uint8_t mask[CHUNK_SIZE * CHUNK_HEIGHT];
    const int axis_length = axis == 1 ? CHUNK_HEIGHT : CHUNK_SIZE;
    const int u_length = CHUNK_SIZE;
    const int v_length = axis == 1 ? CHUNK_SIZE : CHUNK_HEIGHT;
    int slice;
    for (slice = 0; slice < axis_length; ++slice) {
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
                if (block != BLOCK_AIR &&
                    !block_occludes(neighbor_block(chunk, x, y, z)))
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
                append_mesh_quad(mesh, axis, sign, slice, u, v, width,
                                 height, block);
                for (row = 0; row < height; ++row)
                    for (column = 0; column < width; ++column)
                        mask[(v + row) * u_length + u + column] = 0;
                u += width;
            }
        }
    }
}

static void rebuild_mesh(chunk_mesh_t *mesh, const chunk_t *chunk) {
    int axis;
    pxa_raster_zero_bytes(mesh, sizeof(*mesh));
    mesh->chunk = chunk;
    mesh->revision = chunk->revision;
    for (axis = 0; axis < 3; ++axis) {
        build_axis_faces(mesh, chunk, axis, -1);
        build_axis_faces(mesh, chunk, axis, 1);
    }
}

void voxel_raster_reset(void) {
    pxa_raster_zero_bytes(g_meshes, sizeof(g_meshes));
    pxa_raster_zero_bytes(&g_stats, sizeof(g_stats));
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
    camera->fog_end = quality >= QUALITY_PERFORMANCE ? 30.0F
                      : quality >= QUALITY_BALANCED  ? 38.0F
                                                     : 46.0F;
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

static int project_quad(const raster_camera_t *camera, const chunk_t *chunk,
                        const mesh_quad_t *quad, projected_quad_t *projected) {
    float corners[4][3];
    float center[3] = {0};
    float min_x = 1.0e9F;
    float min_y = 1.0e9F;
    float max_x = -1.0e9F;
    float max_y = -1.0e9F;
    float normal_dot;
    float depth_sum = 0.0F;
    uint8_t light;
    uint8_t index;
    quad_corners(chunk, quad, corners);
    for (index = 0; index < 4; ++index) {
        center[0] += corners[index][0] * 0.25F;
        center[1] += corners[index][1] * 0.25F;
        center[2] += corners[index][2] * 0.25F;
    }
    normal_dot = quad->sign *
        (quad->axis == 0 ? camera->x - center[0]
         : quad->axis == 1 ? camera->y - center[1]
                           : camera->z - center[2]);
    if (normal_dot <= 0.0F) {
        ++g_stats.backface_culled;
        return 0;
    }
    for (index = 0; index < 4; ++index) {
        const float dx = corners[index][0] - camera->x;
        const float dy = corners[index][1] - camera->y;
        const float dz = corners[index][2] - camera->z;
        const float depth = dx * camera->fx + dy * camera->fy + dz * camera->fz;
        float screen_x;
        float screen_y;
        float right;
        float up;
        if (depth < VOXEL_RASTER_NEAR) {
            ++g_stats.frustum_culled;
            return 0;
        }
        right = dx * camera->rx + dz * camera->rz;
        up = dx * camera->ux + dy * camera->uy + dz * camera->uz;
        screen_x = camera->width * 0.5F +
                   right / depth * (camera->width * 0.5F) / camera->tan_x;
        screen_y = camera->height * 0.5F -
                   up / depth * (camera->height * 0.5F) / camera->tan_y;
        projected->vertices[index].x_q4 = (int16_t)(screen_x * 16.0F);
        projected->vertices[index].y_q4 = (int16_t)(screen_y * 16.0F);
        depth_sum += depth;
        if (screen_x < min_x) min_x = screen_x;
        if (screen_x > max_x) max_x = screen_x;
        if (screen_y < min_y) min_y = screen_y;
        if (screen_y > max_y) max_y = screen_y;
    }
    if (max_x < 0.0F || max_y < 0.0F || min_x >= camera->width ||
        min_y >= camera->height || depth_sum * 0.25F > camera->fog_end) {
        ++g_stats.frustum_culled;
        return 0;
    }
    projected->depth = depth_sum * 0.25F;
    light = quad->axis == 1 ? (quad->sign > 0 ? 255u : 116u)
                            : (quad->axis == 0 ? 190u : 160u);
    if (projected->depth > 14.0F) {
        float fog = (projected->depth - 14.0F) /
                    (camera->fog_end - 14.0F);
        if (fog > 1.0F) fog = 1.0F;
        light = (uint8_t)((float)light * (1.0F - fog * 0.65F));
    }
    projected->vertices[0].u_q4 = 0;
    projected->vertices[0].v_q4 = 0;
    projected->vertices[1].u_q4 = (int16_t)(quad->u_length * 256u);
    projected->vertices[1].v_q4 = 0;
    projected->vertices[2].u_q4 = (int16_t)(quad->u_length * 256u);
    projected->vertices[2].v_q4 = (int16_t)(quad->v_length * 256u);
    projected->vertices[3].u_q4 = 0;
    projected->vertices[3].v_q4 = (int16_t)(quad->v_length * 256u);
    for (index = 0; index < 4; ++index)
        projected->vertices[index].light = light;
    projected->textured = quad->block <= VOXEL_RASTER_TEXTURE_SLOTS;
    projected->texture_slot = (uint8_t)(quad->block - 1u);
    projected->color = render_block_color(quad->block);
    return 1;
}

static int project_billboard(const raster_camera_t *camera, float x, float y,
                             float z, float width, float height,
                             uint16_t color, projected_quad_t *projected) {
    const float half_width = width * 0.5F;
    float corners[4][3];
    float depth_sum = 0.0F;
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
        depth_sum += depth;
        if (screen_x < min_x) min_x = screen_x;
        if (screen_x > max_x) max_x = screen_x;
        if (screen_y < min_y) min_y = screen_y;
        if (screen_y > max_y) max_y = screen_y;
    }
    if (max_x < 0.0F || max_y < 0.0F || min_x >= camera->width ||
        min_y >= camera->height)
        return 0;
    projected->depth = depth_sum * 0.25F;
    projected->color = color;
    projected->texture_slot = 0;
    projected->textured = 0;
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

static void sort_candidates(uint32_t count) {
    uint32_t gap;
    for (gap = count / 2u; gap != 0; gap /= 2u) {
        uint32_t index;
        for (index = gap; index < count; ++index) {
            projected_quad_t value = g_candidates[index];
            uint32_t cursor = index;
            while (cursor >= gap &&
                   g_candidates[cursor - gap].depth < value.depth) {
                g_candidates[cursor] = g_candidates[cursor - gap];
                cursor -= gap;
            }
            g_candidates[cursor] = value;
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

static void append_text(pxa_raster_draw_list_t *list, int x, int y,
                        const char *text, uint16_t color, int scale) {
    while (text != NULL && *text != '\0') {
        const int glyph = font_character_index(*text++);
        if (glyph != VOXEL_RASTER_FONT_GLYPHS - 1u)
            (void)pxa_raster_sprite(
                list, VOXEL_RASTER_FONT_SLOT,
                PXA_RASTER_SPRITE_TRANSPARENT_INDEX0 |
                    PXA_RASTER_SPRITE_SOLID_COLOR,
                g_raster_capabilities, (int16_t)x, (int16_t)y,
                (uint16_t)(3 * scale), (uint16_t)(5 * scale),
                (uint16_t)(glyph * 4), 0, 3, 5, color);
        x += 4 * scale;
    }
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

static void append_hud(pxa_raster_draw_list_t *list, const hud_state_t *hud,
                       uint16_t width, uint16_t height) {
    int16_t quad[8];
    char status[20];
    char *out = status;
    int slot;
    const int center_x = width / 2;
    const int center_y = height / 2;
    const int font_scale = width >= 240 ? 2 : 1;
    const int slot_size = width >= 240 ? 22 : (width >= 120 ? 11 : 6);
    const int hotbar_x = ((int)width - HOTBAR_SLOTS * slot_size) / 2;
    quad[0] = (int16_t)((center_x - 4) * 16);
    quad[1] = (int16_t)(center_y * 16);
    quad[2] = (int16_t)((center_x + 5) * 16);
    quad[3] = quad[1];
    quad[4] = quad[2];
    quad[5] = (int16_t)((center_y + 1) * 16);
    quad[6] = quad[0];
    quad[7] = quad[5];
    (void)pxa_raster_flat_quad(list, quad, UINT16_C(0xffff));
    quad[0] = (int16_t)(center_x * 16);
    quad[1] = (int16_t)((center_y - 4) * 16);
    quad[2] = (int16_t)((center_x + 1) * 16);
    quad[3] = quad[1];
    quad[4] = quad[2];
    quad[5] = (int16_t)((center_y + 5) * 16);
    quad[6] = quad[0];
    quad[7] = quad[5];
    (void)pxa_raster_flat_quad(list, quad, UINT16_C(0xffff));
    for (slot = 0; slot < HOTBAR_SLOTS; ++slot) {
        append_rect(list, hotbar_x + slot * slot_size,
                    height - slot_size - 3, slot_size - 1, slot_size - 1,
                    hud != NULL && slot == hud->hotbar_selected
                        ? UINT16_C(0xfec8)
                        : UINT16_C(0x2945));
    }
    if (hud == NULL) return;
    *out++ = 'F';
    *out++ = 'P';
    *out++ = 'S';
    *out++ = ' ';
    out = append_u32_text(out, hud->fps_x10 / 10u);
    *out++ = ' ';
    *out++ = 'Q';
    out = append_u32_text(out, hud->quality);
    *out = '\0';
    append_text(list, ((int)width - raster_text_width(status, font_scale)) / 2,
                2, status, UINT16_C(0xffff), font_scale);
    append_rect(list, width - 13, height - 25, 11, 11, UINT16_C(0x2945));
    append_text(list, width - 9, height - 22, "+", UINT16_C(0xffff), 1);
    append_rect(list, width - 13, height - 44, 11, 11,
                hud->action_held ? UINT16_C(0xfec8) : UINT16_C(0x2945));
    append_text(list, width - 9, height - 41,
                hud->action_mode == 2 ? "U" : "A", UINT16_C(0xffff), 1);
    append_rect(list, width - 13, height - 63, 11, 11, UINT16_C(0x2945));
    append_text(list, width - 9, height - 60, "P", UINT16_C(0xffff), 1);
    append_text(list, width - 39, 3, "M B F", UINT16_C(0xffff), 1);
    if (hud->mine_progress > 0.01F) {
        int progress = (int)(hud->mine_progress * 22.0F);
        if (progress > 22) progress = 22;
        append_rect(list, center_x - 12, center_y + 7, 24, 4,
                    UINT16_C(0x2945));
        append_rect(list, center_x - 11, center_y + 8, progress, 2,
                    UINT16_C(0xfec8));
    }
    if (hud->toast != NULL) {
        const int text_width = raster_text_width(hud->toast, 1);
        append_text(list, ((int)width - text_width) / 2, center_y + 14,
                    hud->toast, UINT16_C(0xfec8), 1);
    }
}

int32_t voxel_raster_render(uint32_t surface_handle, uint64_t frame_id,
                            const player_t *player, uint8_t quality,
                            const hud_state_t *hud) {
    raster_camera_t camera;
    pxa_raster_draw_list_t list;
    uint32_t candidate_count = 0;
    uint32_t candidate_limit = quality >= QUALITY_PERFORMANCE ? 320u
                               : quality >= QUALITY_BALANCED  ? 480u
                                                              : 620u;
    int grid_z;
    if (surface_handle == 0 || frame_id == 0 || player == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    pxa_raster_zero_bytes(&g_stats, sizeof(g_stats));
    build_camera(&camera, player, quality);
    append_entities(&camera, candidate_limit, &candidate_count);
    for (grid_z = 0; grid_z < GRID_W; ++grid_z) {
        int grid_x;
        for (grid_x = 0; grid_x < GRID_W; ++grid_x) {
            const int mesh_index = grid_z * GRID_W + grid_x;
            const chunk_t *chunk = g_chunk_grid[grid_z][grid_x];
            chunk_mesh_t *mesh = &g_meshes[mesh_index];
            uint16_t quad_index;
            if (chunk == NULL || !chunk->loaded || !chunk_visible(&camera, chunk)) {
                ++g_stats.frustum_culled;
                continue;
            }
            if (mesh->chunk != chunk || mesh->revision != chunk->revision)
                rebuild_mesh(mesh, chunk);
            g_stats.cached_quads += mesh->quad_count;
            g_stats.dropped_quads += mesh->dropped;
            for (quad_index = 0; quad_index < mesh->quad_count; ++quad_index) {
                if (candidate_count >= candidate_limit ||
                    candidate_count >= VOXEL_RASTER_CANDIDATES) {
                    ++g_stats.dropped_quads;
                    continue;
                }
                if (project_quad(&camera, chunk, &mesh->quads[quad_index],
                                 &g_candidates[candidate_count]))
                    ++candidate_count;
            }
        }
    }
    g_stats.candidate_quads = candidate_count;
    sort_candidates(candidate_count);
    pxa_raster_draw_list_begin(&list, g_draw_list, sizeof(g_draw_list), frame_id);
    (void)pxa_raster_clear(&list, UINT16_C(0x9e5f));
    for (uint32_t index = 0; index < candidate_count; ++index) {
        projected_quad_t *quad = &g_candidates[index];
        int added;
        if (!projected_quad_has_area(quad)) {
            ++g_stats.dropped_quads;
            continue;
        }
        if (quad->textured &&
            (g_raster_capabilities & PXA_RASTER_CAP_TEXTURED_QUAD) != 0) {
            added = pxa_raster_textured_quad(&list, quad->vertices,
                                              quad->texture_slot);
        } else {
            int16_t xy[8];
            uint8_t corner;
            for (corner = 0; corner < 4; ++corner) {
                xy[corner * 2u] = quad->vertices[corner].x_q4;
                xy[corner * 2u + 1u] = quad->vertices[corner].y_q4;
            }
            added = pxa_raster_flat_quad(&list, xy, quad->color);
        }
        if (!added) {
            g_stats.dropped_quads += candidate_count - index;
            break;
        }
        ++g_stats.submitted_quads;
    }
    append_hud(&list, hud, camera.width, camera.height);
    g_stats.draw_list_bytes = list.length;
    g_stats.covered_pixel_budget = (uint32_t)camera.width * camera.height;
    return pxa_raster_submit(surface_handle, &list);
}

void voxel_raster_get_stats(voxel_raster_stats_t *stats) {
    if (stats != NULL) *stats = g_stats;
}

#include "voxel_raster.h"

#include <stddef.h>
#include <stdint.h>

#include "block_textures.h"
#include "pxa_raster.h"
#include "rc_math.h"

#define VOXEL_MESH_QUADS_PER_CHUNK 512u
#define VOXEL_MESH_MAX_SPAN 16
#define VOXEL_RASTER_CANDIDATES 640u
#define VOXEL_RASTER_TEXTURE_SLOTS 15u
#define VOXEL_RASTER_FONT_SLOT 15u
#define VOXEL_RASTER_FONT_GLYPHS 42u
#define VOXEL_RASTER_FONT_WIDTH (VOXEL_RASTER_FONT_GLYPHS * 4u)
#define VOXEL_RASTER_NEAR 0.08F
#define VOXEL_RASTER_TAN_HALF 0.70F
#define VOXEL_RASTER_PARTICLE_LIMIT 32u

#define HUD_PANEL UINT16_C(0x2945)
#define HUD_PANEL_LIGHT UINT16_C(0x5aeb)
#define HUD_SHADOW UINT16_C(0x18c3)
#define HUD_TEXT UINT16_C(0xffff)
#define HUD_SELECT UINT16_C(0xfec8)
#define HUD_GREEN UINT16_C(0x5eea)

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
    uint8_t affine;
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
static projected_quad_t g_candidates[VOXEL_RASTER_CANDIDATES];
static uint16_t g_sort_order[VOXEL_RASTER_CANDIDATES];
static uint8_t g_draw_list[PXA_RASTER_MAX_DRAW_BYTES];
static uint8_t g_upload[PXA_RASTER_UPLOAD_HEADER_BYTES +
                        VOXEL_RASTER_FONT_WIDTH * 5u];
/* Three 16x16 face tiles stacked into one 16x48 atlas: top, side, bottom. */
static uint8_t g_texture[16 * 48];
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

void voxel_raster_set_capabilities(uint32_t capabilities) {
    g_raster_capabilities = capabilities;
}

int voxel_raster_upload_assets(uint32_t surface_handle) {
    const block_index_set_t *indices = block_texture_indices();
    const uint16_t *palette = block_texture_palette();
    uint8_t slot;
    int32_t result;
    result = pxa_raster_upload_palette_rgb565(
        surface_handle, palette, g_upload, sizeof(g_upload));
    if (result != (int32_t)(PXA_RASTER_UPLOAD_HEADER_BYTES + 512u)) return 0;
    for (slot = 0; slot < VOXEL_RASTER_TEXTURE_SLOTS; ++slot) {
        const int block = slot + 1;
        uint8_t kind;
        for (kind = 0; kind < 3; ++kind) {
            const uint8_t *tile = indices[block][kind];
            uint16_t pixel;
            for (pixel = 0; pixel < 256; ++pixel)
                g_texture[kind * 256u + pixel] = tile[pixel];
        }
        result = pxa_raster_upload_texture_index8(
            surface_handle, slot, 16, 48, g_texture, g_upload,
            sizeof(g_upload));
        if (result != (int32_t)(PXA_RASTER_UPLOAD_HEADER_BYTES + 768u))
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
    }
    projected->depth = depth;
    projected->textured = quad->block <= VOXEL_RASTER_TEXTURE_SLOTS;
    projected->texture_slot = (uint8_t)(quad->block - 1u);
    projected->color = render_block_color(quad->block);
    projected->affine = 0;
    if (projected->textured) {
        /* Faces below roughly 3x3 screen pixels skip the texture and use the
         * block colour; the texel detail is invisible at that size. */
        if (max_x - min_x < 3 * 16 || max_y - min_y < 3 * 16) {
            projected->textured = 0;
            return;
        }
        /* Affine UV is only worth its warping when the depth range across the
         * face stays small relative to its merged texel span. The bound keeps
         * the worst-case affine error near a quarter texel. */
        {
            const uint8_t span = quad->u_length > quad->v_length
                                     ? quad->u_length
                                     : quad->v_length;
            if (min_depth != 0 &&
                (uint32_t)(max_depth - min_depth) * 16u * span <= min_depth)
                projected->affine = 1;
        }
    }
}

static uint8_t project_quad(const raster_camera_t *camera,
                            const chunk_t *chunk, const mesh_quad_t *quad,
                            projected_quad_t *output, uint16_t capacity) {
    float corners[4][3];
    clip_vertex_t clip_a[VOXEL_CLIP_VERTICES];
    clip_vertex_t clip_b[VOXEL_CLIP_VERTICES];
    clip_vertex_t *input = clip_a;
    clip_vertex_t *clipped = clip_b;
    pxa_raster_vertex_t vertices[VOXEL_CLIP_VERTICES];
    float center[3] = {0};
    float normal_dot;
    uint8_t count = 4;
    uint8_t primitive_count;
    uint8_t was_clipped = 0;
    uint8_t plane;
    uint8_t index;
    if (capacity == 0) return 0;
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
        input[index].right = dx * camera->rx + dz * camera->rz;
        input[index].up =
            dx * camera->ux + dy * camera->uy + dz * camera->uz;
        input[index].depth =
            dx * camera->fx + dy * camera->fy + dz * camera->fz;
    }
    {
        /* The 16x48 face atlas stacks top, side and bottom tiles. Y faces
         * pick the cap that points at the camera; side faces use the middle
         * band. Texture V grows downwards inside a tile, so V is flipped
         * against the world axis the face runs along: the highest world
         * corner samples row 0. This matches the ray caster's
         * `ty = 15 - frac(v)` and keeps grass fringes and table edges up. */
        const float v_base_q4 =
            (quad->axis == 1 ? (quad->sign > 0 ? 0.0F : 32.0F) : 16.0F) *
            256.0F;
        if (quad->axis == 0) {
            input[0].u_q4 = input[1].u_q4 = 0.0F;
            input[2].u_q4 = input[3].u_q4 = quad->u_length * 256.0F;
            input[0].v_q4 = input[3].v_q4 =
                v_base_q4 + quad->v_length * 256.0F;
            input[1].v_q4 = input[2].v_q4 = v_base_q4;
        } else if (quad->axis == 1) {
            input[0].u_q4 = input[3].u_q4 = 0.0F;
            input[1].u_q4 = input[2].u_q4 = quad->u_length * 256.0F;
            input[0].v_q4 = input[1].v_q4 =
                v_base_q4 + quad->v_length * 256.0F;
            input[2].v_q4 = input[3].v_q4 = v_base_q4;
        } else {
            input[0].u_q4 = input[3].u_q4 = 0.0F;
            input[1].u_q4 = input[2].u_q4 = quad->u_length * 256.0F;
            input[0].v_q4 = input[1].v_q4 =
                v_base_q4 + quad->v_length * 256.0F;
            input[2].v_q4 = input[3].v_q4 = v_base_q4;
        }
    }
    for (plane = 0; plane < CLIP_PLANE_COUNT; ++plane) {
        clip_vertex_t *swap;
        for (index = 0; index < count; ++index)
            if (clip_distance(camera, &input[index], plane) < 0.0F) {
                was_clipped = 1;
                break;
            }
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
    if (was_clipped) ++g_stats.clipped_quads;
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
        projected->vertices[index].u_q4 = 0;
        projected->vertices[index].v_q4 = 0;
        projected->vertices[index].light = 255;
        projected->vertices[index].depth_q8 =
            (uint16_t)(depth * 256.0F + 0.5F);
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

static void sort_candidates(uint32_t count, uint8_t depth_tested) {
    uint32_t gap;
    uint32_t index;
    if (count > VOXEL_RASTER_CANDIDATES) count = VOXEL_RASTER_CANDIDATES;
    for (index = 0; index < count; ++index)
        g_sort_order[index] = (uint16_t)index;
    for (gap = count / 2u; gap != 0; gap /= 2u) {
        for (index = gap; index < count; ++index) {
            const uint16_t value = g_sort_order[index];
            const float depth = g_candidates[value].depth;
            uint32_t cursor = index;
            while (cursor >= gap &&
                   (depth_tested
                        ? g_candidates[g_sort_order[cursor - gap]].depth > depth
                        : g_candidates[g_sort_order[cursor - gap]].depth <
                              depth)) {
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
                list, VOXEL_RASTER_FONT_SLOT,
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

static void append_ui_button(pxa_raster_draw_list_t *list,
                             const raster_ui_t *ui, int cx, int cy,
                             int radius, uint16_t fill, uint8_t active) {
    const uint16_t inner = active ? HUD_GREEN : fill;
    append_ui_rect(list, ui, cx - radius + 4, cy - radius, 2 * radius - 7, 1,
                   HUD_SHADOW);
    append_ui_rect(list, ui, cx - radius, cy - radius + 4, 2 * radius,
                   2 * radius - 7, HUD_SHADOW);
    append_ui_rect(list, ui, cx - radius + 4, cy + radius - 1, 2 * radius - 7,
                   1, HUD_SHADOW);
    append_ui_rect(list, ui, cx - radius + 3, cy - radius + 1,
                   2 * radius - 5, 2 * radius - 3, inner);
    append_ui_rect(list, ui, cx - radius + 1, cy - radius + 3,
                   2 * radius - 1, 2 * radius - 5, inner);
}

static void append_ui_arrow(pxa_raster_draw_list_t *list, const raster_ui_t *ui,
                            int cx, int cy, int down) {
    int index;
    for (index = 0; index < 6; ++index) {
        const int half = down ? 5 - index : index;
        append_ui_rect(list, ui, cx - half, cy - 2 + index, half * 2 + 1, 1,
                       HUD_TEXT);
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
    if (item <= VOXEL_RASTER_TEXTURE_SLOTS) {
        /* Inventory icons show the side band of the 16x48 face atlas. */
        (void)pxa_raster_sprite(list, (uint8_t)(item - 1u), 0,
                                g_raster_capabilities, (int16_t)mapped_x,
                                (int16_t)mapped_y, width, height, 0, 16, 16,
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
                            int x, int y, uint8_t count) {
    char number[4];
    char *out = number;
    if (count == 0) return;
    out = append_u32_text(out, count);
    *out = '\0';
    append_ui_text(list, ui, x - raster_text_width(number, 1), y, number,
                   HUD_TEXT, 1);
}

static void append_hud(pxa_raster_draw_list_t *list, const hud_state_t *hud,
                       uint16_t width, uint16_t height) {
    raster_ui_t ui;
    char status[32];
    char *out;
    int slot;
    const render_layout_t *layout;
    int center_x;
    int center_y;
    if (hud == NULL || hud->layout.screen_w <= 0 || hud->layout.screen_h <= 0)
        return;
    layout = &hud->layout;
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
        const int x = layout->hotbar_x + slot * HOTBAR_SLOT;
        const int y = layout->hotbar_y;
        append_ui_rect(list, &ui, x, y, HOTBAR_SLOT, HOTBAR_SLOT, HUD_PANEL);
        append_ui_outline(list, &ui, x, y, HOTBAR_SLOT, HOTBAR_SLOT, 1,
                          HUD_SHADOW);
        if (slot == hud->hotbar_selected)
            append_ui_outline(list, &ui, x, y - 2, HOTBAR_SLOT,
                              HOTBAR_SLOT + 2, 2, HUD_SELECT);
        append_ui_item(list, &ui, x + 2, y + 2, HOTBAR_SLOT - 4,
                       hud->hotbar_items[slot]);
        append_ui_count(list, &ui, x + HOTBAR_SLOT - 2, y + HOTBAR_SLOT - 7,
                        hud->hotbar_counts[slot]);
    }

    append_ui_button(list, &ui, layout->jump_x, layout->jump_y,
                     layout->jump_r, HUD_PANEL_LIGHT, hud->jump_held);
    append_ui_arrow(list, &ui, layout->jump_x, layout->jump_y - 3, 0);
    append_ui_button(list, &ui, layout->action_x, layout->action_y,
                     layout->action_r, HUD_PANEL_LIGHT,
                     (uint8_t)(hud->action_held || hud->action_mode != 0));
    if (hud->action_mode == 2) {
        append_ui_rect(list, &ui, layout->action_x - 7, layout->action_y - 5,
                       15, 3, HUD_TEXT);
        append_ui_rect(list, &ui, layout->action_x - 6, layout->action_y - 2,
                       3, 7, HUD_TEXT);
        append_ui_rect(list, &ui, layout->action_x + 4, layout->action_y - 2,
                       3, 7, HUD_TEXT);
    } else {
        append_ui_rect(list, &ui, layout->action_x - 2, layout->action_y - 6,
                       3, 14, HUD_TEXT);
        append_ui_rect(list, &ui, layout->action_x - 8, layout->action_y - 4,
                       14, 3, HUD_TEXT);
    }
    append_ui_button(list, &ui, layout->place_x, layout->place_y,
                     layout->place_r, HUD_PANEL_LIGHT, 0);
    append_ui_rect(list, &ui, layout->place_x - 7, layout->place_y - 1, 15,
                   3, HUD_TEXT);
    append_ui_rect(list, &ui, layout->place_x - 1, layout->place_y - 7, 3,
                   15, HUD_TEXT);
    if (hud->flying) {
        append_ui_button(list, &ui, layout->down_x, layout->down_y,
                         layout->down_r, HUD_PANEL_LIGHT, hud->down_held);
        append_ui_arrow(list, &ui, layout->down_x, layout->down_y - 3, 1);
    }
    append_ui_button(list, &ui, layout->fly_x, layout->fly_y, layout->fly_r,
                     HUD_PANEL_LIGHT, hud->flying);
    append_ui_text(list, &ui, layout->fly_x - 3, layout->fly_y - 3, "F",
                   HUD_TEXT, 1);
    append_ui_button(list, &ui, layout->menu_x, layout->menu_y,
                     layout->menu_r, HUD_PANEL_LIGHT, 0);
    append_ui_rect(list, &ui, layout->menu_x - 6, layout->menu_y - 5, 13, 2,
                   HUD_TEXT);
    append_ui_rect(list, &ui, layout->menu_x - 6, layout->menu_y - 1, 13, 2,
                   HUD_TEXT);
    append_ui_rect(list, &ui, layout->menu_x - 6, layout->menu_y + 3, 13, 2,
                   HUD_TEXT);
    append_ui_button(list, &ui, layout->bag_x, layout->bag_y, layout->bag_r,
                     HUD_PANEL_LIGHT, hud->inventory_open);
    append_ui_outline(list, &ui, layout->bag_x - 6, layout->bag_y - 4, 13, 9,
                      1, HUD_TEXT);
    append_ui_rect(list, &ui, layout->bag_x - 3, layout->bag_y - 6, 7, 2,
                   HUD_TEXT);

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

int32_t voxel_raster_render(uint32_t surface_handle, uint64_t frame_id,
                            const player_t *player, uint8_t quality,
                            const hud_state_t *hud) {
    raster_camera_t camera;
    pxa_raster_draw_list_t list;
    visible_chunk_t visible_chunks[GRID_COUNT];
    uint32_t candidate_count = 0;
    uint32_t candidate_limit = quality >= QUALITY_PERFORMANCE ? 320u
                               : quality >= QUALITY_BALANCED  ? 480u
                                                              : 620u;
    uint32_t mesh_limit;
    uint8_t visible_count = 0;
    int grid_z;
    if (surface_handle == 0 || frame_id == 0 || player == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    pxa_raster_zero_bytes(&g_stats, sizeof(g_stats));
    build_camera(&camera, player, quality);
    for (grid_z = 0; grid_z < GRID_W; ++grid_z) {
        int grid_x;
        for (grid_x = 0; grid_x < GRID_W; ++grid_x) {
            const int mesh_index = grid_z * GRID_W + grid_x;
            const chunk_t *chunk = g_chunk_grid[grid_z][grid_x];
            chunk_mesh_t *mesh = &g_meshes[mesh_index];
            if (chunk == NULL || !chunk->loaded || !chunk_visible(&camera, chunk)) {
                ++g_stats.frustum_culled;
                continue;
            }
            if (mesh->chunk != chunk || mesh->revision != chunk->revision)
                rebuild_mesh(mesh, chunk);
            g_stats.cached_quads += mesh->quad_count;
            g_stats.dropped_quads += mesh->dropped;
            visible_chunks[visible_count].chunk = chunk;
            visible_chunks[visible_count].mesh = mesh;
            visible_chunks[visible_count].depth = chunk_depth(&camera, chunk);
            ++visible_count;
        }
    }
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
                (uint16_t)(mesh_limit - candidate_count));
        }
    }
    append_entities(&camera, candidate_limit, &candidate_count);
    g_stats.candidate_quads = candidate_count;
    sort_candidates(
        candidate_count,
        (g_raster_capabilities & PXA_RASTER_CAP_TEXTURED_QUAD) != 0);
    pxa_raster_draw_list_begin(&list, g_draw_list, sizeof(g_draw_list), frame_id);
    (void)pxa_raster_clear(&list, UINT16_C(0x9e5f));
    for (uint32_t order = 0; order < candidate_count; ++order) {
        projected_quad_t *quad = &g_candidates[g_sort_order[order]];
        int added;
        if (!projected_quad_has_area(quad)) {
            ++g_stats.dropped_quads;
            continue;
        }
        if (quad->textured &&
            (g_raster_capabilities & PXA_RASTER_CAP_TEXTURED_QUAD) != 0) {
            if (quad->affine &&
                (g_raster_capabilities & PXA_RASTER_CAP_AFFINE_UV) != 0) {
                added = pxa_raster_textured_quad_flags(
                    &list, quad->vertices, quad->texture_slot,
                    PXA_RASTER_QUAD_AFFINE_UV);
            } else {
                added = pxa_raster_textured_quad(&list, quad->vertices,
                                                 quad->texture_slot);
            }
        } else if ((g_raster_capabilities &
                    PXA_RASTER_CAP_TEXTURED_QUAD) != 0) {
            added = pxa_raster_solid_depth_quad(&list, quad->vertices,
                                                quad->color);
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
            g_stats.dropped_quads += candidate_count - order;
            break;
        }
        ++g_stats.submitted_quads;
        if (quad->textured && quad->affine &&
            (g_raster_capabilities & PXA_RASTER_CAP_AFFINE_UV) != 0)
            ++g_stats.affine_quads;
    }
    append_hud(&list, hud, camera.width, camera.height);
    g_stats.draw_list_bytes = list.length;
    g_stats.covered_pixel_budget = (uint32_t)camera.width * camera.height;
    return pxa_raster_submit(surface_handle, &list);
}

void voxel_raster_get_stats(voxel_raster_stats_t *stats) {
    if (stats != NULL) *stats = g_stats;
}

#include "pxa/raster.h"

#include <limits.h>
#include <string.h>

#include "pxa/wire.h"

typedef struct {
    int16_t x;
    int16_t y;
    int16_t u;
    int16_t v;
    uint8_t light;
    uint16_t depth;
} raster_vertex_t;

static uint16_t read_u16(const uint8_t *bytes) { return pxa_read_u16(bytes); }
static uint32_t read_u32(const uint8_t *bytes) { return pxa_read_u32(bytes); }
static uint64_t read_u64(const uint8_t *bytes) { return pxa_read_u64(bytes); }
static int16_t read_i16(const uint8_t *bytes) {
    return (int16_t)pxa_read_u16(bytes);
}

static uint16_t saturating_add_rgb565(uint16_t destination, uint16_t source) {
    uint32_t red = (destination >> 11) + (source >> 11);
    uint32_t green = ((destination >> 5) & 63u) + ((source >> 5) & 63u);
    uint32_t blue = (destination & 31u) + (source & 31u);
    if (red > 31u) red = 31u;
    if (green > 63u) green = 63u;
    if (blue > 31u) blue = 31u;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static uint16_t light_rgb565(uint16_t color, uint8_t light) {
    uint32_t red = ((color >> 11) * light + 127u) / 255u;
    uint32_t green = (((color >> 5) & 63u) * light + 127u) / 255u;
    uint32_t blue = ((color & 31u) * light + 127u) / 255u;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

pxa_status_t pxa_raster_decode_upload(const uint8_t *bytes, size_t size,
                                      pxa_raster_upload_view_t *output) {
    uint32_t payload_bytes;
    uint16_t major;
    uint16_t minor;
    uint16_t flags;
    uint16_t width;
    uint16_t height;
    uint8_t kind;
    uint8_t slot;
    uint64_t expected;
    if (bytes == NULL || output == NULL || size < PXA_RASTER_UPLOAD_HEADER_BYTES)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (read_u32(bytes) != PXA_RASTER_UPLOAD_MAGIC)
        return PXA_STATUS_PROTOCOL_ERROR;
    major = read_u16(bytes + 4);
    minor = read_u16(bytes + 6);
    kind = bytes[8];
    slot = bytes[9];
    flags = read_u16(bytes + 10);
    width = read_u16(bytes + 12);
    height = read_u16(bytes + 14);
    payload_bytes = read_u32(bytes + 16);
    if (major != PXA_RASTER_ABI_MAJOR || minor > PXA_RASTER_ABI_MINOR)
        return PXA_STATUS_UNSUPPORTED;
    if (flags != 0 || payload_bytes != size - PXA_RASTER_UPLOAD_HEADER_BYTES)
        return PXA_STATUS_PROTOCOL_ERROR;
    if (kind == PXA_RASTER_UPLOAD_PALETTE_RGB565) {
        if (slot != 0 || width != PXA_RASTER_PALETTE_COLORS || height != 1 ||
            payload_bytes != PXA_RASTER_PALETTE_COLORS * sizeof(uint16_t))
            return PXA_STATUS_INVALID_ARGUMENT;
    } else if (kind == PXA_RASTER_UPLOAD_TEXTURE_INDEX8) {
        if (slot >= PXA_RASTER_MAX_TEXTURES || width == 0 || height == 0 ||
            width > PXA_RASTER_MAX_TEXTURE_DIMENSION ||
            height > PXA_RASTER_MAX_TEXTURE_DIMENSION)
            return PXA_STATUS_INVALID_ARGUMENT;
        expected = (uint64_t)width * height;
        if (expected != payload_bytes) return PXA_STATUS_INVALID_ARGUMENT;
    } else {
        return PXA_STATUS_UNSUPPORTED;
    }
    output->kind = kind;
    output->slot = slot;
    output->width = width;
    output->height = height;
    output->payload = bytes + PXA_RASTER_UPLOAD_HEADER_BYTES;
    output->payload_bytes = payload_bytes;
    return PXA_STATUS_OK;
}

static void decode_vertex(const uint8_t *bytes, raster_vertex_t *vertex) {
    vertex->x = read_i16(bytes);
    vertex->y = read_i16(bytes + 2);
    vertex->u = read_i16(bytes + 4);
    vertex->v = read_i16(bytes + 6);
    vertex->light = bytes[8];
    vertex->depth = read_u16(bytes + 10);
}

static int64_t polygon_area2(const raster_vertex_t vertices[4]) {
    int64_t area = 0;
    uint8_t index;
    for (index = 0; index < 4; ++index) {
        const raster_vertex_t *a = &vertices[index];
        const raster_vertex_t *b = &vertices[(index + 1u) & 3u];
        area += (int64_t)a->x * b->y - (int64_t)b->x * a->y;
    }
    return area;
}

static pxa_status_t validate_quad(const uint8_t *record, uint16_t record_size,
                                  uint8_t textured,
                                  uint16_t abi_minor,
                                  const pxa_raster_resources_t *resources) {
    raster_vertex_t vertices[4];
    uint8_t index;
    uint32_t offset;
    if ((!textured && record_size != PXA_RASTER_FLAT_QUAD_BYTES) ||
        (textured && record_size != PXA_RASTER_TEXTURED_QUAD_BYTES))
        return PXA_STATUS_PROTOCOL_ERROR;
    if (textured) {
        const uint8_t flags = record[1];
        const uint8_t slot = record[4];
        const uint8_t solid = flags & PXA_RASTER_QUAD_SOLID_COLOR;
        if ((flags & ~PXA_RASTER_QUAD_SOLID_COLOR) != 0 ||
            (solid != 0 && abi_minor < 1))
            return PXA_STATUS_UNSUPPORTED;
        if (record[5] != 0 || (solid == 0 && read_u16(record + 6) != 0))
            return PXA_STATUS_PROTOCOL_ERROR;
        if (solid == 0 &&
            (slot >= PXA_RASTER_MAX_TEXTURES || resources->palette == NULL ||
             resources->textures[slot].pixels == NULL))
            return PXA_STATUS_BAD_STATE;
        offset = 8;
    } else {
        if (record[1] != 0) return PXA_STATUS_UNSUPPORTED;
        if (read_u16(record + 6) != 0) return PXA_STATUS_PROTOCOL_ERROR;
        offset = 8;
    }
    for (index = 0; index < 4; ++index) {
        if (textured) {
            decode_vertex(record + offset + index * PXA_RASTER_VERTEX_BYTES,
                          &vertices[index]);
            if (record[offset + index * PXA_RASTER_VERTEX_BYTES + 9] != 0 ||
                (abi_minor == 0 && vertices[index].depth != 0) ||
                (abi_minor >= 1 && vertices[index].depth == 0))
                return PXA_STATUS_PROTOCOL_ERROR;
        } else {
            vertices[index].x = read_i16(record + offset + index * 4u);
            vertices[index].y = read_i16(record + offset + index * 4u + 2u);
        }
    }
    return polygon_area2(vertices) == 0 ? PXA_STATUS_INVALID_ARGUMENT
                                       : PXA_STATUS_OK;
}

static pxa_status_t validate_sprite(const uint8_t *record, uint16_t size,
                                    const pxa_raster_resources_t *resources) {
    const uint8_t flags = record[1];
    const uint8_t slot = record[4];
    const uint8_t known = PXA_RASTER_SPRITE_TRANSPARENT_INDEX0 |
                          PXA_RASTER_SPRITE_SOLID_COLOR |
                          PXA_RASTER_SPRITE_ADDITIVE;
    if (size != PXA_RASTER_SPRITE_BYTES) return PXA_STATUS_PROTOCOL_ERROR;
    if ((flags & ~known) != 0) return PXA_STATUS_UNSUPPORTED;
    if ((flags & PXA_RASTER_SPRITE_ADDITIVE) != 0 &&
        (resources->capabilities & PXA_RASTER_CAP_ADDITIVE_SPRITE) == 0)
        return PXA_STATUS_UNSUPPORTED;
    if (slot >= PXA_RASTER_MAX_TEXTURES ||
        resources->textures[slot].pixels == NULL || resources->palette == NULL)
        return PXA_STATUS_BAD_STATE;
    if (record[5] != 0 ||
        ((flags & PXA_RASTER_SPRITE_SOLID_COLOR) == 0 &&
         read_u16(record + 6) != 0) ||
        read_u16(record + 12) == 0 || read_u16(record + 14) == 0 ||
        read_u16(record + 20) == 0 || read_u16(record + 22) == 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    if ((uint32_t)read_u16(record + 16) + read_u16(record + 20) >
            resources->textures[slot].width ||
        (uint32_t)read_u16(record + 18) + read_u16(record + 22) >
            resources->textures[slot].height)
        return PXA_STATUS_INVALID_ARGUMENT;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_raster_validate_draw_list(
    const uint8_t *bytes, size_t size, const pxa_raster_target_t *target,
    const pxa_raster_resources_t *resources,
    pxa_raster_draw_list_view_t *output) {
    uint32_t total_size;
    uint32_t required;
    uint32_t command_count;
    uint32_t offset;
    uint32_t index;
    uint16_t abi_minor;
    if (bytes == NULL || target == NULL || resources == NULL || output == NULL ||
        target->pixels == NULL || target->width == 0 || target->height == 0 ||
        target->stride_pixels < target->width ||
        (target->depth_pixels != NULL &&
         target->depth_stride_pixels < target->width))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (size < PXA_RASTER_DRAW_HEADER_BYTES || size > PXA_RASTER_MAX_DRAW_BYTES)
        return PXA_STATUS_LIMIT_EXCEEDED;
    if (read_u32(bytes) != PXA_RASTER_DRAW_MAGIC)
        return PXA_STATUS_PROTOCOL_ERROR;
    abi_minor = read_u16(bytes + 6);
    if (read_u16(bytes + 4) != PXA_RASTER_ABI_MAJOR ||
        abi_minor > PXA_RASTER_ABI_MINOR)
        return PXA_STATUS_UNSUPPORTED;
    total_size = read_u32(bytes + 8);
    required = read_u32(bytes + 12);
    command_count = read_u32(bytes + 16);
    if (total_size != size || read_u32(bytes + 28) != 0)
        return PXA_STATUS_PROTOCOL_ERROR;
    if (command_count == 0 || command_count > PXA_RASTER_MAX_COMMANDS)
        return PXA_STATUS_LIMIT_EXCEEDED;
    if ((required & ~PXA_RASTER_CAP_KNOWN_MASK) != 0 ||
        (required & ~resources->capabilities) != 0)
        return PXA_STATUS_UNSUPPORTED;
    if (read_u64(bytes + 20) == 0) return PXA_STATUS_INVALID_ARGUMENT;
    offset = PXA_RASTER_DRAW_HEADER_BYTES;
    for (index = 0; index < command_count; ++index) {
        uint16_t record_size;
        uint8_t type;
        pxa_status_t status = PXA_STATUS_OK;
        if (offset > size || size - offset < PXA_RASTER_RECORD_HEADER_BYTES)
            return PXA_STATUS_PROTOCOL_ERROR;
        type = bytes[offset];
        record_size = read_u16(bytes + offset + 2);
        if (record_size < PXA_RASTER_RECORD_HEADER_BYTES ||
            record_size > size - offset)
            return PXA_STATUS_PROTOCOL_ERROR;
        if (type == PXA_RASTER_RECORD_CLEAR_RGB565) {
            if (record_size != PXA_RASTER_CLEAR_BYTES || bytes[offset + 1] != 0 ||
                read_u16(bytes + offset + 6) != 0)
                status = PXA_STATUS_PROTOCOL_ERROR;
        } else if (type == PXA_RASTER_RECORD_FLAT_QUAD) {
            if ((required & PXA_RASTER_CAP_FLAT_QUAD) == 0)
                status = PXA_STATUS_PROTOCOL_ERROR;
            else
                status = validate_quad(bytes + offset, record_size, 0,
                                       abi_minor, resources);
        } else if (type == PXA_RASTER_RECORD_TEXTURED_QUAD) {
            if ((required & PXA_RASTER_CAP_TEXTURED_QUAD) == 0)
                status = PXA_STATUS_PROTOCOL_ERROR;
            else
                status = validate_quad(bytes + offset, record_size, 1,
                                       abi_minor, resources);
        } else if (type == PXA_RASTER_RECORD_SPRITE) {
            status = validate_sprite(bytes + offset, record_size, resources);
        } else {
            return PXA_STATUS_UNSUPPORTED;
        }
        if (status != PXA_STATUS_OK) return status;
        offset += record_size;
    }
    if (offset != size) return PXA_STATUS_PROTOCOL_ERROR;
    output->abi_minor = abi_minor;
    output->total_size = total_size;
    output->required_capabilities = required;
    output->command_count = command_count;
    output->frame_id = read_u64(bytes + 20);
    return PXA_STATUS_OK;
}

static int64_t edge(const raster_vertex_t *a, const raster_vertex_t *b,
                    int32_t x, int32_t y) {
    return (int64_t)(x - a->x) * (b->y - a->y) -
           (int64_t)(y - a->y) * (b->x - a->x);
}

#define RASTER_RECIPROCAL_DEPTH_ONE INT64_C(524288)
#define RASTER_INTERPOLANT_SCALE INT64_C(16)
#define RASTER_AREA_RECIPROCAL_BITS 35

static int64_t scaled_divide(int64_t numerator, int64_t denominator) {
    const int64_t quotient = numerator / denominator;
    const int64_t remainder = numerator % denominator;
    return quotient * RASTER_INTERPOLANT_SCALE +
           remainder * RASTER_INTERPOLANT_SCALE / denominator;
}

static int64_t interpolate_scaled_reciprocal(
    int64_t w0, int64_t w1, int64_t w2, int64_t a, int64_t b, int64_t c,
    int64_t denominator, int64_t area_reciprocal) {
    const int64_t numerator = w0 * a + w1 * b + w2 * c;
    const int64_t limit = INT64_MAX / area_reciprocal;
    if (numerator >= -limit && numerator <= limit) {
        return (numerator * area_reciprocal) /
               (INT64_C(1) <<
                (RASTER_AREA_RECIPROCAL_BITS - 4));
    }
    return scaled_divide(numerator, denominator);
}

static int64_t reciprocal_depth(uint16_t depth) {
    return depth == 0 ? 0 : RASTER_RECIPROCAL_DEPTH_ONE / depth;
}

#define RASTER_PERSPECTIVE_BLOCK_PIXELS 8
#define RASTER_TEXTURE_Q8_FROM_UV_Q4 16

static int32_t perspective_texture_q8(int64_t numerator, int64_t denominator) {
    if (denominator == 0) return 0;
    if (numerator >= INT32_MIN / RASTER_TEXTURE_Q8_FROM_UV_Q4 &&
        numerator <= INT32_MAX / RASTER_TEXTURE_Q8_FROM_UV_Q4 &&
        denominator >= INT32_MIN && denominator <= INT32_MAX) {
        return (int32_t)numerator * RASTER_TEXTURE_Q8_FROM_UV_Q4 /
               (int32_t)denominator;
    }
    return (int32_t)((numerator / denominator) *
                         RASTER_TEXTURE_Q8_FROM_UV_Q4 +
                     (numerator % denominator) *
                         RASTER_TEXTURE_Q8_FROM_UV_Q4 / denominator);
}

static uint32_t draw_triangle(const raster_vertex_t *a,
                              const raster_vertex_t *b,
                              const raster_vertex_t *c, uint16_t flat_color,
                              const pxa_raster_texture_t *texture,
                              const uint16_t *palette,
                              uint8_t perspective,
                              const pxa_raster_target_t *target) {
    int32_t min_x = a->x;
    int32_t max_x = a->x;
    int32_t min_y = a->y;
    int32_t max_y = a->y;
    int64_t area = edge(a, b, c->x, c->y);
    int64_t w0_row;
    int64_t w1_row;
    int64_t w2_row;
    int64_t w0_dx;
    int64_t w1_dx;
    int64_t w2_dx;
    int64_t w0_dy;
    int64_t w1_dy;
    int64_t w2_dy;
    int64_t u_row = 0;
    int64_t v_row = 0;
    int64_t light_row = 0;
    int64_t u_dx = 0;
    int64_t v_dx = 0;
    int64_t light_dx = 0;
    int64_t u_dy = 0;
    int64_t v_dy = 0;
    int64_t light_dy = 0;
    int64_t reciprocal_row = 0;
    int64_t reciprocal_dx = 0;
    int64_t reciprocal_dy = 0;
    int64_t u_reciprocal_row = 0;
    int64_t u_reciprocal_dx = 0;
    int64_t u_reciprocal_dy = 0;
    int64_t v_reciprocal_row = 0;
    int64_t v_reciprocal_dx = 0;
    int64_t v_reciprocal_dy = 0;
    const uint8_t depth_test = perspective && target->depth_pixels != NULL;
    const uint8_t width_power_of_two =
        texture != NULL && (texture->width & (texture->width - 1u)) == 0;
    const uint8_t height_power_of_two =
        texture != NULL && (texture->height & (texture->height - 1u)) == 0;
    uint32_t covered = 0;
    int32_t y;
    if (b->x < min_x) min_x = b->x;
    if (c->x < min_x) min_x = c->x;
    if (b->x > max_x) max_x = b->x;
    if (c->x > max_x) max_x = c->x;
    if (b->y < min_y) min_y = b->y;
    if (c->y < min_y) min_y = c->y;
    if (b->y > max_y) max_y = b->y;
    if (c->y > max_y) max_y = c->y;
    min_x >>= 4;
    min_y >>= 4;
    max_x = (max_x + 15) >> 4;
    max_y = (max_y + 15) >> 4;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x > target->width) max_x = target->width;
    if (max_y > target->height) max_y = target->height;
    if (area == 0 || min_x >= max_x || min_y >= max_y) return 0;
    {
        const int64_t sign = area < 0 ? -1 : 1;
        const int32_t sample_x = (min_x << 4) + 8;
        const int32_t sample_y = (min_y << 4) + 8;
        const int64_t denominator = area * sign;
        const int64_t area_reciprocal =
            ((INT64_C(1) << RASTER_AREA_RECIPROCAL_BITS) +
             denominator / 2) /
            denominator;
        w0_row = edge(b, c, sample_x, sample_y) * sign;
        w1_row = edge(c, a, sample_x, sample_y) * sign;
        w2_row = edge(a, b, sample_x, sample_y) * sign;
        w0_dx = INT64_C(16) * (c->y - b->y) * sign;
        w1_dx = INT64_C(16) * (a->y - c->y) * sign;
        w2_dx = INT64_C(16) * (b->y - a->y) * sign;
        w0_dy = -INT64_C(16) * (c->x - b->x) * sign;
        w1_dy = -INT64_C(16) * (a->x - c->x) * sign;
        w2_dy = -INT64_C(16) * (b->x - a->x) * sign;
        if (texture != NULL) {
            const int64_t precision = INT64_C(4096);
            light_row = (w0_row * a->light + w1_row * b->light +
                         w2_row * c->light) * precision / denominator;
            light_dx = (w0_dx * a->light + w1_dx * b->light +
                        w2_dx * c->light) * precision / denominator;
            light_dy = (w0_dy * a->light + w1_dy * b->light +
                        w2_dy * c->light) * precision / denominator;
            if (!perspective) {
                u_row = (w0_row * a->u + w1_row * b->u + w2_row * c->u) *
                        precision / denominator;
                v_row = (w0_row * a->v + w1_row * b->v + w2_row * c->v) *
                        precision / denominator;
                u_dx = (w0_dx * a->u + w1_dx * b->u + w2_dx * c->u) *
                       precision / denominator;
                v_dx = (w0_dx * a->v + w1_dx * b->v + w2_dx * c->v) *
                       precision / denominator;
                u_dy = (w0_dy * a->u + w1_dy * b->u + w2_dy * c->u) *
                       precision / denominator;
                v_dy = (w0_dy * a->v + w1_dy * b->v + w2_dy * c->v) *
                       precision / denominator;
            }
        }
        if (perspective) {
            const int64_t qa = reciprocal_depth(a->depth);
            const int64_t qb = reciprocal_depth(b->depth);
            const int64_t qc = reciprocal_depth(c->depth);
            reciprocal_row = interpolate_scaled_reciprocal(
                w0_row, w1_row, w2_row, qa, qb, qc, denominator,
                area_reciprocal);
            reciprocal_dx = interpolate_scaled_reciprocal(
                w0_dx, w1_dx, w2_dx, qa, qb, qc, denominator,
                area_reciprocal);
            reciprocal_dy = interpolate_scaled_reciprocal(
                w0_dy, w1_dy, w2_dy, qa, qb, qc, denominator,
                area_reciprocal);
            if (texture != NULL) {
                const int64_t uqa = (int64_t)a->u * qa;
                const int64_t uqb = (int64_t)b->u * qb;
                const int64_t uqc = (int64_t)c->u * qc;
                const int64_t vqa = (int64_t)a->v * qa;
                const int64_t vqb = (int64_t)b->v * qb;
                const int64_t vqc = (int64_t)c->v * qc;
                u_reciprocal_row = interpolate_scaled_reciprocal(
                    w0_row, w1_row, w2_row, uqa, uqb, uqc, denominator,
                    area_reciprocal);
                u_reciprocal_dx = interpolate_scaled_reciprocal(
                    w0_dx, w1_dx, w2_dx, uqa, uqb, uqc, denominator,
                    area_reciprocal);
                u_reciprocal_dy = interpolate_scaled_reciprocal(
                    w0_dy, w1_dy, w2_dy, uqa, uqb, uqc, denominator,
                    area_reciprocal);
                v_reciprocal_row = interpolate_scaled_reciprocal(
                    w0_row, w1_row, w2_row, vqa, vqb, vqc, denominator,
                    area_reciprocal);
                v_reciprocal_dx = interpolate_scaled_reciprocal(
                    w0_dx, w1_dx, w2_dx, vqa, vqb, vqc, denominator,
                    area_reciprocal);
                v_reciprocal_dy = interpolate_scaled_reciprocal(
                    w0_dy, w1_dy, w2_dy, vqa, vqb, vqc, denominator,
                    area_reciprocal);
            }
        }
    }
    for (y = min_y; y < max_y; ++y) {
        uint16_t *row = target->pixels + (size_t)y * target->stride_pixels;
        uint16_t *depth_row_pixels = target->depth_pixels == NULL
            ? NULL
            : target->depth_pixels +
                  (size_t)y * target->depth_stride_pixels;
        int64_t w0 = w0_row;
        int64_t w1 = w1_row;
        int64_t w2 = w2_row;
        int32_t span_start = min_x;
        int32_t span_end;
        while (span_start < max_x && (w0 < 0 || w1 < 0 || w2 < 0)) {
            w0 += w0_dx;
            w1 += w1_dx;
            w2 += w2_dx;
            ++span_start;
        }
        span_end = span_start;
        while (span_end < max_x && w0 >= 0 && w1 >= 0 && w2 >= 0) {
            w0 += w0_dx;
            w1 += w1_dx;
            w2 += w2_dx;
            ++span_end;
        }
        if (span_start < span_end) {
            const int32_t offset = span_start - min_x;
            int64_t u = u_row + u_dx * offset;
            int64_t v = v_row + v_dx * offset;
            int64_t light = light_row + light_dx * offset;
            int64_t reciprocal = reciprocal_row + reciprocal_dx * offset;
            int64_t u_reciprocal =
                u_reciprocal_row + u_reciprocal_dx * offset;
            int64_t v_reciprocal =
                v_reciprocal_row + v_reciprocal_dx * offset;
            int32_t u_texture_q8 = 0;
            int32_t v_texture_q8 = 0;
            int32_t x = span_start;
            if (perspective && texture != NULL) {
                u_texture_q8 = perspective_texture_q8(
                    u_reciprocal, reciprocal);
                v_texture_q8 = perspective_texture_q8(
                    v_reciprocal, reciprocal);
            }
            while (x < span_end) {
                int32_t u_texture_step_q8 = 0;
                int32_t v_texture_step_q8 = 0;
                int32_t u_texture_end_q8 = 0;
                int32_t v_texture_end_q8 = 0;
                int32_t block_pixels = span_end - x;
                int32_t block_index;
                if (block_pixels > RASTER_PERSPECTIVE_BLOCK_PIXELS)
                    block_pixels = RASTER_PERSPECTIVE_BLOCK_PIXELS;
                if (perspective && texture != NULL) {
                    const int64_t reciprocal_end =
                        reciprocal + reciprocal_dx * block_pixels;
                    const int64_t u_reciprocal_end =
                        u_reciprocal + u_reciprocal_dx * block_pixels;
                    const int64_t v_reciprocal_end =
                        v_reciprocal + v_reciprocal_dx * block_pixels;
                    u_texture_end_q8 = perspective_texture_q8(
                        u_reciprocal_end, reciprocal_end);
                    v_texture_end_q8 = perspective_texture_q8(
                        v_reciprocal_end, reciprocal_end);
                    u_texture_step_q8 =
                        (u_texture_end_q8 - u_texture_q8) / block_pixels;
                    v_texture_step_q8 =
                        (v_texture_end_q8 - v_texture_q8) / block_pixels;
                }
                for (block_index = 0; block_index < block_pixels;
                     ++block_index, ++x) {
                    uint16_t color;
                    int64_t inverse_depth =
                        reciprocal / RASTER_INTERPOLANT_SCALE;
                    uint16_t pixel_depth;
                    if (inverse_depth < 0) inverse_depth = 0;
                    if (inverse_depth > UINT16_MAX)
                        inverse_depth = UINT16_MAX;
                    pixel_depth = (uint16_t)inverse_depth;
                    if (!depth_test || pixel_depth >= depth_row_pixels[x]) {
                        if (texture == NULL) {
                            color = flat_color;
                        } else {
                            int32_t tx;
                            int32_t ty;
                            int32_t intensity = (int32_t)(light >> 12);
                            if (perspective) {
                                tx = u_texture_q8 >> 8;
                                ty = v_texture_q8 >> 8;
                            } else {
                                tx = (int32_t)(u >> 16);
                                ty = (int32_t)(v >> 16);
                            }
                            if (width_power_of_two) {
                                tx = (int32_t)((uint32_t)tx &
                                               (texture->width - 1u));
                            } else {
                                tx %= texture->width;
                                if (tx < 0) tx += texture->width;
                            }
                            if (height_power_of_two) {
                                ty = (int32_t)((uint32_t)ty &
                                               (texture->height - 1u));
                            } else {
                                ty %= texture->height;
                                if (ty < 0) ty += texture->height;
                            }
                            if (intensity < 0) intensity = 0;
                            if (intensity > 255) intensity = 255;
                            color = light_rgb565(
                                palette[texture->pixels[(size_t)ty *
                                                        texture->width + tx]],
                                (uint8_t)intensity);
                        }
                        row[x] = color;
                        if (depth_test) depth_row_pixels[x] = pixel_depth;
                        ++covered;
                    }
                    u += u_dx;
                    v += v_dx;
                    light += light_dx;
                    reciprocal += reciprocal_dx;
                    u_reciprocal += u_reciprocal_dx;
                    v_reciprocal += v_reciprocal_dx;
                    u_texture_q8 += u_texture_step_q8;
                    v_texture_q8 += v_texture_step_q8;
                }
                if (perspective && texture != NULL) {
                    u_texture_q8 = u_texture_end_q8;
                    v_texture_q8 = v_texture_end_q8;
                }
            }
        }
        w0_row += w0_dy;
        w1_row += w1_dy;
        w2_row += w2_dy;
        u_row += u_dy;
        v_row += v_dy;
        light_row += light_dy;
        reciprocal_row += reciprocal_dy;
        u_reciprocal_row += u_reciprocal_dy;
        v_reciprocal_row += v_reciprocal_dy;
    }
    return covered;
}

static uint32_t draw_quad(const uint8_t *record, uint8_t textured,
                          uint16_t abi_minor,
                          const pxa_raster_target_t *target,
                          const pxa_raster_resources_t *resources) {
    raster_vertex_t vertices[4];
    const pxa_raster_texture_t *texture = NULL;
    uint16_t color = 0;
    uint32_t offset = 8;
    uint8_t index;
    const uint8_t perspective = textured && abi_minor >= 1;
    if (textured) {
        if ((record[1] & PXA_RASTER_QUAD_SOLID_COLOR) != 0)
            color = read_u16(record + 6);
        else
            texture = &resources->textures[record[4]];
        for (index = 0; index < 4; ++index)
            decode_vertex(record + offset + index * PXA_RASTER_VERTEX_BYTES,
                          &vertices[index]);
    } else {
        color = read_u16(record + 4);
        for (index = 0; index < 4; ++index) {
            vertices[index].x = read_i16(record + offset + index * 4u);
            vertices[index].y = read_i16(record + offset + index * 4u + 2u);
        }
    }
    return draw_triangle(&vertices[0], &vertices[1], &vertices[2], color,
                         texture, resources->palette, perspective, target) +
           draw_triangle(&vertices[0], &vertices[2], &vertices[3], color,
                         texture, resources->palette, perspective, target);
}

static uint32_t draw_sprite(const uint8_t *record,
                            const pxa_raster_target_t *target,
                            const pxa_raster_resources_t *resources) {
    const pxa_raster_texture_t *texture = &resources->textures[record[4]];
    const uint8_t flags = record[1];
    const uint16_t solid_color = read_u16(record + 6);
    const int32_t x0 = read_i16(record + 8);
    const int32_t y0 = read_i16(record + 10);
    const uint32_t width = read_u16(record + 12);
    const uint32_t height = read_u16(record + 14);
    const uint32_t source_x = read_u16(record + 16);
    const uint32_t source_y = read_u16(record + 18);
    const uint32_t source_width = read_u16(record + 20);
    const uint32_t source_height = read_u16(record + 22);
    uint32_t covered = 0;
    uint32_t dy;
    for (dy = 0; dy < height; ++dy) {
        const int32_t y = y0 + (int32_t)dy;
        uint32_t dx;
        if (y < 0 || y >= target->height) continue;
        for (dx = 0; dx < width; ++dx) {
            const int32_t x = x0 + (int32_t)dx;
            const uint32_t tx = source_x + dx * source_width / width;
            const uint32_t ty = source_y + dy * source_height / height;
            const uint8_t texel = texture->pixels[(size_t)ty * texture->width + tx];
            uint16_t color;
            uint16_t *destination;
            if (x < 0 || x >= target->width) continue;
            if ((flags & PXA_RASTER_SPRITE_TRANSPARENT_INDEX0) != 0 && texel == 0)
                continue;
            color = (flags & PXA_RASTER_SPRITE_SOLID_COLOR) != 0
                        ? solid_color
                        : resources->palette[texel];
            destination = target->pixels + (size_t)y * target->stride_pixels + x;
            *destination = (flags & PXA_RASTER_SPRITE_ADDITIVE) != 0
                               ? saturating_add_rgb565(*destination, color)
                               : color;
            ++covered;
        }
    }
    return covered;
}

void pxa_raster_execute_draw_list(const uint8_t *bytes,
                                  const pxa_raster_draw_list_view_t *list,
                                  const pxa_raster_target_t *target,
                                  const pxa_raster_resources_t *resources,
                                  pxa_raster_telemetry_t *telemetry) {
    uint32_t offset = PXA_RASTER_DRAW_HEADER_BYTES;
    uint32_t index;
    uint32_t covered = 0;
    if (bytes == NULL || list == NULL || target == NULL || resources == NULL)
        return;
    for (index = 0; index < list->command_count; ++index) {
        const uint8_t *record = bytes + offset;
        const uint16_t size = read_u16(record + 2);
        if (record[0] == PXA_RASTER_RECORD_CLEAR_RGB565) {
            const uint16_t color = read_u16(record + 4);
            uint16_t y;
            for (y = 0; y < target->height; ++y) {
                uint16_t *row = target->pixels + (size_t)y * target->stride_pixels;
                uint16_t *depth = target->depth_pixels == NULL ? NULL :
                    target->depth_pixels + (size_t)y * target->depth_stride_pixels;
                uint16_t x;
                for (x = 0; x < target->width; ++x) {
                    row[x] = color;
                    if (depth != NULL) depth[x] = 0;
                }
            }
            covered += (uint32_t)target->width * target->height;
            if (telemetry != NULL) ++telemetry->clear_commands;
        } else if (record[0] == PXA_RASTER_RECORD_FLAT_QUAD) {
            covered += draw_quad(record, 0, list->abi_minor, target, resources);
            if (telemetry != NULL) ++telemetry->flat_quad_commands;
        } else if (record[0] == PXA_RASTER_RECORD_TEXTURED_QUAD) {
            covered += draw_quad(record, 1, list->abi_minor, target, resources);
            if (telemetry != NULL) ++telemetry->textured_quad_commands;
        } else if (record[0] == PXA_RASTER_RECORD_SPRITE) {
            covered += draw_sprite(record, target, resources);
            if (telemetry != NULL) ++telemetry->sprite_commands;
        }
        offset += size;
    }
    if (telemetry != NULL) {
        telemetry->draw_list_bytes += list->total_size;
        telemetry->covered_pixels += covered;
        telemetry->last_draw_list_bytes = list->total_size;
        telemetry->last_covered_pixels = covered;
    }
}

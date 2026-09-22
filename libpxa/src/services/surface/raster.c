#include "pxa/raster.h"

#include <limits.h>
#include <string.h>

#include "pxa/wire.h"

#if defined(__GNUC__) || defined(__clang__)
#define RASTER_NOINLINE __attribute__((noinline))
#else
#define RASTER_NOINLINE
#endif

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
    /* Exact floor(x / 255) for x < 65536 without a hardware divide. */
    uint32_t red = (color >> 11) * light + 127u;
    uint32_t green = ((color >> 5) & 63u) * light + 127u;
    uint32_t blue = (color & 31u) * light + 127u;
    red = (red + 1u + ((red + 1u) >> 8)) >> 8;
    green = (green + 1u + ((green + 1u) >> 8)) >> 8;
    blue = (blue + 1u + ((blue + 1u) >> 8)) >> 8;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static void fill_rgb565(uint16_t *pixels, uint32_t count, uint16_t color) {
    const uint32_t pair = (uint32_t)color | ((uint32_t)color << 16);
    while (count >= 2u) {
        memcpy(pixels, &pair, sizeof(pair));
        pixels += 2;
        count -= 2u;
    }
    if (count != 0) *pixels = color;
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
    } else if (kind == PXA_RASTER_UPLOAD_LIT_PALETTE_RGB565) {
        expected = (uint64_t)PXA_RASTER_PALETTE_COLORS * height *
                   sizeof(uint16_t);
        if (slot != 0 || width != PXA_RASTER_PALETTE_COLORS || height == 0 ||
            height > 256u || expected != payload_bytes)
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
    uint8_t painter = 0;
    uint8_t lit_palette = 0;
    uint32_t offset;
    if ((!textured && record_size != PXA_RASTER_FLAT_QUAD_BYTES) ||
        (textured && record_size != PXA_RASTER_TEXTURED_QUAD_BYTES))
        return PXA_STATUS_PROTOCOL_ERROR;
    if (textured) {
        const uint8_t flags = record[1];
        const uint8_t slot = record[4];
        const uint8_t solid = flags & PXA_RASTER_QUAD_SOLID_COLOR;
        const uint8_t affine = flags & PXA_RASTER_QUAD_AFFINE_UV;
        const uint8_t transparent =
            flags & PXA_RASTER_QUAD_TRANSPARENT_INDEX0;
        const uint8_t blend = flags & PXA_RASTER_QUAD_BLEND_75;
        lit_palette = flags & PXA_RASTER_QUAD_LIT_PALETTE;
        painter = flags & PXA_RASTER_QUAD_PAINTER;
        if ((flags & ~(PXA_RASTER_QUAD_SOLID_COLOR |
                       PXA_RASTER_QUAD_AFFINE_UV |
                       PXA_RASTER_QUAD_PAINTER |
                       PXA_RASTER_QUAD_TRANSPARENT_INDEX0 |
                       PXA_RASTER_QUAD_LIT_PALETTE |
                       PXA_RASTER_QUAD_BLEND_75)) != 0 ||
            (solid != 0 && abi_minor < 1))
            return PXA_STATUS_UNSUPPORTED;
        if (painter != 0 &&
            (abi_minor < 3 ||
             (resources->capabilities & PXA_RASTER_CAP_PAINTER_POLYGON) == 0 ||
             resources->palette == NULL || resources->palette_light_levels == 0))
            return PXA_STATUS_UNSUPPORTED;
        if (lit_palette != 0 &&
            (abi_minor < 4 || painter != 0 || solid != 0 ||
             (resources->capabilities &
              PXA_RASTER_CAP_LIT_PALETTE_DEPTH) == 0 ||
             resources->palette == NULL ||
             resources->palette_light_levels == 0))
            return PXA_STATUS_UNSUPPORTED;
        if (transparent != 0 && painter == 0 &&
            (abi_minor < 5 ||
             (resources->capabilities & PXA_RASTER_CAP_DEPTH_CUTOUT) == 0))
            return PXA_STATUS_UNSUPPORTED;
        if (blend != 0 &&
            (abi_minor < 5 ||
             (resources->capabilities &
              PXA_RASTER_CAP_FIXED_ALPHA_BLEND) == 0))
            return PXA_STATUS_UNSUPPORTED;
        if (affine != 0) {
            if ((resources->capabilities & PXA_RASTER_CAP_AFFINE_UV) == 0)
                return PXA_STATUS_UNSUPPORTED;
            if (solid != 0 && painter == 0) return PXA_STATUS_PROTOCOL_ERROR;
        }
        if (record[5] != 0 || (solid == 0 && read_u16(record + 6) != 0) ||
            (solid != 0 && painter != 0 && read_u16(record + 6) > 255u) ||
            (solid != 0 &&
             (flags & (PXA_RASTER_QUAD_TRANSPARENT_INDEX0 |
                       PXA_RASTER_QUAD_BLEND_75)) != 0))
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
                ((painter != 0 || lit_palette != 0) &&
                 vertices[index].light >= resources->palette_light_levels) ||
                (painter == 0 && abi_minor == 0 && vertices[index].depth != 0) ||
                (painter == 0 && abi_minor >= 1 && vertices[index].depth == 0))
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

static pxa_status_t validate_sprite_batch(
    const uint8_t *record, uint16_t size,
    const pxa_raster_resources_t *resources) {
    const uint8_t flags = record[1];
    const uint8_t slot = record[4];
    const uint8_t known = PXA_RASTER_SPRITE_TRANSPARENT_INDEX0 |
                          PXA_RASTER_SPRITE_SOLID_COLOR |
                          PXA_RASTER_SPRITE_ADDITIVE;
    const uint16_t count = read_u16(record + 8);
    uint16_t index;
    if (size < PXA_RASTER_SPRITE_BATCH_HEADER_BYTES || count == 0 ||
        size != PXA_RASTER_SPRITE_BATCH_HEADER_BYTES +
                    (uint32_t)count * PXA_RASTER_SPRITE_INSTANCE_BYTES)
        return PXA_STATUS_PROTOCOL_ERROR;
    if ((flags & ~known) != 0) return PXA_STATUS_UNSUPPORTED;
    if ((flags & PXA_RASTER_SPRITE_ADDITIVE) != 0 &&
        (resources->capabilities & PXA_RASTER_CAP_ADDITIVE_SPRITE) == 0)
        return PXA_STATUS_UNSUPPORTED;
    if (slot >= PXA_RASTER_MAX_TEXTURES ||
        resources->textures[slot].pixels == NULL || resources->palette == NULL)
        return PXA_STATUS_BAD_STATE;
    if (record[5] != 0 || read_u16(record + 10) != 0 ||
        ((flags & PXA_RASTER_SPRITE_SOLID_COLOR) == 0 &&
         read_u16(record + 6) != 0))
        return PXA_STATUS_PROTOCOL_ERROR;
    for (index = 0; index < count; ++index) {
        const uint8_t *instance =
            record + PXA_RASTER_SPRITE_BATCH_HEADER_BYTES +
            (uint32_t)index * PXA_RASTER_SPRITE_INSTANCE_BYTES;
        const uint32_t source_right =
            (uint32_t)read_u16(instance + 8) + read_u16(instance + 12);
        const uint32_t source_bottom =
            (uint32_t)read_u16(instance + 10) + read_u16(instance + 14);
        if (read_u16(instance + 4) == 0 || read_u16(instance + 6) == 0 ||
            read_u16(instance + 12) == 0 || read_u16(instance + 14) == 0 ||
            source_right > resources->textures[slot].width ||
            source_bottom > resources->textures[slot].height)
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t validate_triangle_batch(
    const uint8_t *record, uint16_t size, uint16_t abi_minor,
    const pxa_raster_resources_t *resources) {
    const uint8_t flags = record[1];
    const uint8_t slot = record[4];
    const uint16_t count = read_u16(record + 8);
    const uint8_t solid = flags & PXA_RASTER_QUAD_SOLID_COLOR;
    const uint8_t painter = flags & PXA_RASTER_QUAD_PAINTER;
    const uint8_t lit_palette = flags & PXA_RASTER_QUAD_LIT_PALETTE;
    const uint8_t transparent =
        flags & PXA_RASTER_QUAD_TRANSPARENT_INDEX0;
    const uint8_t blend = flags & PXA_RASTER_QUAD_BLEND_75;
    uint16_t triangle;
    if (size < PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES || count == 0 ||
        size != PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES +
                    (uint32_t)count * 3u * PXA_RASTER_VERTEX_BYTES)
        return PXA_STATUS_PROTOCOL_ERROR;
    if ((flags & ~(PXA_RASTER_QUAD_SOLID_COLOR |
                   PXA_RASTER_QUAD_AFFINE_UV |
                   PXA_RASTER_QUAD_PAINTER |
                   PXA_RASTER_QUAD_TRANSPARENT_INDEX0 |
                   PXA_RASTER_QUAD_LIT_PALETTE |
                   PXA_RASTER_QUAD_BLEND_75)) != 0 ||
        record[5] != 0 ||
        read_u16(record + 10) != 0 ||
        (solid == 0 && read_u16(record + 6) != 0) ||
        (solid != 0 && (flags & PXA_RASTER_QUAD_PAINTER) != 0 &&
         read_u16(record + 6) > 255u) ||
        (solid != 0 &&
         (flags & (PXA_RASTER_QUAD_TRANSPARENT_INDEX0 |
                   PXA_RASTER_QUAD_BLEND_75)) != 0))
        return PXA_STATUS_PROTOCOL_ERROR;
    if (painter != 0 &&
        ((resources->capabilities & PXA_RASTER_CAP_PAINTER_POLYGON) == 0 ||
         resources->palette == NULL || resources->palette_light_levels == 0))
        return PXA_STATUS_UNSUPPORTED;
    if (lit_palette != 0 &&
        (abi_minor < 4 || painter != 0 || solid != 0 ||
         (resources->capabilities & PXA_RASTER_CAP_LIT_PALETTE_DEPTH) == 0 ||
         resources->palette == NULL || resources->palette_light_levels == 0))
        return PXA_STATUS_UNSUPPORTED;
    if (transparent != 0 && painter == 0 &&
        (abi_minor < 5 ||
         (resources->capabilities & PXA_RASTER_CAP_DEPTH_CUTOUT) == 0))
        return PXA_STATUS_UNSUPPORTED;
    if (blend != 0 &&
        (abi_minor < 5 ||
         (resources->capabilities & PXA_RASTER_CAP_FIXED_ALPHA_BLEND) == 0))
        return PXA_STATUS_UNSUPPORTED;
    if (solid == 0 &&
        (slot >= PXA_RASTER_MAX_TEXTURES || resources->palette == NULL ||
         resources->textures[slot].pixels == NULL))
        return PXA_STATUS_BAD_STATE;
    for (triangle = 0; triangle < count; ++triangle) {
        raster_vertex_t vertices[3];
        uint8_t vertex;
        int64_t area;
        for (vertex = 0; vertex < 3; ++vertex) {
            const uint8_t *wire =
                record + PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES +
                ((uint32_t)triangle * 3u + vertex) * PXA_RASTER_VERTEX_BYTES;
            decode_vertex(wire, &vertices[vertex]);
            if (wire[9] != 0 ||
                ((painter != 0 || lit_palette != 0) &&
                 vertices[vertex].light >= resources->palette_light_levels) ||
                (painter != 0 ? vertices[vertex].depth != 0
                              : vertices[vertex].depth == 0))
                return PXA_STATUS_PROTOCOL_ERROR;
        }
        area = (int64_t)(vertices[1].x - vertices[0].x) *
                   (vertices[2].y - vertices[0].y) -
               (int64_t)(vertices[1].y - vertices[0].y) *
                   (vertices[2].x - vertices[0].x);
        if (area == 0) return PXA_STATUS_INVALID_ARGUMENT;
    }
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
        } else if (type == PXA_RASTER_RECORD_SPRITE_BATCH) {
            if ((required & PXA_RASTER_CAP_SPRITE_BATCH) == 0)
                status = PXA_STATUS_PROTOCOL_ERROR;
            else
                status = validate_sprite_batch(bytes + offset, record_size,
                                               resources);
        } else if (type == PXA_RASTER_RECORD_TRIANGLE_BATCH) {
            if ((required & PXA_RASTER_CAP_TRIANGLE_BATCH) == 0)
                status = PXA_STATUS_PROTOCOL_ERROR;
            else
                status = validate_triangle_batch(bytes + offset, record_size,
                                                 abi_minor, resources);
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

static int edge_is_inclusive(const raster_vertex_t *a,
                             const raster_vertex_t *b, int64_t sign) {
    const int32_t dx = (b->x - a->x) * (int32_t)sign;
    const int32_t dy = (b->y - a->y) * (int32_t)sign;
    return dy < 0 || (dy == 0 && dx > 0);
}

#define RASTER_RECIPROCAL_DEPTH_ONE UINT32_C(524288)
#define RASTER_INTERPOLANT_SCALE INT64_C(16)
#define RASTER_AREA_RECIPROCAL_BITS 35

static int64_t scaled_divide(int64_t numerator, int64_t denominator,
                             int64_t scale) {
    const int64_t quotient = numerator / denominator;
    const int64_t remainder = numerator % denominator;
    return quotient * scale + remainder * scale / denominator;
}

static RASTER_NOINLINE int64_t scale_interpolant(
    int64_t numerator, int64_t denominator, int64_t area_reciprocal,
    uint8_t scale_bits) {
    const int64_t scale = INT64_C(1) << scale_bits;
    int64_t product;
#if defined(__GNUC__) || defined(__clang__)
    const int product_fits =
        !__builtin_mul_overflow(numerator, area_reciprocal, &product);
#else
    const int64_t limit = INT64_MAX / area_reciprocal;
    const int product_fits = numerator >= -limit && numerator <= limit;
    if (product_fits) product = numerator * area_reciprocal;
#endif
    if (product_fits)
        return product /
               (INT64_C(1) <<
                (RASTER_AREA_RECIPROCAL_BITS - scale_bits));
    return scaled_divide(numerator, denominator, scale);
}

static int64_t reciprocal_depth(uint16_t depth) {
    return depth == 0 ? 0 : (int64_t)(RASTER_RECIPROCAL_DEPTH_ONE / depth);
}

#define RASTER_PERSPECTIVE_BLOCK_MIN_PIXELS 8
#define RASTER_PERSPECTIVE_BLOCK_MAX_PIXELS 16
#define RASTER_PERSPECTIVE_BLOCK_DEPTH_SHIFT 3
#define RASTER_TEXTURE_Q8_FROM_UV_Q4 16
/* draw_triangle interpolation modes. Untextured triangles ignore
 * RASTER_MODE_PERSPECTIVE_UV. */
#define RASTER_MODE_PERSPECTIVE_UV UINT8_C(1)
#define RASTER_MODE_DEPTH UINT8_C(2)
#define RASTER_MODE_LIT_PALETTE UINT8_C(4)
#define RASTER_MODE_TRANSPARENT_INDEX0 UINT8_C(8)
#define RASTER_MODE_BLEND_75 UINT8_C(16)

static inline uint16_t blend_rgb565_75(uint16_t destination,
                                       uint16_t source) {
    return (uint16_t)(((destination & UINT16_C(0xe79c)) >> 2) +
                      ((source & UINT16_C(0xf7de)) >> 1) +
                      ((source & UINT16_C(0xe79c)) >> 2));
}

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

static int32_t perspective_block_pixels(int32_t remaining,
                                        int64_t reciprocal,
                                        int64_t reciprocal_dx) {
    int32_t pixels = remaining;
    uint64_t magnitude;
    uint64_t delta;
    if (pixels > RASTER_PERSPECTIVE_BLOCK_MAX_PIXELS)
        pixels = RASTER_PERSPECTIVE_BLOCK_MAX_PIXELS;
    if (pixels <= RASTER_PERSPECTIVE_BLOCK_MIN_PIXELS) return pixels;
    if (reciprocal <= 0) return RASTER_PERSPECTIVE_BLOCK_MIN_PIXELS;
    magnitude = (uint64_t)reciprocal;
    delta = reciprocal_dx < 0
                ? (uint64_t)(-(reciprocal_dx + 1)) + UINT64_C(1)
                : (uint64_t)reciprocal_dx;
    if (delta > UINT64_MAX / (uint32_t)pixels ||
        delta * (uint32_t)pixels >
            (magnitude >> RASTER_PERSPECTIVE_BLOCK_DEPTH_SHIFT))
        return RASTER_PERSPECTIVE_BLOCK_MIN_PIXELS;
    return pixels;
}

static inline int32_t wrap_texture_coordinate(int32_t coordinate,
                                              uint16_t dimension,
                                              uint8_t power_of_two) {
    if (power_of_two)
        return (int32_t)((uint32_t)coordinate & (dimension - 1u));
    /* Voxel Craft uses a 16x48 three-face atlas. Keeping 48 visible here lets
     * the compiler replace the modulo with reciprocal multiplication instead
     * of emitting a variable integer divide in the per-pixel loop. */
    if (dimension == 48u)
        coordinate %= 48;
    else
        coordinate %= dimension;
    return coordinate < 0 ? coordinate + dimension : coordinate;
}

static uint32_t draw_triangle(const raster_vertex_t *a,
                              const raster_vertex_t *b,
                              const raster_vertex_t *c, uint16_t flat_color,
                              const pxa_raster_texture_t *texture,
                              const uint16_t *palette, uint8_t mode,
                              const pxa_raster_target_t *target,
                              uint16_t row_begin, uint16_t row_end) {
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
    int64_t qa = 0;
    int64_t qb = 0;
    int64_t qc = 0;
    const uint8_t depth_test =
        (mode & RASTER_MODE_DEPTH) != 0 && target->depth_pixels != NULL;
    const uint8_t perspective_uv =
        texture != NULL && (mode & RASTER_MODE_PERSPECTIVE_UV) != 0;
    const uint8_t lit_palette =
        texture != NULL && (mode & RASTER_MODE_LIT_PALETTE) != 0;
    const uint8_t transparent_index0 =
        texture != NULL && (mode & RASTER_MODE_TRANSPARENT_INDEX0) != 0;
    const uint8_t blend_75 =
        texture != NULL && (mode & RASTER_MODE_BLEND_75) != 0;
    /* Voxel faces carry one light value for the whole primitive. Detecting it
     * drops three scale_interpolant setups per triangle plus the per-pixel
     * light accumulator. */
    const uint8_t constant_light =
        texture != NULL && a->light == b->light && b->light == c->light;
    const uint8_t need_reciprocal = perspective_uv || depth_test;
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
    if (min_y < row_begin) min_y = row_begin;
    if (max_y > row_end) max_y = row_end;
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
        if (texture != NULL && !constant_light) {
            light_row = scale_interpolant(
                w0_row * a->light + w1_row * b->light + w2_row * c->light,
                denominator, area_reciprocal, 12);
            light_dx = scale_interpolant(
                w0_dx * a->light + w1_dx * b->light + w2_dx * c->light,
                denominator, area_reciprocal, 12);
            light_dy = scale_interpolant(
                w0_dy * a->light + w1_dy * b->light + w2_dy * c->light,
                denominator, area_reciprocal, 12);
        }
        if (texture != NULL && !perspective_uv) {
            u_row = scale_interpolant(
                w0_row * a->u + w1_row * b->u + w2_row * c->u,
                denominator, area_reciprocal, 12);
            v_row = scale_interpolant(
                w0_row * a->v + w1_row * b->v + w2_row * c->v,
                denominator, area_reciprocal, 12);
            u_dx = scale_interpolant(
                w0_dx * a->u + w1_dx * b->u + w2_dx * c->u,
                denominator, area_reciprocal, 12);
            v_dx = scale_interpolant(
                w0_dx * a->v + w1_dx * b->v + w2_dx * c->v,
                denominator, area_reciprocal, 12);
            u_dy = scale_interpolant(
                w0_dy * a->u + w1_dy * b->u + w2_dy * c->u,
                denominator, area_reciprocal, 12);
            v_dy = scale_interpolant(
                w0_dy * a->v + w1_dy * b->v + w2_dy * c->v,
                denominator, area_reciprocal, 12);
        }
        if (need_reciprocal) {
            qa = reciprocal_depth(a->depth);
            qb = reciprocal_depth(b->depth);
            qc = reciprocal_depth(c->depth);
            reciprocal_row = scale_interpolant(
                w0_row * qa + w1_row * qb + w2_row * qc, denominator,
                area_reciprocal, 4);
            reciprocal_dx = scale_interpolant(
                w0_dx * qa + w1_dx * qb + w2_dx * qc, denominator,
                area_reciprocal, 4);
            reciprocal_dy = scale_interpolant(
                w0_dy * qa + w1_dy * qb + w2_dy * qc, denominator,
                area_reciprocal, 4);
        }
        if (perspective_uv) {
            const int64_t uqa = (int64_t)a->u * qa;
            const int64_t uqb = (int64_t)b->u * qb;
            const int64_t uqc = (int64_t)c->u * qc;
            const int64_t vqa = (int64_t)a->v * qa;
            const int64_t vqb = (int64_t)b->v * qb;
            const int64_t vqc = (int64_t)c->v * qc;
            u_reciprocal_row = scale_interpolant(
                w0_row * uqa + w1_row * uqb + w2_row * uqc, denominator,
                area_reciprocal, 4);
            u_reciprocal_dx = scale_interpolant(
                w0_dx * uqa + w1_dx * uqb + w2_dx * uqc, denominator,
                area_reciprocal, 4);
            u_reciprocal_dy = scale_interpolant(
                w0_dy * uqa + w1_dy * uqb + w2_dy * uqc, denominator,
                area_reciprocal, 4);
            v_reciprocal_row = scale_interpolant(
                w0_row * vqa + w1_row * vqb + w2_row * vqc, denominator,
                area_reciprocal, 4);
            v_reciprocal_dx = scale_interpolant(
                w0_dx * vqa + w1_dx * vqb + w2_dx * vqc, denominator,
                area_reciprocal, 4);
            v_reciprocal_dy = scale_interpolant(
                w0_dy * vqa + w1_dy * vqb + w2_dy * vqc, denominator,
                area_reciprocal, 4);
        }
        /* Blended adjacent triangles must own their shared edge exactly once;
         * otherwise the diagonal is averaged twice and appears darker. The
         * bias only affects coverage, after interpolants use the exact edge
         * values. */
        if (blend_75) {
            if (!edge_is_inclusive(b, c, sign)) --w0_row;
            if (!edge_is_inclusive(c, a, sign)) --w1_row;
            if (!edge_is_inclusive(a, b, sign)) --w2_row;
        }
    }
    if (texture == NULL) {
        /* Flat 2D rectangle or solid depth quad: no UV or light interpolants
         * at all. */
        for (y = min_y; y < max_y; ++y) {
            uint16_t *row = target->pixels + (size_t)y * target->stride_pixels;
            uint16_t *depth_row_pixels = target->depth_pixels == NULL
                ? NULL
                : target->depth_pixels + (size_t)y * target->depth_stride_pixels;
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
                if (depth_test) {
                    const int32_t offset = span_start - min_x;
                    int64_t reciprocal =
                        reciprocal_row + reciprocal_dx * offset;
                    int32_t x;
                    for (x = span_start; x < span_end; ++x) {
                        int64_t inverse_depth =
                            reciprocal / RASTER_INTERPOLANT_SCALE;
                        uint16_t pixel_depth;
                        if (inverse_depth < 0) inverse_depth = 0;
                        if (inverse_depth > UINT16_MAX)
                            inverse_depth = UINT16_MAX;
                        pixel_depth = (uint16_t)inverse_depth;
                        if (pixel_depth >= depth_row_pixels[x]) {
                            row[x] = flat_color;
                            depth_row_pixels[x] = pixel_depth;
                            ++covered;
                        }
                        reciprocal += reciprocal_dx;
                    }
                } else {
                    int32_t x;
                    for (x = span_start; x < span_end; ++x)
                        row[x] = flat_color;
                    covered += (uint32_t)(span_end - span_start);
                }
            }
            w0_row += w0_dy;
            w1_row += w1_dy;
            w2_row += w2_dy;
            reciprocal_row += reciprocal_dy;
        }
        return covered;
    }
    if (!perspective_uv) {
        /* Screen-linear UV, optionally with the perspective depth
         * interpolant kept for occlusion. */
        for (y = min_y; y < max_y; ++y) {
            uint16_t *row = target->pixels + (size_t)y * target->stride_pixels;
            uint16_t *depth_row_pixels = target->depth_pixels == NULL
                ? NULL
                : target->depth_pixels + (size_t)y * target->depth_stride_pixels;
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
                int32_t x;
                for (x = span_start; x < span_end; ++x) {
                    uint16_t pixel_depth = 0;
                    uint8_t visible = 1;
                    int32_t tx;
                    int32_t ty;
                    int32_t intensity;
                    if (depth_test) {
                        int64_t inverse_depth =
                            reciprocal / RASTER_INTERPOLANT_SCALE;
                        if (inverse_depth < 0) inverse_depth = 0;
                        if (inverse_depth > UINT16_MAX)
                            inverse_depth = UINT16_MAX;
                        pixel_depth = (uint16_t)inverse_depth;
                        visible = pixel_depth >= depth_row_pixels[x];
                    }
                    if (visible) {
                        tx = (int32_t)(u >> 16);
                        ty = (int32_t)(v >> 16);
                        tx = wrap_texture_coordinate(
                            tx, texture->width, width_power_of_two);
                        ty = wrap_texture_coordinate(
                            ty, texture->height, height_power_of_two);
                        if (constant_light) {
                            intensity = (int32_t)a->light;
                        } else {
                            intensity = (int32_t)(light >> 12);
                            if (intensity < 0) intensity = 0;
                            if (intensity > 255) intensity = 255;
                        }
                        {
                            const uint8_t texel = texture->pixels[
                                (size_t)ty * texture->width + tx];
                            if (!transparent_index0 || texel != 0) {
                                const uint16_t source =
                                    lit_palette
                                        ? palette[((uint32_t)(uint8_t)intensity
                                                   << 8) |
                                                  texel]
                                        : light_rgb565(
                                              palette[texel],
                                              (uint8_t)intensity);
                                row[x] = blend_75
                                             ? blend_rgb565_75(row[x], source)
                                             : source;
                                if (depth_test && !blend_75)
                                    depth_row_pixels[x] = pixel_depth;
                                ++covered;
                            }
                        }
                    }
                    u += u_dx;
                    v += v_dx;
                    if (depth_test) reciprocal += reciprocal_dx;
                    if (!constant_light) light += light_dx;
                }
            }
            w0_row += w0_dy;
            w1_row += w1_dy;
            w2_row += w2_dy;
            u_row += u_dy;
            v_row += v_dy;
            light_row += light_dy;
            reciprocal_row += reciprocal_dy;
        }
        return covered;
    }
    /* Perspective-correct UV and depth. The reciprocal depth interpolant is
     * divided into short screen-space blocks so the expensive per-texel
     * perspective divide runs once per block instead of once per pixel. */
    for (y = min_y; y < max_y; ++y) {
        uint16_t *row = target->pixels + (size_t)y * target->stride_pixels;
        uint16_t *depth_row_pixels = target->depth_pixels == NULL
            ? NULL
            : target->depth_pixels + (size_t)y * target->depth_stride_pixels;
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
            int64_t light = light_row + light_dx * offset;
            int64_t reciprocal = reciprocal_row + reciprocal_dx * offset;
            int64_t u_reciprocal =
                u_reciprocal_row + u_reciprocal_dx * offset;
            int64_t v_reciprocal =
                v_reciprocal_row + v_reciprocal_dx * offset;
            int32_t u_texture_q8 = perspective_texture_q8(
                u_reciprocal, reciprocal);
            int32_t v_texture_q8 = perspective_texture_q8(
                v_reciprocal, reciprocal);
            int32_t x = span_start;
            while (x < span_end) {
                int32_t u_texture_step_q8 = 0;
                int32_t v_texture_step_q8 = 0;
                int32_t u_texture_end_q8 = 0;
                int32_t v_texture_end_q8 = 0;
                int32_t block_pixels = perspective_block_pixels(
                    span_end - x, reciprocal, reciprocal_dx);
                int32_t block_index;
                {
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
                    int64_t inverse_depth =
                        reciprocal / RASTER_INTERPOLANT_SCALE;
                    uint16_t pixel_depth;
                    if (inverse_depth < 0) inverse_depth = 0;
                    if (inverse_depth > UINT16_MAX)
                        inverse_depth = UINT16_MAX;
                    pixel_depth = (uint16_t)inverse_depth;
                    if (!depth_test || pixel_depth >= depth_row_pixels[x]) {
                        int32_t tx = u_texture_q8 >> 8;
                        int32_t ty = v_texture_q8 >> 8;
                        int32_t intensity;
                        tx = wrap_texture_coordinate(
                            tx, texture->width, width_power_of_two);
                        ty = wrap_texture_coordinate(
                            ty, texture->height, height_power_of_two);
                        if (constant_light) {
                            intensity = (int32_t)a->light;
                        } else {
                            intensity = (int32_t)(light >> 12);
                            if (intensity < 0) intensity = 0;
                            if (intensity > 255) intensity = 255;
                        }
                        {
                            const uint8_t texel = texture->pixels[
                                (size_t)ty * texture->width + tx];
                            if (!transparent_index0 || texel != 0) {
                                const uint16_t source =
                                    lit_palette
                                        ? palette[((uint32_t)(uint8_t)intensity
                                                   << 8) |
                                                  texel]
                                        : light_rgb565(
                                              palette[texel],
                                              (uint8_t)intensity);
                                row[x] = blend_75
                                             ? blend_rgb565_75(row[x], source)
                                             : source;
                                if (depth_test && !blend_75)
                                    depth_row_pixels[x] = pixel_depth;
                                ++covered;
                            }
                        }
                    }
                    if (!constant_light) light += light_dx;
                    reciprocal += reciprocal_dx;
                    u_reciprocal += u_reciprocal_dx;
                    v_reciprocal += v_reciprocal_dx;
                    u_texture_q8 += u_texture_step_q8;
                    v_texture_q8 += v_texture_step_q8;
                }
                u_texture_q8 = u_texture_end_q8;
                v_texture_q8 = v_texture_end_q8;
            }
        }
        w0_row += w0_dy;
        w1_row += w1_dy;
        w2_row += w2_dy;
        light_row += light_dy;
        reciprocal_row += reciprocal_dy;
        u_reciprocal_row += u_reciprocal_dy;
        v_reciprocal_row += v_reciprocal_dy;
    }
    return covered;
}

#define RASTER_PAINTER_FIXED_SHIFT 16
#define RASTER_PAINTER_HALF_SUBPIXEL 8

typedef struct {
    int32_t x, u, v, light;
    int32_t dx, du, dv, dlight;
    int32_t end_row;
} raster_painter_edge_t;

typedef struct {
    const raster_vertex_t *vertices;
    uint8_t count;
    uint8_t index;
    uint8_t remaining;
    uint8_t forward;
    raster_painter_edge_t edge;
} raster_painter_chain_t;

static int32_t painter_row_ceil(int32_t y_q4) {
    return (y_q4 + RASTER_PAINTER_HALF_SUBPIXEL - 1) >> 4;
}

static int32_t painter_column_ceil(int32_t x_q16) {
    return (x_q16 + 0x7fff) >> RASTER_PAINTER_FIXED_SHIFT;
}

static int painter_setup_edge(const raster_vertex_t *a,
                              const raster_vertex_t *b, int32_t start_row,
                              raster_painter_edge_t *edge) {
    const int32_t dy = (int32_t)b->y - a->y;
    int32_t first_row;
    float per_row;
    int32_t u0;
    int32_t v0;
    int32_t light0;
    int64_t rows16;
    if (dy <= 0) return 0;
    first_row = painter_row_ceil(a->y);
    if (first_row < start_row) first_row = start_row;
    edge->end_row = painter_row_ceil(b->y);
    if (edge->end_row <= first_row) return 0;
    per_row = 16.0f / (float)dy;
    edge->dx = (int32_t)((float)((int32_t)b->x - a->x) * per_row * 4096.0f);
    u0 = (int32_t)a->u * 4096;
    v0 = (int32_t)a->v * 4096;
    light0 = (int32_t)a->light * 65536;
    edge->du = (int32_t)((float)((int32_t)b->u * 4096 - u0) * per_row);
    edge->dv = (int32_t)((float)((int32_t)b->v * 4096 - v0) * per_row);
    edge->dlight =
        (int32_t)((float)((int32_t)b->light * 65536 - light0) * per_row);
    rows16 = ((int64_t)first_row << 4) + RASTER_PAINTER_HALF_SUBPIXEL - a->y;
#define PXA_PAINTER_EDGE_AT(start, step) \
    ((start) + (int32_t)(((int64_t)(step) * rows16) >> 4))
    edge->x = PXA_PAINTER_EDGE_AT((int32_t)a->x * 4096, edge->dx);
    edge->u = PXA_PAINTER_EDGE_AT(u0, edge->du);
    edge->v = PXA_PAINTER_EDGE_AT(v0, edge->dv);
    edge->light = PXA_PAINTER_EDGE_AT(light0, edge->dlight);
#undef PXA_PAINTER_EDGE_AT
    return 1;
}

static int painter_advance_chain(raster_painter_chain_t *chain, int32_t row) {
    while (chain->remaining != 0) {
        uint8_t next;
        --chain->remaining;
        if (chain->forward)
            next = chain->index + 1u == chain->count ? 0u
                                                     : chain->index + 1u;
        else
            next = chain->index == 0 ? chain->count - 1u
                                     : chain->index - 1u;
        if (painter_setup_edge(&chain->vertices[chain->index],
                               &chain->vertices[next], row, &chain->edge)) {
            chain->index = next;
            return 1;
        }
        chain->index = next;
    }
    return 0;
}

static void painter_step_edge(raster_painter_edge_t *edge) {
    edge->x += edge->dx;
    edge->u += edge->du;
    edge->v += edge->dv;
    edge->light += edge->dlight;
}

static uint8_t painter_texture_log2(uint16_t dimension) {
    uint8_t result = 0;
    if (dimension == 0 || (dimension & (dimension - 1u)) != 0)
        return UINT8_MAX;
    while ((UINT32_C(1) << result) != dimension) ++result;
    return result;
}

static inline size_t painter_texture_index_pow2(
    uint32_t u, uint32_t v, uint32_t mask_u, uint32_t mask_v,
    uint8_t log2_width) {
    return (size_t)(((v >> RASTER_PAINTER_FIXED_SHIFT) & mask_v)
                    << log2_width) |
           ((u >> RASTER_PAINTER_FIXED_SHIFT) & mask_u);
}

static uint32_t draw_painter_polygon(
    const raster_vertex_t *vertices, uint8_t count, uint8_t flags,
    uint8_t color_index, const pxa_raster_texture_t *texture,
    const uint16_t *palette, const pxa_raster_target_t *target,
    uint16_t row_begin, uint16_t row_end) {
    int64_t area = 0;
    uint8_t top = 0;
    int32_t min_y = vertices[0].y;
    int32_t max_y = vertices[0].y;
    uint8_t min_light = vertices[0].light;
    uint8_t max_light = vertices[0].light;
    int32_t row;
    int32_t end;
    raster_painter_chain_t left;
    raster_painter_chain_t right;
    uint32_t covered = 0;
    uint8_t i;
    const int flat = (flags & PXA_RASTER_QUAD_SOLID_COLOR) != 0;
    const int transparent =
        (flags & PXA_RASTER_QUAD_TRANSPARENT_INDEX0) != 0;
    const int blend_75 = (flags & PXA_RASTER_QUAD_BLEND_75) != 0;
    const uint8_t texture_log2_width =
        flat ? UINT8_MAX : painter_texture_log2(texture->width);
    const uint8_t texture_log2_height =
        flat ? UINT8_MAX : painter_texture_log2(texture->height);
    const int texture_power_of_two =
        texture_log2_width != UINT8_MAX &&
        texture_log2_height != UINT8_MAX;
    const uint32_t texture_mask_u =
        texture_power_of_two ? texture->width - 1u : 0u;
    const uint32_t texture_mask_v =
        texture_power_of_two ? texture->height - 1u : 0u;
    for (i = 0; i < count; ++i) {
        const raster_vertex_t *a = &vertices[i];
        const raster_vertex_t *b = &vertices[i + 1u == count ? 0u : i + 1u];
        area += (int64_t)a->x * b->y - (int64_t)b->x * a->y;
        if (a->y < min_y) {
            min_y = a->y;
            top = i;
        }
        if (a->y > max_y) max_y = a->y;
        if (a->light < min_light) min_light = a->light;
        if (a->light > max_light) max_light = a->light;
    }
    if (area == 0 || (!flat && (texture == NULL || texture->pixels == NULL)))
        return 0;
    row = painter_row_ceil(min_y);
    if (row < row_begin) row = row_begin;
    end = painter_row_ceil(max_y);
    if (end > row_end) end = row_end;
    if (row >= end) return 0;
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    left.vertices = right.vertices = vertices;
    left.count = right.count = count;
    left.index = right.index = top;
    left.remaining = right.remaining = count - 1u;
    left.forward = area < 0;
    right.forward = area > 0;
    if (!painter_advance_chain(&left, row) ||
        !painter_advance_chain(&right, row))
        return 0;
    for (;;) {
        const raster_painter_edge_t *l = &left.edge;
        const raster_painter_edge_t *r = &right.edge;
        int32_t width;
        int32_t x0;
        int32_t x1;
        if (l->x > r->x) {
            const raster_painter_edge_t *swap = l;
            l = r;
            r = swap;
        }
        width = r->x - l->x;
        x0 = painter_column_ceil(l->x);
        x1 = painter_column_ceil(r->x);
        if (x0 < 0) x0 = 0;
        if (x1 > target->width) x1 = target->width;
        if (width > 0 && x1 > x0) {
            const float per_pixel = 65536.0f / (float)width;
            const int32_t du = (int32_t)((float)(r->u - l->u) * per_pixel);
            const int32_t dv = (int32_t)((float)(r->v - l->v) * per_pixel);
            int32_t dlight =
                (int32_t)((float)(r->light - l->light) * per_pixel);
            const int64_t prestep = ((int64_t)x0 << 16) + 0x8000 - l->x;
            uint32_t u = (uint32_t)(l->u +
                (int32_t)(((int64_t)du * prestep) >> 16));
            uint32_t v = (uint32_t)(l->v +
                (int32_t)(((int64_t)dv * prestep) >> 16));
            int32_t light = l->light +
                (int32_t)(((int64_t)dlight * prestep) >> 16);
            const int32_t light_low = (int32_t)min_light << 16;
            const int32_t light_high = ((int32_t)max_light << 16) | 0xffff;
            int32_t light_end;
            uint16_t *out = target->pixels +
                            (size_t)row * target->stride_pixels + x0;
            int32_t x;
            if (light < light_low) light = light_low;
            if (light > light_high) light = light_high;
            light_end = light + (int32_t)((int64_t)dlight * (x1 - x0 - 1));
            if (light_end < light_low || light_end > light_high) {
                if (light_end < light_low) light_end = light_low;
                if (light_end > light_high) light_end = light_high;
                dlight = x1 - x0 > 1
                             ? (light_end - light) / (x1 - x0 - 1)
                             : 0;
            }
            if (flat && (light >> 16) == (light_end >> 16)) {
                fill_rgb565(out, (uint32_t)(x1 - x0),
                            palette[((uint32_t)light >> 16) * 256u +
                                    color_index]);
            } else if ((light >> 16) == (light_end >> 16)) {
                const uint16_t *lit =
                    palette + ((uint32_t)light >> 16) * 256u;
                if (texture_power_of_two && !transparent && !blend_75) {
                    uint32_t remaining = (uint32_t)(x1 - x0);
                    while (remaining >= 4u) {
                        const uint32_t u_step = (uint32_t)du;
                        const uint32_t v_step = (uint32_t)dv;
                        const uint8_t t0 = texture->pixels[
                            painter_texture_index_pow2(
                                u, v, texture_mask_u, texture_mask_v,
                                texture_log2_width)];
                        const uint8_t t1 = texture->pixels[
                            painter_texture_index_pow2(
                                u + u_step, v + v_step, texture_mask_u,
                                texture_mask_v, texture_log2_width)];
                        const uint8_t t2 = texture->pixels[
                            painter_texture_index_pow2(
                                u + 2u * u_step, v + 2u * v_step,
                                texture_mask_u, texture_mask_v,
                                texture_log2_width)];
                        const uint8_t t3 = texture->pixels[
                            painter_texture_index_pow2(
                                u + 3u * u_step, v + 3u * v_step,
                                texture_mask_u, texture_mask_v,
                                texture_log2_width)];
                        out[0] = lit[t0];
                        out[1] = lit[t1];
                        out[2] = lit[t2];
                        out[3] = lit[t3];
                        out += 4;
                        u += 4u * u_step;
                        v += 4u * v_step;
                        remaining -= 4u;
                    }
                    while (remaining-- != 0) {
                        const uint8_t texel = texture->pixels[
                            painter_texture_index_pow2(
                                u, v, texture_mask_u, texture_mask_v,
                                texture_log2_width)];
                        *out++ = lit[texel];
                        u += (uint32_t)du;
                        v += (uint32_t)dv;
                    }
                } else {
                    for (x = x0; x < x1; ++x) {
                        uint8_t texel;
                        if (texture_power_of_two) {
                            texel = texture->pixels[
                                painter_texture_index_pow2(
                                    u, v, texture_mask_u, texture_mask_v,
                                    texture_log2_width)];
                        } else {
                            const int32_t tx = wrap_texture_coordinate(
                                (int32_t)(u >> 16), texture->width, 0);
                            const int32_t ty = wrap_texture_coordinate(
                                (int32_t)(v >> 16), texture->height, 0);
                            texel = texture->pixels[
                                (size_t)ty * texture->width + tx];
                        }
                        if (!transparent || texel != 0) {
                            const uint16_t source = lit[texel];
                            *out = blend_75
                                       ? blend_rgb565_75(*out, source)
                                       : source;
                        }
                        ++out;
                        u += (uint32_t)du;
                        v += (uint32_t)dv;
                    }
                }
            } else {
                for (x = x0; x < x1; ++x) {
                    const uint32_t level = (uint32_t)light >> 16;
                    uint8_t texel = color_index;
                    if (!flat) {
                        if (texture_power_of_two) {
                            texel = texture->pixels[
                                painter_texture_index_pow2(
                                    u, v, texture_mask_u, texture_mask_v,
                                    texture_log2_width)];
                        } else {
                            const int32_t tx = wrap_texture_coordinate(
                                (int32_t)(u >> 16), texture->width, 0);
                            const int32_t ty = wrap_texture_coordinate(
                                (int32_t)(v >> 16), texture->height, 0);
                            texel = texture->pixels[
                                (size_t)ty * texture->width + tx];
                        }
                    }
                    if (!transparent || texel != 0) {
                        const uint16_t source =
                            palette[(level << 8) | texel];
                        *out = blend_75 ? blend_rgb565_75(*out, source)
                                        : source;
                    }
                    ++out;
                    u += (uint32_t)du;
                    v += (uint32_t)dv;
                    light += dlight;
                }
            }
            covered += (uint32_t)(x1 - x0);
        }
        if (++row >= end) break;
        if (row == left.edge.end_row) {
            if (!painter_advance_chain(&left, row)) break;
        } else {
            painter_step_edge(&left.edge);
        }
        if (row == right.edge.end_row) {
            if (!painter_advance_chain(&right, row)) break;
        } else {
            painter_step_edge(&right.edge);
        }
    }
    return covered;
}

static uint32_t draw_quad(const uint8_t *record, uint8_t textured,
                          uint16_t abi_minor,
                          const pxa_raster_target_t *target,
                          const pxa_raster_resources_t *resources,
                          uint16_t row_begin, uint16_t row_end) {
    raster_vertex_t vertices[4];
    const pxa_raster_texture_t *texture = NULL;
    uint16_t color = 0;
    uint32_t offset = 8;
    uint8_t index;
    uint8_t mode = 0;
    if (textured) {
        const uint8_t flags = record[1];
        if ((flags & PXA_RASTER_QUAD_SOLID_COLOR) != 0)
            color = read_u16(record + 6);
        else
            texture = &resources->textures[record[4]];
        for (index = 0; index < 4; ++index)
            decode_vertex(record + offset + index * PXA_RASTER_VERTEX_BYTES,
                          &vertices[index]);
        if ((flags & PXA_RASTER_QUAD_PAINTER) != 0)
            return draw_painter_polygon(
                vertices, 4, flags, (uint8_t)read_u16(record + 6), texture,
                resources->palette, target, row_begin, row_end);
        if (abi_minor >= 1) {
            mode = RASTER_MODE_DEPTH;
            if ((flags & PXA_RASTER_QUAD_LIT_PALETTE) != 0)
                mode |= RASTER_MODE_LIT_PALETTE;
            if ((flags & PXA_RASTER_QUAD_TRANSPARENT_INDEX0) != 0)
                mode |= RASTER_MODE_TRANSPARENT_INDEX0;
            if ((flags & PXA_RASTER_QUAD_BLEND_75) != 0)
                mode |= RASTER_MODE_BLEND_75;
            if (texture != NULL &&
                (flags & PXA_RASTER_QUAD_AFFINE_UV) == 0)
                mode |= RASTER_MODE_PERSPECTIVE_UV;
        }
    } else {
        color = read_u16(record + 4);
        for (index = 0; index < 4; ++index) {
            vertices[index].x = read_i16(record + offset + index * 4u);
            vertices[index].y = read_i16(record + offset + index * 4u + 2u);
        }
    }
    return draw_triangle(&vertices[0], &vertices[1], &vertices[2], color,
                         texture, resources->palette, mode, target,
                         row_begin, row_end) +
           draw_triangle(&vertices[0], &vertices[2], &vertices[3], color,
                         texture, resources->palette, mode, target,
                         row_begin, row_end);
}

static uint32_t draw_sprite_instance(
    const uint8_t *instance, const pxa_raster_texture_t *texture,
    uint8_t flags, uint16_t solid_color,
    const pxa_raster_target_t *target,
    const pxa_raster_resources_t *resources, uint16_t row_begin,
    uint16_t row_end) {
    const int32_t x0 = read_i16(instance);
    const int32_t y0 = read_i16(instance + 2);
    const uint32_t width = read_u16(instance + 4);
    const uint32_t height = read_u16(instance + 6);
    const uint32_t source_x = read_u16(instance + 8);
    const uint32_t source_y = read_u16(instance + 10);
    const uint32_t source_width = read_u16(instance + 12);
    const uint32_t source_height = read_u16(instance + 14);
    uint32_t dx_begin = x0 < 0 ? (uint32_t)-x0 : 0;
    uint32_t dy_begin = y0 < 0 ? (uint32_t)-y0 : 0;
    uint32_t dx_end = width;
    uint32_t dy_end = height;
    uint32_t x_advance = source_width / width;
    uint32_t x_remainder_step = source_width % width;
    uint32_t y_advance = source_height / height;
    uint32_t y_remainder_step = source_height % height;
    uint32_t x_numerator = dx_begin * source_width;
    uint32_t tx_begin = source_x + x_numerator / width;
    uint32_t x_error_begin = x_numerator % width;
    uint32_t y_numerator = dy_begin * source_height;
    uint32_t ty = source_y + y_numerator / height;
    uint32_t y_error = y_numerator % height;
    uint32_t covered = 0;
    uint32_t dy;
    if (x0 >= target->width || y0 >= target->height || dx_begin >= width ||
        dy_begin >= height)
        return 0;
    if ((uint32_t)(target->width - x0) < dx_end)
        dx_end = (uint32_t)(target->width - x0);
    if ((uint32_t)(target->height - y0) < dy_end)
        dy_end = (uint32_t)(target->height - y0);
    {
        const int32_t clipped_begin = (int32_t)row_begin - y0;
        const int32_t clipped_end = (int32_t)row_end - y0;
        if (clipped_end <= 0) return 0;
        if (clipped_begin > 0 && (uint32_t)clipped_begin > dy_begin)
            dy_begin = (uint32_t)clipped_begin;
        if ((uint32_t)clipped_end < dy_end)
            dy_end = (uint32_t)clipped_end;
        if (dy_begin >= dy_end) return 0;
        y_numerator = dy_begin * source_height;
        ty = source_y + y_numerator / height;
        y_error = y_numerator % height;
    }
    for (dy = dy_begin; dy < dy_end; ++dy) {
        const int32_t y = y0 + (int32_t)dy;
        uint32_t tx = tx_begin;
        uint32_t x_error = x_error_begin;
        uint32_t dx;
        for (dx = dx_begin; dx < dx_end; ++dx) {
            const int32_t x = x0 + (int32_t)dx;
            const uint8_t texel =
                texture->pixels[(size_t)ty * texture->width + tx];
            uint16_t color;
            uint16_t *destination;
            if ((flags & PXA_RASTER_SPRITE_TRANSPARENT_INDEX0) == 0 ||
                texel != 0) {
                color = (flags & PXA_RASTER_SPRITE_SOLID_COLOR) != 0
                            ? solid_color
                            : resources->palette[texel];
                destination =
                    target->pixels + (size_t)y * target->stride_pixels + x;
                *destination = (flags & PXA_RASTER_SPRITE_ADDITIVE) != 0
                                   ? saturating_add_rgb565(*destination, color)
                                   : color;
                ++covered;
            }
            tx += x_advance;
            x_error += x_remainder_step;
            if (x_error >= width) {
                x_error -= width;
                ++tx;
            }
        }
        ty += y_advance;
        y_error += y_remainder_step;
        if (y_error >= height) {
            y_error -= height;
            ++ty;
        }
    }
    return covered;
}

static uint32_t draw_sprite(const uint8_t *record,
                            const pxa_raster_target_t *target,
                            const pxa_raster_resources_t *resources,
                            uint16_t row_begin, uint16_t row_end) {
    return draw_sprite_instance(record + 8, &resources->textures[record[4]],
                                record[1], read_u16(record + 6), target,
                                resources, row_begin, row_end);
}

static uint32_t draw_sprite_batch(
    const uint8_t *record, const pxa_raster_target_t *target,
    const pxa_raster_resources_t *resources, uint16_t row_begin,
    uint16_t row_end) {
    const pxa_raster_texture_t *texture = &resources->textures[record[4]];
    const uint16_t count = read_u16(record + 8);
    uint32_t covered = 0;
    uint16_t index;
    for (index = 0; index < count; ++index) {
        covered += draw_sprite_instance(
            record + PXA_RASTER_SPRITE_BATCH_HEADER_BYTES +
                (uint32_t)index * PXA_RASTER_SPRITE_INSTANCE_BYTES,
            texture, record[1], read_u16(record + 6), target, resources,
            row_begin, row_end);
    }
    return covered;
}

static uint32_t draw_triangle_batch(
    const uint8_t *record, const pxa_raster_target_t *target,
    const pxa_raster_resources_t *resources, uint16_t row_begin,
    uint16_t row_end) {
    const uint8_t solid = record[1] & PXA_RASTER_QUAD_SOLID_COLOR;
    const pxa_raster_texture_t *texture =
        solid ? NULL : &resources->textures[record[4]];
    const uint16_t color = solid ? read_u16(record + 6) : 0;
    const uint16_t count = read_u16(record + 8);
    uint32_t covered = 0;
    uint16_t triangle;
    for (triangle = 0; triangle < count; ++triangle) {
        raster_vertex_t vertices[3];
        uint8_t vertex;
        for (vertex = 0; vertex < 3; ++vertex) {
            decode_vertex(
                record + PXA_RASTER_TRIANGLE_BATCH_HEADER_BYTES +
                    ((uint32_t)triangle * 3u + vertex) *
                        PXA_RASTER_VERTEX_BYTES,
                &vertices[vertex]);
        }
        if ((record[1] & PXA_RASTER_QUAD_PAINTER) != 0) {
            covered += draw_painter_polygon(
                vertices, 3, record[1], (uint8_t)color, texture,
                resources->palette, target, row_begin, row_end);
        } else {
            uint8_t mode = RASTER_MODE_DEPTH;
            if ((record[1] & PXA_RASTER_QUAD_LIT_PALETTE) != 0)
                mode |= RASTER_MODE_LIT_PALETTE;
            if ((record[1] & PXA_RASTER_QUAD_TRANSPARENT_INDEX0) != 0)
                mode |= RASTER_MODE_TRANSPARENT_INDEX0;
            if ((record[1] & PXA_RASTER_QUAD_BLEND_75) != 0)
                mode |= RASTER_MODE_BLEND_75;
            if (texture != NULL &&
                (record[1] & PXA_RASTER_QUAD_AFFINE_UV) == 0)
                mode |= RASTER_MODE_PERSPECTIVE_UV;
            covered += draw_triangle(&vertices[0], &vertices[1], &vertices[2],
                                     color, texture, resources->palette, mode,
                                     target, row_begin, row_end);
        }
    }
    return covered;
}

void pxa_raster_execute_draw_list_rows(
    const uint8_t *bytes, const pxa_raster_draw_list_view_t *list,
    const pxa_raster_target_t *target,
    const pxa_raster_resources_t *resources, uint16_t row_begin,
    uint16_t row_end, pxa_raster_telemetry_t *telemetry) {
    uint32_t offset = PXA_RASTER_DRAW_HEADER_BYTES;
    uint32_t index;
    uint32_t covered = 0;
    if (bytes == NULL || list == NULL || target == NULL || resources == NULL ||
        row_begin >= row_end || row_end > target->height)
        return;
    for (index = 0; index < list->command_count; ++index) {
        const uint8_t *record = bytes + offset;
        const uint16_t size = read_u16(record + 2);
        if (record[0] == PXA_RASTER_RECORD_CLEAR_RGB565) {
            const uint16_t color = read_u16(record + 4);
            const uint32_t pixel_count =
                (uint32_t)target->width * (row_end - row_begin);
            uint16_t y;
            if (target->stride_pixels == target->width) {
                fill_rgb565(target->pixels +
                                (size_t)row_begin * target->stride_pixels,
                            pixel_count, color);
            } else {
                for (y = row_begin; y < row_end; ++y) {
                    uint16_t *row = target->pixels +
                                    (size_t)y * target->stride_pixels;
                    fill_rgb565(row, target->width, color);
                }
            }
            if (target->depth_pixels != NULL) {
                if (target->depth_stride_pixels == target->width) {
                    memset(target->depth_pixels +
                               (size_t)row_begin *
                                   target->depth_stride_pixels,
                           0,
                           (size_t)pixel_count *
                               sizeof(*target->depth_pixels));
                } else {
                    for (y = row_begin; y < row_end; ++y) {
                        uint16_t *depth = target->depth_pixels +
                            (size_t)y * target->depth_stride_pixels;
                        memset(depth, 0,
                               (size_t)target->width * sizeof(*depth));
                    }
                }
            }
            covered += pixel_count;
            if (telemetry != NULL) ++telemetry->clear_commands;
        } else if (record[0] == PXA_RASTER_RECORD_FLAT_QUAD) {
            covered += draw_quad(record, 0, list->abi_minor, target, resources,
                                 row_begin, row_end);
            if (telemetry != NULL) ++telemetry->flat_quad_commands;
        } else if (record[0] == PXA_RASTER_RECORD_TEXTURED_QUAD) {
            covered += draw_quad(record, 1, list->abi_minor, target, resources,
                                 row_begin, row_end);
            if (telemetry != NULL) ++telemetry->textured_quad_commands;
        } else if (record[0] == PXA_RASTER_RECORD_SPRITE) {
            covered += draw_sprite(record, target, resources, row_begin,
                                   row_end);
            if (telemetry != NULL) ++telemetry->sprite_commands;
        } else if (record[0] == PXA_RASTER_RECORD_SPRITE_BATCH) {
            const uint16_t count = read_u16(record + 8);
            covered += draw_sprite_batch(record, target, resources, row_begin,
                                         row_end);
            if (telemetry != NULL) telemetry->sprite_commands += count;
        } else if (record[0] == PXA_RASTER_RECORD_TRIANGLE_BATCH) {
            const uint16_t count = read_u16(record + 8);
            covered += draw_triangle_batch(record, target, resources,
                                           row_begin, row_end);
            if (telemetry != NULL) telemetry->textured_quad_commands += count;
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

void pxa_raster_execute_draw_list(const uint8_t *bytes,
                                  const pxa_raster_draw_list_view_t *list,
                                  const pxa_raster_target_t *target,
                                  const pxa_raster_resources_t *resources,
                                  pxa_raster_telemetry_t *telemetry) {
    if (target == NULL) return;
    pxa_raster_execute_draw_list_rows(bytes, list, target, resources, 0,
                                      target->height, telemetry);
}

#ifndef PXA_GUEST_GAME_RENDER_TILES_H
#define PXA_GUEST_GAME_RENDER_TILES_H
#include "pxa_game_render_compute.h"

#define PXA_GAME_RENDER_IO_EXECUTE_PRIORITY_TILES 0x10au
#define PXA_GAME_RENDER_FEATURE_PRIORITY_TILE_BATCHES UINT64_C(64)

typedef struct {
    uint32_t color_offset, priority_offset, pixel_count, pitch;
} pxa_game_render_tile_target_t;

/* No allocation. Commands grow from the front; immutable tile/palette
 * snapshots grow from the back. Reset only after synchronous execute returns. */
typedef struct {
    uint8_t *bytes;
    uint32_t capacity, command_end, resource_begin, count;
    int32_t status;
    pxa_game_render_tile_target_t target;
} pxa_game_render_tile_batch_t;

static inline void pxa_game_render_tile_batch_begin(pxa_game_render_tile_batch_t *b,
    uint8_t *storage, uint32_t capacity, pxa_game_render_tile_target_t target) {
    if (!b) return;
    b->bytes = storage; b->capacity = capacity; b->target = target;
    b->command_end = PXA_GAME_RENDER_TILES_HEADER_BYTES;
    b->resource_begin = capacity; b->count = 0;
    b->status = !storage || capacity < PXA_GAME_RENDER_TILES_HEADER_BYTES ||
        capacity > PXA_GAME_RENDER_TILES_MAX_PACKET_BYTES || !target.pixel_count ||
        !target.pitch || target.pitch > target.pixel_count ? PXA_STATUS_INVALID_ARGUMENT : 0;
}
static inline uint32_t pxa_game_render_tile_remaining(const pxa_game_render_tile_batch_t *b) {
    return !b || b->status ? 0 : b->resource_begin - b->command_end;
}
/* Initialize every reserved byte before execute. Offsets are packet-relative. */
static inline uint32_t pxa_game_render_tile_reserve_source(pxa_game_render_tile_batch_t *b,
    uint32_t bytes) {
    if (!b || b->status) return 0;
    if (!bytes) { b->status = PXA_STATUS_INVALID_ARGUMENT; return 0; }
    if (bytes > b->resource_begin) { b->status = PXA_STATUS_LIMIT_EXCEEDED; return 0; }
    uint32_t at = b->resource_begin - bytes;
    uint32_t padding = (uint32_t)((uintptr_t)(b->bytes + at) & 3u);
    if (at < padding || at - padding < b->command_end) {
        b->status = PXA_STATUS_LIMIT_EXCEEDED; return 0;
    }
    b->resource_begin = at - padding;
    return b->resource_begin;
}
static inline uint32_t pxa_game_render_tile_copy_source(pxa_game_render_tile_batch_t *b,
    const void *source, uint32_t bytes) {
    if (!b || b->status) return 0;
    if (!source) { b->status = PXA_STATUS_INVALID_ARGUMENT; return 0; }
    uint32_t at = pxa_game_render_tile_reserve_source(b, bytes);
    if (at) memmove(b->bytes + at, source, bytes);
    return at;
}
static inline int pxa_game_render_tile_append(pxa_game_render_tile_batch_t *b,
    const pxa_game_render_tile_wire_t *tile) {
    if (!b || b->status) return 0;
    if (!tile || !tile->width || !tile->height || tile->source_x + tile->width > 8 ||
        tile->source_y + tile->height > 8 || tile->reserved0 || tile->reserved1 ||
        (tile->flags & ~3u) ||
        (tile->palette_entries != 4 && tile->palette_entries != 16 && tile->palette_entries != 256) ||
        tile->tile_offset < b->resource_begin || tile->tile_offset > b->capacity ||
        b->capacity - tile->tile_offset < 64 ||
        tile->palette_offset < b->resource_begin || tile->palette_offset > b->capacity ||
        b->capacity - tile->palette_offset < (uint32_t)tile->palette_entries * 2u) {
        b->status = PXA_STATUS_INVALID_ARGUMENT; return 0;
    }
    if (b->count == PXA_GAME_RENDER_TILES_MAX_TILES ||
        pxa_game_render_tile_remaining(b) < PXA_GAME_RENDER_TILES_COMMAND_BYTES) {
        b->status = PXA_STATUS_LIMIT_EXCEEDED; return 0;
    }
    pxa_game_render_tile_encode(b->bytes + b->command_end,
        PXA_GAME_RENDER_TILES_COMMAND_BYTES, tile);
    b->command_end += PXA_GAME_RENDER_TILES_COMMAND_BYTES; ++b->count;
    return 1;
}
static inline int32_t pxa_game_render_execute_priority_tiles(uint64_t context,
    pxa_game_render_tile_batch_t *b) {
    if (!context || !b) return PXA_STATUS_INVALID_ARGUMENT;
    if (b->status) return b->status;
    if (!b->count) return 0;
    pxa_game_render_tiles_header_wire_t h = {PXA_GAME_RENDER_TILES_MAGIC, 1, 0,
        b->count, b->command_end, b->target.color_offset, b->target.priority_offset,
        b->target.pixel_count, b->target.pitch};
    pxa_game_render_tiles_header_encode(b->bytes, PXA_GAME_RENDER_TILES_HEADER_BYTES, &h);
    int32_t result = pxa_io(context, PXA_GAME_RENDER_IO_EXECUTE_PRIORITY_TILES, b->bytes, b->capacity);
    return result < 0 ? result : result == (int32_t)b->capacity ? result : PXA_STATUS_PROTOCOL_ERROR;
}
#endif

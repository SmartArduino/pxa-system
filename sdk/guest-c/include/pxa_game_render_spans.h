#ifndef PXA_GUEST_GAME_RENDER_SPANS_H
#define PXA_GUEST_GAME_RENDER_SPANS_H
#include "pxa_game_render.h"
#include <string.h>
#include "pxa_game_render_spans_wire_generated.h"

#define PXA_GAME_RENDER_CREATE_RASTER_CONTEXT 3u
#define PXA_GAME_RENDER_IO_EXECUTE_SPANS 0x104u
#define PXA_GAME_RENDER_IO_SPAN_TELEMETRY 0x105u
#define PXA_GAME_RENDER_IO_EXECUTE_PROJECTED_SPANS 0x106u
#define PXA_GAME_RENDER_FEATURE_PROJECTED_INDEXED_SPANS UINT64_C(4)
#define PXA_GAME_RENDER_FEATURE_BORROWED_INDEXED_SPANS UINT64_C(2)

typedef struct {
    int32_t status;
    uint64_t handle;
    uint32_t max_spans;
} pxa_game_render_raster_result_t;

typedef struct {
    uint64_t batches, rejected, spans, pixels, validation_us, execute_us;
    uint32_t last_validation_us, last_execute_us, max_spans;
} pxa_game_render_span_telemetry_t;

static inline int32_t pxa_game_render_query_span_telemetry(uint64_t context,
    pxa_game_render_span_telemetry_t *out) {
    uint8_t bytes[64] = {0};
    if (!context || !out) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_zero(out, sizeof(*out));
    int32_t result = pxa_io(context, PXA_GAME_RENDER_IO_SPAN_TELEMETRY,
        bytes, sizeof(bytes));
    if (result < 0) return result;
    if (result != sizeof(bytes) || pxa_load_u32(bytes + 60))
        return PXA_STATUS_PROTOCOL_ERROR;
    out->batches = pxa_load_u64(bytes); out->rejected = pxa_load_u64(bytes + 8);
    out->spans = pxa_load_u64(bytes + 16); out->pixels = pxa_load_u64(bytes + 24);
    out->validation_us = pxa_load_u64(bytes + 32);
    out->execute_us = pxa_load_u64(bytes + 40);
    out->last_validation_us = pxa_load_u32(bytes + 48);
    out->last_execute_us = pxa_load_u32(bytes + 52);
    out->max_spans = pxa_load_u32(bytes + 56);
    return PXA_STATUS_OK;
}

static inline int32_t pxa_game_render_create_raster(uint64_t token,
    uint32_t max_spans) {
    uint8_t p[8];
    if (!token || !max_spans) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u32(p, max_spans); pxa_store_u32(p + 4, 0);
    return pxa_control_call(PXA_GAME_RENDER_SERVICE,
        PXA_GAME_RENDER_CREATE_RASTER_CONTEXT, token, p, sizeof(p));
}

static inline int pxa_game_render_parse_raster(const pxa_event_t *event,
    uint64_t token, pxa_game_render_raster_result_t *out) {
    if (!out) return 0;
    pxa_zero(out, sizeof(*out));
    if (!event || event->service != PXA_GAME_RENDER_SERVICE ||
        event->opcode != PXA_GAME_RENDER_CREATE_RASTER_CONTEXT ||
        !token || event->token != token || !event->payload || event->payload_size < 4)
        return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status) return event->payload_size == 4;
    if (event->payload_size != 20 || pxa_load_u32(event->payload + 16)) return 0;
    out->handle = pxa_load_u64(event->payload + 4);
    out->max_spans = pxa_load_u32(event->payload + 12);
    return out->handle && out->max_spans;
}

typedef struct {
    uint8_t *bytes;
    size_t capacity;
    uint32_t count;
    int32_t status;
} pxa_game_render_span_batch_t;

/* Header offsets are Guest linear-memory addresses, never native pointers.
 * Borrowed sources/target are immutable/exclusive until execute returns. */
static inline void pxa_game_render_span_batch_begin(
    pxa_game_render_span_batch_t *b, uint8_t *bytes, size_t capacity,
    const pxa_game_render_spans_header_wire_t *header) {
    pxa_game_render_spans_header_wire_t h;
    if (!b) return;
    b->bytes = bytes; b->capacity = capacity; b->count = 0;
    b->status = PXA_STATUS_INVALID_ARGUMENT;
    if (!header || !bytes || capacity < PXA_GAME_RENDER_SPANS_HEADER_BYTES) return;
    h = *header; h.magic = PXA_GAME_RENDER_SPANS_MAGIC;
    h.major = 1; h.minor = 0; h.count = 0; h.reserved = 0;
    pxa_game_render_spans_header_encode(bytes, capacity, &h);
    b->status = PXA_STATUS_OK;
}

static inline int pxa_game_render_span_append(pxa_game_render_span_batch_t *b,
    const pxa_game_render_span_wire_t *span) {
    size_t pos;
    if (!b || b->status) return 0;
    if (!span || !span->count) { b->status = PXA_STATUS_INVALID_ARGUMENT; return 0; }
    pos = PXA_GAME_RENDER_SPANS_HEADER_BYTES + (size_t)b->count * PXA_GAME_RENDER_SPAN_BYTES;
    if (pos > b->capacity || b->capacity - pos < PXA_GAME_RENDER_SPAN_BYTES) {
        b->status = PXA_STATUS_LIMIT_EXCEEDED; return 0;
    }
    pxa_game_render_span_encode(b->bytes + pos, b->capacity - pos, span);
    ++b->count;
    return 1;
}

static inline int32_t pxa_game_render_execute_spans(uint64_t context,
    pxa_game_render_span_batch_t *b) {
    if (!context || !b) return PXA_STATUS_INVALID_ARGUMENT;
    if (b->status) return b->status;
    if (!b->count) return 0;
    pxa_store_u32(b->bytes + 8, b->count);
    return pxa_io(context, PXA_GAME_RENDER_IO_EXECUTE_SPANS, b->bytes,
        PXA_GAME_RENDER_SPANS_HEADER_BYTES + b->count * PXA_GAME_RENDER_SPAN_BYTES);
}

/* Projected batches use the same caller-owned storage and sticky errors.
 * Keep the distinct builder type to prevent selecting the wrong wire format. */
typedef struct { pxa_game_render_span_batch_t batch; } pxa_game_render_projected_batch_t;

static inline void pxa_game_render_projected_batch_begin(
    pxa_game_render_projected_batch_t *projected, uint8_t *bytes, size_t capacity,
    const pxa_game_render_spans_header_wire_t *header,
    const pxa_game_render_projection_wire_t *projection) {
    if (!projected) return;
    pxa_game_render_span_batch_t *b = &projected->batch;
    pxa_game_render_span_batch_begin(b, bytes, capacity, header);
    if (b->status) return;
    if (!projection || capacity < PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES) {
        b->status = PXA_STATUS_INVALID_ARGUMENT; return;
    }
    pxa_store_u32(bytes, PXA_GAME_RENDER_PROJECTED_MAGIC);
    pxa_game_render_projection_encode(bytes + PXA_GAME_RENDER_SPANS_HEADER_BYTES,
        PXA_GAME_RENDER_PROJECTION_BYTES, projection);
}

static inline int pxa_game_render_projected_append(pxa_game_render_projected_batch_t *projected,
    const pxa_game_render_row_wire_t *row) {
    if (!projected) return 0;
    pxa_game_render_span_batch_t *b = &projected->batch;
    if (b->status) return 0;
    if (!row || !row->count || row->reserved) { b->status = PXA_STATUS_INVALID_ARGUMENT; return 0; }
    size_t pos = PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES + (size_t)b->count * PXA_GAME_RENDER_ROW_BYTES;
    if (pos > b->capacity || b->capacity - pos < PXA_GAME_RENDER_ROW_BYTES) {
        b->status = PXA_STATUS_LIMIT_EXCEEDED; return 0;
    }
    pxa_game_render_row_encode(b->bytes + pos, PXA_GAME_RENDER_ROW_BYTES, row);
    ++b->count;
    return 1;
}

static inline int32_t pxa_game_render_execute_projected_spans(uint64_t context,
    pxa_game_render_projected_batch_t *projected) {
    if (!context || !projected) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_game_render_span_batch_t *b = &projected->batch;
    if (b->status) return b->status;
    if (!b->count) return 0;
    pxa_store_u32(b->bytes + 8, b->count);
    uint32_t size = PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES + b->count * PXA_GAME_RENDER_ROW_BYTES;
    int32_t result = pxa_io(context, PXA_GAME_RENDER_IO_EXECUTE_PROJECTED_SPANS, b->bytes, size);
    return result < 0 ? result : result == (int32_t)size ? result : PXA_STATUS_PROTOCOL_ERROR;
}

#define PXA_GAME_RENDER_IO_EXECUTE_FRAME_SPANS 0x107u
#define PXA_GAME_RENDER_FEATURE_INDEXED_FRAME_BATCHES UINT64_C(8)
typedef struct {
    uint8_t *bytes;
    uint32_t capacity, command_end, resource_begin, count;
    int32_t status;
} pxa_game_render_frame_batch_t;

static inline void pxa_game_render_frame_begin(pxa_game_render_frame_batch_t *b,
    uint8_t *storage, uint32_t capacity) {
    if (!b) return;
    b->bytes = storage; b->capacity = capacity;
    b->command_end = PXA_GAME_RENDER_FRAME_HEADER_BYTES;
    b->resource_begin = capacity; b->count = 0;
    b->status = !storage || capacity < PXA_GAME_RENDER_FRAME_HEADER_BYTES ||
        capacity > PXA_GAME_RENDER_FRAME_MAX_PACKET_BYTES ? PXA_STATUS_INVALID_ARGUMENT : 0;
}

static inline uint32_t pxa_game_render_frame_remaining(const pxa_game_render_frame_batch_t *b) {
    return !b || b->status ? 0 : b->resource_begin - b->command_end;
}

/* Reserve caller-initialized source storage. Returns a packet-relative offset,
 * zero on sticky failure. Initialize all reserved bytes before execution. */
static inline uint32_t pxa_game_render_frame_reserve_source(pxa_game_render_frame_batch_t *b,
    uint32_t bytes) {
    if (!b || b->status) return 0;
    if (!bytes) { b->status = PXA_STATUS_INVALID_ARGUMENT; return 0; }
    if (bytes > b->resource_begin) { b->status = PXA_STATUS_LIMIT_EXCEEDED; return 0; }
    uint32_t position = b->resource_begin - bytes;
    uint32_t padding = (uint32_t)((uintptr_t)(b->bytes + position) & 3u);
    if (position < padding || position - padding < b->command_end) {
        b->status = PXA_STATUS_LIMIT_EXCEEDED; return 0;
    }
    position -= padding;
    b->resource_begin = position;
    return position;
}

/* An acquired display buffer may be used as storage, then reused after execute
 * returns. Overlapping source and destination have memmove semantics. */
static inline uint32_t pxa_game_render_frame_copy_source(pxa_game_render_frame_batch_t *b,
    const void *source, uint32_t bytes) {
    if (!b || b->status) return 0;
    if (!source) { b->status = PXA_STATUS_INVALID_ARGUMENT; return 0; }
    uint32_t position = pxa_game_render_frame_reserve_source(b, bytes);
    if (position) memmove(b->bytes + position, source, bytes);
    return position;
}

static inline int pxa_game_render_frame_append(pxa_game_render_frame_batch_t *b,
    const pxa_game_render_spans_header_wire_t *material,
    const pxa_game_render_projection_wire_t *projection,
    const pxa_game_render_row_wire_t *rows, uint32_t count) {
    if (!b || b->status) return 0;
    if (!material || !projection || !rows || !count || count > 4096) {
        b->status = PXA_STATUS_INVALID_ARGUMENT; return 0;
    }
    uint32_t size = PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES + count * PXA_GAME_RENDER_ROW_BYTES;
    if (b->count == PXA_GAME_RENDER_FRAME_MAX_MATERIALS ||
        size > pxa_game_render_frame_remaining(b)) {
        b->status = PXA_STATUS_LIMIT_EXCEEDED; return 0;
    }
    for (uint32_t i = 0; i < count; ++i) if (!rows[i].count || rows[i].reserved) {
        b->status = PXA_STATUS_INVALID_ARGUMENT; return 0;
    }
    pxa_game_render_spans_header_wire_t h = *material;
    h.magic = PXA_GAME_RENDER_FRAME_MATERIAL_MAGIC;
    h.major = 1; h.minor = 0; h.count = count; h.reserved = 0;
    uint8_t *record = b->bytes + b->command_end;
    pxa_game_render_spans_header_encode(record, PXA_GAME_RENDER_SPANS_HEADER_BYTES, &h);
    pxa_game_render_projection_encode(record + PXA_GAME_RENDER_SPANS_HEADER_BYTES,
        PXA_GAME_RENDER_PROJECTION_BYTES, projection);
    for (uint32_t i = 0; i < count; ++i)
        pxa_game_render_row_encode(record + PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES +
            i * PXA_GAME_RENDER_ROW_BYTES, PXA_GAME_RENDER_ROW_BYTES, rows + i);
    b->command_end += size; ++b->count;
    return 1;
}

static inline int32_t pxa_game_render_execute_frame_spans(uint64_t context,
    pxa_game_render_frame_batch_t *b) {
    if (!context || !b) return PXA_STATUS_INVALID_ARGUMENT;
    if (b->status) return b->status;
    if (!b->count) return 0;
    pxa_game_render_frame_header_wire_t h = {
        PXA_GAME_RENDER_FRAME_MAGIC, 1, 0, b->count, b->command_end};
    pxa_game_render_frame_header_encode(b->bytes, PXA_GAME_RENDER_FRAME_HEADER_BYTES, &h);
    int32_t result = pxa_io(context, PXA_GAME_RENDER_IO_EXECUTE_FRAME_SPANS, b->bytes, b->capacity);
    return result < 0 ? result : result == (int32_t)b->capacity ? result : PXA_STATUS_PROTOCOL_ERROR;
}

#define PXA_GAME_RENDER_IO_BLIT_INDEXED_FRAME 0x108u
#define PXA_GAME_RENDER_FEATURE_BORROWED_INDEXED_BLIT UINT64_C(16)
static inline int32_t pxa_game_render_blit_indexed_frame(uint64_t context,
    const pxa_game_render_blit_wire_t *frame) {
    uint8_t bytes[PXA_GAME_RENDER_BLIT_BYTES];
    if (!context || !frame) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_game_render_blit_encode(bytes, sizeof(bytes), frame);
    int32_t result = pxa_io(context, PXA_GAME_RENDER_IO_BLIT_INDEXED_FRAME, bytes, sizeof(bytes));
    return result < 0 ? result : result == sizeof(bytes) ? result : PXA_STATUS_PROTOCOL_ERROR;
}
#endif

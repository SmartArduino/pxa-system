#ifndef PXA_GUEST_GAME_RENDER_SPANS_H
#define PXA_GUEST_GAME_RENDER_SPANS_H
#include "pxa_game_render.h"
#include "pxa_game_render_spans_wire_generated.h"

#define PXA_GAME_RENDER_CREATE_RASTER_CONTEXT 3u
#define PXA_GAME_RENDER_IO_EXECUTE_SPANS 0x104u
#define PXA_GAME_RENDER_IO_SPAN_TELEMETRY 0x105u
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
#endif

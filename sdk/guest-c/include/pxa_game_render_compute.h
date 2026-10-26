#ifndef PXA_GUEST_GAME_RENDER_COMPUTE_H
#define PXA_GUEST_GAME_RENDER_COMPUTE_H
#include "pxa_game_render.h"
#include <string.h>
#include "pxa_game_render_compute_wire_generated.h"

#define PXA_GAME_RENDER_CREATE_RASTER_CONTEXT 3u
#define PXA_GAME_RENDER_IO_COMPUTE_TELEMETRY 0x10cu

typedef struct {
    int32_t status;
    uint64_t handle;
    uint32_t max_commands;
} pxa_game_render_raster_result_t;

typedef struct {
    uint64_t batches, rejected, commands, pixels, validation_us, execute_us;
    uint32_t last_validation_us, last_execute_us, max_commands;
} pxa_game_render_compute_telemetry_t;

static inline int32_t pxa_game_render_query_compute_telemetry(uint64_t context,
    pxa_game_render_compute_telemetry_t *out) {
    uint8_t bytes[64] = {0};
    if (!context || !out) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_zero(out, sizeof(*out));
    int32_t result = pxa_io(context, PXA_GAME_RENDER_IO_COMPUTE_TELEMETRY,
        bytes, sizeof(bytes));
    if (result < 0) return result;
    if (result != sizeof(bytes) || pxa_load_u32(bytes + 60))
        return PXA_STATUS_PROTOCOL_ERROR;
    out->batches = pxa_load_u64(bytes); out->rejected = pxa_load_u64(bytes + 8);
    out->commands = pxa_load_u64(bytes + 16); out->pixels = pxa_load_u64(bytes + 24);
    out->validation_us = pxa_load_u64(bytes + 32);
    out->execute_us = pxa_load_u64(bytes + 40);
    out->last_validation_us = pxa_load_u32(bytes + 48);
    out->last_execute_us = pxa_load_u32(bytes + 52);
    out->max_commands = pxa_load_u32(bytes + 56);
    return PXA_STATUS_OK;
}

static inline int32_t pxa_game_render_create_raster(uint64_t token,
    uint32_t max_commands) {
    uint8_t p[8];
    if (!token || !max_commands) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u32(p, max_commands); pxa_store_u32(p + 4, 0);
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
    out->max_commands = pxa_load_u32(event->payload + 12);
    return out->handle && out->max_commands;
}

#define PXA_GAME_RENDER_IO_BLIT_INDEXED_FRAME 0x108u
#define PXA_GAME_RENDER_FEATURE_BORROWED_INDEXED_BLIT UINT64_C(16)
#define PXA_GAME_RENDER_IO_BLIT_INDEXED_RECT 0x109u
#define PXA_GAME_RENDER_FEATURE_EXACT_INDEXED_BLIT UINT64_C(32)
/* Exact nearest-neighbour rectangle: sx=x*source_width/target_width,
 * sy=y*source_height/target_height. Offset the target into a larger Surface
 * and retain its pitch for letterboxing. Borrows end before this returns. */
static inline int32_t pxa_game_render_blit_indexed_rect(uint64_t context,
    const pxa_game_render_blit_wire_t *frame) {
    uint8_t bytes[PXA_GAME_RENDER_BLIT_BYTES];
    if (!context || !frame) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_game_render_blit_encode(bytes, sizeof(bytes), frame);
    int32_t result = pxa_io(context, PXA_GAME_RENDER_IO_BLIT_INDEXED_RECT, bytes, sizeof(bytes));
    return result < 0 ? result : result == sizeof(bytes) ? result : PXA_STATUS_PROTOCOL_ERROR;
}
static inline int32_t pxa_game_render_blit_indexed_frame(uint64_t context,
    const pxa_game_render_blit_wire_t *frame) {
    uint8_t bytes[PXA_GAME_RENDER_BLIT_BYTES];
    if (!context || !frame) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_game_render_blit_encode(bytes, sizeof(bytes), frame);
    int32_t result = pxa_io(context, PXA_GAME_RENDER_IO_BLIT_INDEXED_FRAME, bytes, sizeof(bytes));
    return result < 0 ? result : result == sizeof(bytes) ? result : PXA_STATUS_PROTOCOL_ERROR;
}
#endif

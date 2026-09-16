#ifndef PXA_GAME_RENDER_GUEST_H
#define PXA_GAME_RENDER_GUEST_H

#include "pxa.h"

#define PXA_GAME_RENDER_CREATE_CONTEXT 1u
#define PXA_GAME_RENDER_FLAG_PREFER_DIRECT_SCANOUT 1u
#define PXA_GAME_RENDER_IO_UPLOAD UINT32_C(0x100)
#define PXA_GAME_RENDER_IO_SUBMIT UINT32_C(0x101)
#define PXA_GAME_RENDER_IO_TELEMETRY UINT32_C(0x102)
#define PXA_GAME_RENDER_TELEMETRY_BYTES UINT32_C(88)

typedef struct {
    int32_t status;
    uint32_t context_handle;
    uint32_t capabilities;
    uint32_t max_draw_bytes;
    uint16_t max_texture_dimension;
    uint8_t max_textures;
} pxa_game_render_create_result_t;

static inline void pxa_game_render_store_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static inline void pxa_game_render_store_u32(uint8_t *out, uint32_t value) {
    pxa_game_render_store_u16(out, (uint16_t)value);
    pxa_game_render_store_u16(out + 2, (uint16_t)(value >> 16));
}

static inline void pxa_game_render_store_u64(uint8_t *out, uint64_t value) {
    pxa_game_render_store_u32(out, (uint32_t)value);
    pxa_game_render_store_u32(out + 4, (uint32_t)(value >> 32));
}

static inline int pxa_game_render_create(
    uint32_t request_id, uint16_t width, uint16_t height,
    uint8_t buffer_count, uint8_t prefer_direct, uint8_t *packet,
    size_t packet_capacity) {
    uint8_t payload[8] = {0};
    pxa_writer_t writer;
    if (request_id == 0 || width == 0 || height == 0 || buffer_count < 2 ||
        packet == NULL)
        return 0;
    pxa_game_render_store_u16(payload, width);
    pxa_game_render_store_u16(payload + 2, height);
    payload[4] = buffer_count;
    payload[5] = prefer_direct
                     ? PXA_GAME_RENDER_FLAG_PREFER_DIRECT_SCANOUT
                     : 0;
    pxa_writer_init(&writer, packet, packet_capacity);
    return pxa_message(&writer, PXA_SERVICE_GAME_RENDER,
                       PXA_GAME_RENDER_CREATE_CONTEXT, request_id, payload,
                       sizeof(payload)) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int pxa_game_render_parse_create(
    const pxa_event_t *event, pxa_game_render_create_result_t *output) {
    if (event == NULL || output == NULL ||
        event->service != PXA_SERVICE_GAME_RENDER ||
        event->opcode != PXA_GAME_RENDER_CREATE_CONTEXT)
        return 0;
    if (event->payload == NULL || event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    output->context_handle = 0;
    output->capabilities = 0;
    output->max_draw_bytes = 0;
    output->max_texture_dimension = 0;
    output->max_textures = 0;
    if (output->status != PXA_STATUS_OK)
        return event->payload_length == 4;
    if (event->payload_length != 20) return 0;
    output->context_handle = pxa_read_u32(event->payload + 4);
    output->capabilities = pxa_read_u32(event->payload + 8);
    output->max_draw_bytes = pxa_read_u32(event->payload + 12);
    output->max_texture_dimension = pxa_read_u16(event->payload + 16);
    output->max_textures = event->payload[18];
    return output->context_handle != 0 && event->payload[19] == 0;
}

#endif

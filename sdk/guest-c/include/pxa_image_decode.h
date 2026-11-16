#ifndef PXA_GUEST_IMAGE_DECODE_H
#define PXA_GUEST_IMAGE_DECODE_H

#include "pxa_assets.h"

#define PXA_ASSETS_DECODE_IMAGE 6u
#define PXA_ASSETS_NEXT_IMAGE_FRAME 7u
#define PXA_ASSETS_IMAGE_INPUT_LIMITS 8u
#define PXA_IMAGE_ENCODED_MAX_BYTES 16777216u
#define PXA_IMAGE_FORMAT_PNG 1u
#define PXA_IMAGE_FORMAT_JPEG 2u
#define PXA_IMAGE_FORMAT_GIF 4u
#define PXA_IMAGE_WITH_FRAMES 256u
#define PXA_IMAGE_FRAME_LAST 1u
#define PXA_IMAGE_SOURCE_STREAM 2u
#define PXA_IMAGE_SOURCE_FILE 3u

typedef struct {
    uint16_t max_width, max_height;
    uint32_t max_pixels, max_encoded_bytes;
    uint16_t formats; /* zero accepts any supported codec. */
} pxa_image_decode_options_t;

typedef struct {
    uint32_t max_pixels, max_encoded_bytes; /* zero uses ordinary ceilings. */
} pxa_image_animation_limits_t;
typedef struct {
    int32_t status;
    uint32_t max_encoded_bytes, available_encoded_bytes, max_animation_encoded_bytes;
} pxa_image_input_limits_t;

/* Optional Assets 2.7 query. A successful zero available value is pressure;
 * unsupported on an old Host must retain the caller's legacy <=512KiB limit. */
static inline int32_t pxa_image_input_limits(uint64_t token, uint32_t max_pixels) {
    uint8_t payload[4];
    if (!token || !max_pixels || max_pixels > 1048576u) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u32(payload,max_pixels);
    return pxa_control_call(PXA_ASSETS_SERVICE,PXA_ASSETS_IMAGE_INPUT_LIMITS,token,payload,4);
}
static inline int pxa_image_input_limits_parse(const pxa_event_t *event, uint64_t token,
    pxa_image_input_limits_t *out) {
    if (!out) return 0;
    pxa_zero(out,sizeof(*out));
    if (!event || !token || event->token != token || event->service != PXA_ASSETS_SERVICE ||
        event->opcode != PXA_ASSETS_IMAGE_INPUT_LIMITS || !event->payload || event->payload_size < 4)
        return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status) return event->payload_size == 4;
    if (event->payload_size != 16) return 0;
    out->max_encoded_bytes = pxa_load_u32(event->payload+4);
    out->available_encoded_bytes = pxa_load_u32(event->payload+8);
    out->max_animation_encoded_bytes = pxa_load_u32(event->payload+12);
    return out->max_encoded_bytes && out->max_encoded_bytes <= PXA_IMAGE_ENCODED_MAX_BYTES &&
        out->available_encoded_bytes <= out->max_encoded_bytes &&
        out->max_animation_encoded_bytes && out->max_animation_encoded_bytes <= 524288u &&
        out->max_animation_encoded_bytes <= out->max_encoded_bytes;
}

typedef struct {
    int32_t status;
    uint64_t handle;
    pxa_asset_descriptor_t info;
    uint16_t source_width, source_height;
    uint32_t source_bytes;
    uint32_t delay_ms, iteration, flags;
} pxa_image_decode_result_t;

static inline pxa_image_decode_options_t pxa_image_decode_defaults(void) {
    pxa_image_decode_options_t options = {320, 320, 32768, 131072, 0};
    return options;
}

static inline int pxa_image_decode_options_valid(pxa_image_decode_options_t options) {
    return options.max_width && options.max_width <= 1024 && options.max_height &&
        options.max_height <= 1024 && options.max_pixels && options.max_pixels <= 65536 &&
        options.max_encoded_bytes && options.max_encoded_bytes <= PXA_IMAGE_ENCODED_MAX_BYTES && !(options.formats & ~7u);
}

/* Borrows source exclusively until completion/cancel. The Host reads directly
 * from FILE/STREAM; the caller retains ownership of source and returned images.
 * Pass animation != NULL for frame support, even for default (zero) ceilings. */
static inline int pxa_image_decode_build(uint8_t *packet, size_t capacity,
    uint64_t token, uint64_t source, uint16_t source_type,
    pxa_image_decode_options_t options, const pxa_image_animation_limits_t *animation,
    uint32_t *written) {
    uint8_t *p;
    size_t size = animation ? 32u : 24u;
    if (written) *written = 0;
    if (!packet || !written || !token || !source || capacity < PXA_HEADER_BYTES + size ||
        (source_type != PXA_IMAGE_SOURCE_STREAM && source_type != PXA_IMAGE_SOURCE_FILE) ||
        !pxa_image_decode_options_valid(options) ||
        (animation && (animation->max_pixels > 65536 || animation->max_encoded_bytes > 524288))) return 0;
    p = packet + PXA_HEADER_BYTES;
    pxa_store_u64(p, source); pxa_store_u16(p + 8, source_type);
    pxa_store_u16(p + 10, options.formats | (animation ? PXA_IMAGE_WITH_FRAMES : 0));
    pxa_store_u16(p + 12, options.max_width); pxa_store_u16(p + 14, options.max_height);
    pxa_store_u32(p + 16, options.max_pixels); pxa_store_u32(p + 20, options.max_encoded_bytes);
    if (animation) {
        pxa_store_u32(p + 24, animation->max_pixels);
        pxa_store_u32(p + 28, animation->max_encoded_bytes);
    }
    return pxa_finish_message_in_place(packet, capacity, PXA_ASSETS_SERVICE,
        PXA_ASSETS_DECODE_IMAGE, token, PXA_HEADER_BYTES + size, written);
}

static inline int32_t pxa_image_decode(uint64_t token, uint64_t source,
    uint16_t source_type, pxa_image_decode_options_t options,
    const pxa_image_animation_limits_t *animation) {
    uint8_t packet[PXA_HEADER_BYTES + 32]; uint32_t size;
    if (!pxa_image_decode_build(packet, sizeof(packet), token, source,
        source_type, options, animation, &size)) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_control_packet(packet, size);
}

/* Keep the old image alive while advancing. Success transfers a new handle;
 * close the old one after replacing its bindings. No timer/queue is created. */
static inline int32_t pxa_image_next_frame(uint64_t token, uint64_t image) {
    uint8_t payload[8];
    if (!token || !image) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u64(payload, image);
    return pxa_control_call(PXA_ASSETS_SERVICE, PXA_ASSETS_NEXT_IMAGE_FRAME, token,
        payload, sizeof(payload));
}

/* Parse before leaving the callback. On malformed success a recognizable
 * handle remains in out->handle so the caller can close it, including after a
 * cancellation that lost to publication. No imports are hidden in parsing. */
static inline int pxa_image_decode_parse(const pxa_event_t *event, uint64_t token,
    uint16_t opcode, int with_frames, pxa_image_decode_options_t options,
    pxa_image_decode_result_t *out) {
    const uint8_t *p;
    if (!out) return 0;
    pxa_zero(out, sizeof(*out));
    if (!event || !token || event->token != token || event->service != PXA_ASSETS_SERVICE ||
        event->opcode != opcode || (opcode != PXA_ASSETS_DECODE_IMAGE && opcode != PXA_ASSETS_NEXT_IMAGE_FRAME) ||
        (opcode == PXA_ASSETS_NEXT_IMAGE_FRAME && !with_frames) ||
        !event->payload || event->payload_size < 4) return 0;
    out->status = (int32_t)pxa_load_u32(event->payload);
    if (out->status) return event->payload_size == 4;
    if (event->payload_size >= 12) out->handle = pxa_load_u64(event->payload + 4);
    if (event->payload_size != (with_frames ? 52u : 40u) || !out->handle ||
        !pxa_image_decode_options_valid(options)) return 0;
    p = event->payload + 12;
    out->info.kind = p[0]; out->info.encoding = p[1];
    out->info.format_version = pxa_load_u16(p + 2);
    out->info.width = pxa_load_u16(p + 4); out->info.height = pxa_load_u16(p + 6);
    out->info.stored_bytes = pxa_load_u32(p + 8);
    out->info.decoded_bytes = pxa_load_u32(p + 12);
    out->info.resident_bytes = pxa_load_u32(p + 16);
    out->source_width = pxa_load_u16(p + 20); out->source_height = pxa_load_u16(p + 22);
    out->source_bytes = pxa_load_u32(p + 24);
    if (with_frames) {
        out->delay_ms = pxa_load_u32(p + 28); out->iteration = pxa_load_u32(p + 32);
        out->flags = pxa_load_u32(p + 36);
    }
    return out->info.kind == PXA_ASSET_IMAGE && out->source_width && out->source_height &&
        out->info.width && out->info.height && out->info.width <= options.max_width &&
        out->info.height <= options.max_height &&
        (uint32_t)out->info.width * out->info.height <= options.max_pixels &&
        out->source_bytes && out->source_bytes <= options.max_encoded_bytes && !(out->flags & ~1u);
}

#endif

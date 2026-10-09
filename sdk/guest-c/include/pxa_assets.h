#ifndef PXA_GUEST_ASSETS_H
#define PXA_GUEST_ASSETS_H

#include "pxa_core.h"

#define PXA_ASSETS_SERVICE 21u
#define PXA_ASSETS_QUERY 1u
#define PXA_ASSETS_LOAD 2u
#define PXA_ASSETS_PREFETCH 3u
#define PXA_ASSETS_STATUS 4u
#define PXA_ASSETS_READ 5u
#define PXA_ASSETS_READ_MAX_BYTES (PXA_MAX_CONTROL_BYTES - PXA_HEADER_BYTES - 12u)
#define PXA_ASSET_ABSENT 0u
#define PXA_ASSET_QUEUED 1u
#define PXA_ASSET_LOADING 2u
#define PXA_ASSET_READY 3u
#define PXA_ASSET_FAILED 4u
#define PXA_ASSET_TEXTURE 1u
#define PXA_ASSET_PALETTE 2u
#define PXA_ASSET_BLOB 5u
#define PXA_ASSET_AUDIO 3u
#define PXA_ASSET_IMAGE 4u
#define PXA_ASSET_ENCODING_RGB565 2u
#define PXA_ASSET_ENCODING_BGRA8888 7u
#define PXA_ASSET_ENCODING_BGRA8888_PREMULTIPLIED 8u
#define PXA_ASSET_ENCODING_PCM_S16LE_16K_MONO 9u
#define PXA_ASSET_PATH_MAX 255u

/* The Host C API owns pxa_asset_info_t for its resident asset objects, so the
 * Guest keeps its own name for the wire descriptor. */
typedef struct {
    uint8_t kind;
    uint8_t encoding;
    uint16_t format_version;
    uint16_t width;
    uint16_t height;
    uint32_t stored_bytes;
    uint32_t decoded_bytes;
    /* Final Host resident object estimate, excludes fixed worker/workspace. */
    uint32_t resident_bytes;
} pxa_asset_descriptor_t;

typedef struct {
    int32_t status;
    uint64_t handle;
    pxa_asset_descriptor_t info;
} pxa_asset_result_t;

typedef struct {
    int32_t status; /* Whether the snapshot query itself succeeded. */
    uint8_t state;
    int32_t load_status; /* Cache failure cause, meaningful for FAILED only. */
} pxa_asset_status_t;

typedef struct {
    int32_t status;
    uint32_t offset, total_bytes;
    const uint8_t *data; /* Borrowed only for this event callback. */
    uint32_t bytes;
} pxa_asset_read_result_t;

/* Assets 2.0: raw blob only, up to 4064 data bytes plus the 32-byte result
 * envelope within the 4096-byte control limit. Reads return the requested
 * length or stop at EOF, without block alignment. Use returned offset+bytes
 * for the next request, and total_bytes to detect EOF.
 * No Guest pointer is retained by Host. Cancel through the normal Core API. */
static inline int pxa_assets_build_read(uint8_t *packet, size_t capacity,
    uint64_t token, const char *path, uint32_t offset, uint32_t bytes, uint32_t *packet_size) {
    size_t length = 0;
    if (packet_size) *packet_size = 0;
    if (!packet || !path || !token || !bytes || bytes > PXA_ASSETS_READ_MAX_BYTES) return 0;
    while (length <= PXA_ASSET_PATH_MAX && path[length]) ++length;
    if (!length || length > PXA_ASSET_PATH_MAX || capacity < PXA_HEADER_BYTES+12+length) return 0;
    uint8_t *p = packet+PXA_HEADER_BYTES;
    pxa_store_u16(p,(uint16_t)length); p[2] = p[3] = 0;
    pxa_store_u32(p+4,offset); pxa_store_u32(p+8,bytes);
    for (size_t i = 0; i < length; ++i) p[12+i] = (uint8_t)path[i];
    return pxa_finish_message_in_place(packet,capacity,PXA_ASSETS_SERVICE,
        PXA_ASSETS_READ,token,PXA_HEADER_BYTES+12+length,packet_size);
}
static inline int32_t pxa_assets_read(uint64_t token, const char *path, uint32_t offset, uint32_t bytes) {
    uint8_t packet[PXA_HEADER_BYTES+12+PXA_ASSET_PATH_MAX]; uint32_t size;
    if (!pxa_assets_build_read(packet,sizeof(packet),token,path,offset,bytes,&size)) return -1;
    return pxa_submit(packet,size);
}
static inline int pxa_assets_parse_read(const pxa_event_t *event, uint64_t token,
    pxa_asset_read_result_t *output) {
    if (!output) return 0;
    pxa_zero(output,sizeof(*output));
    if (!event || !token || event->token != token || event->service != PXA_ASSETS_SERVICE ||
        event->opcode != PXA_ASSETS_READ || !event->payload || event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status) return event->payload_size == 4;
    if (event->payload_size < 12 || event->payload_size > 12+PXA_ASSETS_READ_MAX_BYTES) return 0;
    output->offset = pxa_load_u32(event->payload+4);
    output->total_bytes = pxa_load_u32(event->payload+8);
    output->bytes = event->payload_size-12;
    if (output->offset > output->total_bytes || output->bytes > output->total_bytes-output->offset ||
        (!output->bytes && output->offset != output->total_bytes)) return 0;
    output->data = event->payload+12;
    return 1;
}

/* Builders let callers reuse their own control buffer. kind is zero for QUERY/STATUS
 * and TEXTURE/PALETTE/AUDIO/IMAGE for LOAD/PREFETCH. Host applies the authenticated package scope. */
static inline int pxa_assets_build(uint8_t *packet, size_t capacity,
    uint16_t opcode, uint64_t token, const char *path, uint8_t kind,
    uint32_t *packet_size) {
    size_t length = 0;
    if (packet_size) *packet_size = 0;
    if (!packet || !path || !token ||
        (opcode != PXA_ASSETS_QUERY && opcode != PXA_ASSETS_LOAD &&
         opcode != PXA_ASSETS_PREFETCH && opcode != PXA_ASSETS_STATUS) ||
        ((opcode == PXA_ASSETS_QUERY || opcode == PXA_ASSETS_STATUS) && kind) ||
        ((opcode == PXA_ASSETS_LOAD || opcode == PXA_ASSETS_PREFETCH) && kind != PXA_ASSET_TEXTURE && kind != PXA_ASSET_PALETTE && kind != PXA_ASSET_AUDIO && kind != PXA_ASSET_IMAGE)) return 0;
    while (length <= PXA_ASSET_PATH_MAX && path[length]) ++length;
    if (!length || length > PXA_ASSET_PATH_MAX ||
        capacity < PXA_HEADER_BYTES + 4u + length) return 0;
    uint8_t *p = packet + PXA_HEADER_BYTES;
    pxa_store_u16(p, (uint16_t)length);
    p[2] = kind; p[3] = 0;
    for (size_t i = 0; i < length; ++i) p[4 + i] = (uint8_t)path[i];
    return pxa_finish_message_in_place(packet, capacity, PXA_ASSETS_SERVICE,
        opcode, token, PXA_HEADER_BYTES + 4u + length, packet_size);
}

static inline int32_t pxa_assets_query(uint64_t token, const char *path) {
    uint8_t packet[PXA_HEADER_BYTES + 4u + PXA_ASSET_PATH_MAX];
    uint32_t size;
    if (!pxa_assets_build(packet, sizeof(packet), PXA_ASSETS_QUERY, token, path, 0, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int32_t pxa_assets_load(uint64_t token, const char *path, uint8_t kind) {
    uint8_t packet[PXA_HEADER_BYTES + 4u + PXA_ASSET_PATH_MAX];
    uint32_t size;
    if (!pxa_assets_build(packet, sizeof(packet), PXA_ASSETS_LOAD, token, path, kind, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int32_t pxa_assets_load_texture(uint64_t token, const char *path) {
    return pxa_assets_load(token, path, PXA_ASSET_TEXTURE);
}
static inline int32_t pxa_assets_load_palette(uint64_t token, const char *path) {
    return pxa_assets_load(token, path, PXA_ASSET_PALETTE);
}

/* Assets 2.1: offline-compiled UI pixels, asynchronously prepared by Host.
 * The returned handle owns a reference; no pixel data enters Guest memory. */
static inline int32_t pxa_assets_load_image(uint64_t token, const char *path) {
    return pxa_assets_load(token,path,PXA_ASSET_IMAGE);
}

/* Assets 1.3: explicit asynchronous preparation of short PCM U8/16k/mono.
 * Success transfers a handle sharing Host bytes; no PCM crosses into Guest. */
static inline int32_t pxa_assets_load_sound(uint64_t token, const char *path) {
    return pxa_assets_load(token,path,PXA_ASSET_AUDIO);
}

/* Assets 1.1: prefetch uses Core cancellation and returns metadata, no handle.
 * Success leaves an evictable cache entry; LOAD before binding or rendering. */
static inline int32_t pxa_assets_prefetch(uint64_t token, const char *path, uint8_t kind) {
    uint8_t packet[PXA_HEADER_BYTES + 4u + PXA_ASSET_PATH_MAX];
    uint32_t size;
    if (!pxa_assets_build(packet, sizeof(packet), PXA_ASSETS_PREFETCH, token, path, kind, &size)) return -1;
    return pxa_submit(packet, size);
}
static inline int32_t pxa_assets_prefetch_texture(uint64_t token, const char *path) {
    return pxa_assets_prefetch(token, path, PXA_ASSET_TEXTURE);
}
static inline int32_t pxa_assets_prefetch_palette(uint64_t token, const char *path) {
    return pxa_assets_prefetch(token, path, PXA_ASSET_PALETTE);
}
/* Metadata-only snapshot. READY is not a residency promise; polling is not
 * required for completion (use the original request's result event). */
static inline int32_t pxa_assets_status(uint64_t token, const char *path) {
    uint8_t packet[PXA_HEADER_BYTES + 4u + PXA_ASSET_PATH_MAX];
    uint32_t size;
    if (!pxa_assets_build(packet, sizeof(packet), PXA_ASSETS_STATUS, token, path, 0, &size)) return -1;
    return pxa_submit(packet, size);
}
static inline int pxa_assets_parse_status(const pxa_event_t *event,
    uint64_t token, pxa_asset_status_t *output) {
    if (!output) return 0;
    pxa_zero(output, sizeof(*output));
    if (!event || !token || event->token != token || event->service != PXA_ASSETS_SERVICE ||
        event->opcode != PXA_ASSETS_STATUS || !event->payload || event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status) return event->payload_size == 4;
    if (event->payload_size != 12 || event->payload[4] > PXA_ASSET_FAILED ||
        event->payload[5] || event->payload[6] || event->payload[7]) return 0;
    output->state = event->payload[4];
    output->load_status = (int32_t)pxa_load_u32(event->payload + 8);
    return 1;
}

/* A true return means a structurally valid matching result, including errors.
 * Check output.status before using handle/info. LOAD success transfers one
 * handle: close it with pxa_close_handle. Cancel with pxa_cancel(token). */
static inline int pxa_assets_parse_result(const pxa_event_t *event,
    uint64_t token, uint16_t opcode, pxa_asset_result_t *output) {
    const uint8_t *p;
    if (!output) return 0;
    pxa_zero(output, sizeof(*output));
    if (!event || !token || event->token != token || event->service != PXA_ASSETS_SERVICE ||
        event->opcode != opcode || (opcode != PXA_ASSETS_QUERY && opcode != PXA_ASSETS_LOAD && opcode != PXA_ASSETS_PREFETCH) ||
        !event->payload || event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status) return event->payload_size == 4;
    if (event->payload_size != (opcode == PXA_ASSETS_LOAD ? 32u : 24u)) return 0;
    p = event->payload + 4;
    if (opcode == PXA_ASSETS_LOAD) {
        output->handle = pxa_load_u64(p);
        if (!output->handle) return 0;
        p += 8;
        if (p[0] != PXA_ASSET_TEXTURE && p[0] != PXA_ASSET_PALETTE && p[0] != PXA_ASSET_AUDIO && p[0] != PXA_ASSET_IMAGE) return 0;
    }
    output->info.kind = p[0]; output->info.encoding = p[1];
    output->info.format_version = pxa_load_u16(p + 2);
    output->info.width = pxa_load_u16(p + 4);
    output->info.height = pxa_load_u16(p + 6);
    output->info.stored_bytes = pxa_load_u32(p + 8);
    output->info.decoded_bytes = pxa_load_u32(p + 12);
    output->info.resident_bytes = pxa_load_u32(p + 16);
    return 1;
}
#endif

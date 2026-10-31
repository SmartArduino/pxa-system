#ifndef PXA_CONTAINER_H
#define PXA_CONTAINER_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/package.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_CONTAINER_HEADER_BYTES ((size_t)64)
#define PXA_CONTAINER_SIGNATURE_ENVELOPE_BYTES ((size_t)108)
#define PXA_CONTAINER_CHUNK_BYTES ((size_t)4096)
#define PXA_CONTAINER_FORMAT_MAJOR UINT16_C(0)
#define PXA_CONTAINER_FORMAT_MINOR UINT16_C(1)
#define PXA_CONTAINER_FORMAT_PATCH UINT16_C(0)
#define PXA_CONTAINER_SIGNATURE_FORMAT_MAJOR UINT16_C(0)
#define PXA_CONTAINER_SIGNATURE_FORMAT_MINOR UINT16_C(1)
#define PXA_CONTAINER_SIGNATURE_FORMAT_PATCH UINT16_C(0)
#define PXA_CONTAINER_SIGNATURE_FORMAT_VERSION UINT16_C(0x0001)

#define PXA_CONTAINER_STREAM_INDEX_ENTRY_BYTES ((size_t)48)
#define PXA_CONTAINER_MAX_CHUNK_BYTES ((size_t)32768)

#define PXA_CONTAINER_CODEC_STORE UINT16_C(0)
#define PXA_CONTAINER_CODEC_LZ4 UINT16_C(1)
#define PXA_CONTAINER_CODEC_DEFLATE UINT16_C(2)

typedef struct {
    uint16_t codec;
    uint16_t format_minor;
    uint32_t chunk_bytes;
    uint32_t index_size;
    uint64_t index_offset;
    uint32_t manifest_size;
    uint32_t package_signature_size;
    uint32_t container_signature_size;
    uint32_t file_count;
    uint64_t payload_size;
    uint64_t unpacked_size;
    uint64_t container_size;
    uint64_t manifest_offset;
    uint64_t package_signature_offset;
    uint64_t container_signature_offset;
    uint64_t payload_offset;
} pxa_container_header_t;

typedef struct {
    uint16_t algorithm;
    const uint8_t *publisher_key_id;
    const uint8_t *signature;
} pxa_container_signature_t;

uint32_t pxa_container_crc32(const uint8_t *data, size_t size);
pxa_status_t pxa_container_header_parse(
    pxa_bytes_t encoded, uint64_t source_size, pxa_container_header_t *output);
pxa_status_t pxa_container_signature_parse(
    pxa_bytes_t encoded, pxa_container_signature_t *output);

#ifdef __cplusplus
}
#endif

#endif

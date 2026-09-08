#include "pxa/container.h"

#include <string.h>

static int add_u64(uint64_t left, uint64_t right, uint64_t *output) {
    if (left > UINT64_MAX - right) return 0;
    *output = left + right;
    return 1;
}

uint32_t pxa_container_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = UINT32_C(0xffffffff);
    size_t index;
    if (data == NULL && size != 0) return 0;
    for (index = 0; index < size; ++index) {
        unsigned bit;
        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

pxa_status_t pxa_container_header_parse(
    pxa_bytes_t encoded, uint64_t source_size, pxa_container_header_t *output) {
    static const uint8_t magic[] = {'P', 'X', 'A', 'C'};
    uint64_t offset;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (encoded.data == NULL || encoded.size != PXA_CONTAINER_HEADER_BYTES ||
        memcmp(encoded.data, magic, sizeof(magic)) != 0 ||
        pxa_read_u16(encoded.data + 4) != PXA_CONTAINER_FORMAT_MAJOR ||
        pxa_read_u16(encoded.data + 6) != PXA_CONTAINER_FORMAT_MINOR ||
        pxa_read_u16(encoded.data + 8) != PXA_CONTAINER_HEADER_BYTES ||
        pxa_read_u16(encoded.data + 10) != 0 ||
        (pxa_read_u16(encoded.data + 12) != PXA_CONTAINER_CODEC_STORE &&
         pxa_read_u16(encoded.data + 12) != PXA_CONTAINER_CODEC_LZ4) ||
        pxa_read_u16(encoded.data + 14) != 12 ||
        pxa_read_u32(encoded.data + 16) < 12 ||
        pxa_read_u32(encoded.data + 20) !=
            PXA_CONTAINER_SIGNATURE_ENVELOPE_BYTES ||
        pxa_read_u32(encoded.data + 24) !=
            PXA_CONTAINER_SIGNATURE_ENVELOPE_BYTES ||
        pxa_read_u32(encoded.data + 28) > PXA_PACKAGE_MAX_FILES ||
        pxa_read_u32(encoded.data + 56) != 0 ||
        pxa_read_u32(encoded.data + 60) !=
            pxa_container_crc32(encoded.data, 60)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    output->codec = pxa_read_u16(encoded.data + 12);
    output->manifest_size = pxa_read_u32(encoded.data + 16);
    output->package_signature_size = pxa_read_u32(encoded.data + 20);
    output->container_signature_size = pxa_read_u32(encoded.data + 24);
    output->file_count = pxa_read_u32(encoded.data + 28);
    output->payload_size = pxa_read_u64(encoded.data + 32);
    output->unpacked_size = pxa_read_u64(encoded.data + 40);
    output->container_size = pxa_read_u64(encoded.data + 48);
    output->manifest_offset = PXA_CONTAINER_HEADER_BYTES;
    if (!add_u64(output->manifest_offset, output->manifest_size, &offset))
        return PXA_STATUS_INVALID_ARGUMENT;
    output->package_signature_offset = offset;
    if (!add_u64(offset, output->package_signature_size, &offset))
        return PXA_STATUS_INVALID_ARGUMENT;
    output->container_signature_offset = offset;
    if (!add_u64(offset, output->container_signature_size, &offset))
        return PXA_STATUS_INVALID_ARGUMENT;
    output->payload_offset = offset;
    if (!add_u64(offset, output->payload_size, &offset) ||
        offset != output->container_size || offset != source_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_container_signature_parse(
    pxa_bytes_t encoded, pxa_container_signature_t *output) {
    static const uint8_t magic[] = {'P', 'X', 'C', 'S'};
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (encoded.data == NULL ||
        encoded.size != PXA_CONTAINER_SIGNATURE_ENVELOPE_BYTES ||
        memcmp(encoded.data, magic, sizeof(magic)) != 0 ||
        pxa_read_u16(encoded.data + 4) !=
            PXA_CONTAINER_SIGNATURE_FORMAT_VERSION ||
        pxa_read_u16(encoded.data + 6) != 1 ||
        pxa_read_u16(encoded.data + 40) != PXA_PACKAGE_SIGNATURE_BYTES ||
        pxa_read_u16(encoded.data + 42) != 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    output->algorithm = 1;
    output->publisher_key_id = encoded.data + 8;
    output->signature = encoded.data + 44;
    return PXA_STATUS_OK;
}

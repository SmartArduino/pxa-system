/* Generated from spec/draft/pxa-device.json. Do not edit by hand. */
#ifndef PXA_DEVICE_RUNTIME_INFO_GENERATED_H
#define PXA_DEVICE_RUNTIME_INFO_GENERATED_H

#include <stddef.h>
#include <stdint.h>

#ifndef PXA_WIRE_GENERATED_H
#error "include the generated PXA wire codec first"
#endif

#define PXA_DEVICE_INFO_TAG_TARGET UINT16_C(1)
#define PXA_DEVICE_INFO_TAG_ARCHITECTURE UINT16_C(2)
#define PXA_DEVICE_INFO_TAG_ENGINE UINT16_C(3)
#define PXA_DEVICE_INFO_TAG_ENGINE_ABI UINT16_C(4)
#define PXA_DEVICE_INFO_TAG_FORMATS UINT16_C(5)
#define PXA_DEVICE_RUNTIME_INFO_MAX_RECORD_BYTES ((size_t)180)

typedef struct {
    int32_t status;
    char target[32];
    char architecture[24];
    char engine[24];
    char engine_abi[80];
    uint32_t formats;
} pxa_device_runtime_info_payload_t;

static inline int pxa_device_info_valid_utf8(
    const uint8_t *bytes, size_t size) {
    size_t index = 0;
    while (index < size) {
        uint8_t first = bytes[index++];
        unsigned continuation;
        if (first == 0) return 0;
        if (first < 0x80) continue;
        if (first < 0xc2 || first > 0xf4) return 0;
        continuation = first < 0xe0 ? 1u : first < 0xf0 ? 2u : 3u;
        if (continuation > size - index) return 0;
        uint8_t second = bytes[index];
        if (second < 0x80 || second > 0xbf ||
            (first == 0xe0 && second < 0xa0) ||
            (first == 0xed && second >= 0xa0) ||
            (first == 0xf0 && second < 0x90) ||
            (first == 0xf4 && second >= 0x90)) return 0;
        for (unsigned part = 0; part < continuation; ++part) {
            uint8_t byte = bytes[index++];
            if (byte < 0x80 || byte > 0xbf) return 0;
        }
    }
    return 1;
}

static inline int pxa_device_runtime_info_decode(
    const uint8_t *data, size_t size,
    pxa_device_runtime_info_payload_t *output) {
    size_t offset = 4;
    if (output == NULL) return 0;
    for (size_t i = 0; i < sizeof(*output); ++i)
        ((uint8_t *)output)[i] = 0;
    if (data == NULL || size < 4) return 0;
    output->status = (int32_t)pxa_wire_generated_load_u32(data);
    if (output->status != 0) return size == 4;
    {
        pxa_wire_record_view_t record;
        size_t consumed = 0;
        if (offset >= size ||
            !pxa_wire_record_decode(data + offset, size - offset,
                                    &record, &consumed) ||
            record.raw_tag != PXA_DEVICE_INFO_TAG_TARGET) return 0;
        if (record.payload_size == 0 ||
            record.payload_size > 31 ||
            !pxa_device_info_valid_utf8(record.payload,
                                        record.payload_size)) return 0;
        for (size_t i = 0; i < record.payload_size; ++i)
            output->target[i] = (char)record.payload[i];
        output->target[record.payload_size] = '\0';
        offset += consumed;
    }
    {
        pxa_wire_record_view_t record;
        size_t consumed = 0;
        if (offset >= size ||
            !pxa_wire_record_decode(data + offset, size - offset,
                                    &record, &consumed) ||
            record.raw_tag != PXA_DEVICE_INFO_TAG_ARCHITECTURE) return 0;
        if (record.payload_size == 0 ||
            record.payload_size > 23 ||
            !pxa_device_info_valid_utf8(record.payload,
                                        record.payload_size)) return 0;
        for (size_t i = 0; i < record.payload_size; ++i)
            output->architecture[i] = (char)record.payload[i];
        output->architecture[record.payload_size] = '\0';
        offset += consumed;
    }
    {
        pxa_wire_record_view_t record;
        size_t consumed = 0;
        if (offset >= size ||
            !pxa_wire_record_decode(data + offset, size - offset,
                                    &record, &consumed) ||
            record.raw_tag != PXA_DEVICE_INFO_TAG_ENGINE) return 0;
        if (record.payload_size == 0 ||
            record.payload_size > 23 ||
            !pxa_device_info_valid_utf8(record.payload,
                                        record.payload_size)) return 0;
        for (size_t i = 0; i < record.payload_size; ++i)
            output->engine[i] = (char)record.payload[i];
        output->engine[record.payload_size] = '\0';
        offset += consumed;
    }
    {
        pxa_wire_record_view_t record;
        size_t consumed = 0;
        if (offset >= size ||
            !pxa_wire_record_decode(data + offset, size - offset,
                                    &record, &consumed) ||
            record.raw_tag != PXA_DEVICE_INFO_TAG_ENGINE_ABI) return 0;
        if (record.payload_size == 0 ||
            record.payload_size > 79 ||
            !pxa_device_info_valid_utf8(record.payload,
                                        record.payload_size)) return 0;
        for (size_t i = 0; i < record.payload_size; ++i)
            output->engine_abi[i] = (char)record.payload[i];
        output->engine_abi[record.payload_size] = '\0';
        offset += consumed;
    }
    {
        pxa_wire_record_view_t record;
        size_t consumed = 0;
        if (offset >= size ||
            !pxa_wire_record_decode(data + offset, size - offset,
                                    &record, &consumed) ||
            record.raw_tag != PXA_DEVICE_INFO_TAG_FORMATS) return 0;
        if (record.payload_size != 4) return 0;
        output->formats =
            pxa_wire_generated_load_u32(record.payload);
        offset += consumed;
    }
    return offset == size;
}

static inline int pxa_device_runtime_info_encode(
    uint8_t *out, size_t capacity, const char *target, const char *architecture, const char *engine, const char *engine_abi,
    uint32_t formats, size_t *written) {
    size_t offset = 0;
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL) return 0;
    {
        size_t size = 0;
        size_t encoded = 0;
        if (target == NULL) return 0;
        while (size <= 31 && target[size] != '\0')
            ++size;
        if (size == 0 || size > 31 ||
            !pxa_device_info_valid_utf8(
                (const uint8_t *)target, size) ||
            offset > capacity ||
            !pxa_wire_record_encode(out + offset, capacity - offset,
                                    PXA_DEVICE_INFO_TAG_TARGET,
                                    (const uint8_t *)target, size,
                                    &encoded)) return 0;
        offset += encoded;
    }
    {
        size_t size = 0;
        size_t encoded = 0;
        if (architecture == NULL) return 0;
        while (size <= 23 && architecture[size] != '\0')
            ++size;
        if (size == 0 || size > 23 ||
            !pxa_device_info_valid_utf8(
                (const uint8_t *)architecture, size) ||
            offset > capacity ||
            !pxa_wire_record_encode(out + offset, capacity - offset,
                                    PXA_DEVICE_INFO_TAG_ARCHITECTURE,
                                    (const uint8_t *)architecture, size,
                                    &encoded)) return 0;
        offset += encoded;
    }
    {
        size_t size = 0;
        size_t encoded = 0;
        if (engine == NULL) return 0;
        while (size <= 23 && engine[size] != '\0')
            ++size;
        if (size == 0 || size > 23 ||
            !pxa_device_info_valid_utf8(
                (const uint8_t *)engine, size) ||
            offset > capacity ||
            !pxa_wire_record_encode(out + offset, capacity - offset,
                                    PXA_DEVICE_INFO_TAG_ENGINE,
                                    (const uint8_t *)engine, size,
                                    &encoded)) return 0;
        offset += encoded;
    }
    {
        size_t size = 0;
        size_t encoded = 0;
        if (engine_abi == NULL) return 0;
        while (size <= 79 && engine_abi[size] != '\0')
            ++size;
        if (size == 0 || size > 79 ||
            !pxa_device_info_valid_utf8(
                (const uint8_t *)engine_abi, size) ||
            offset > capacity ||
            !pxa_wire_record_encode(out + offset, capacity - offset,
                                    PXA_DEVICE_INFO_TAG_ENGINE_ABI,
                                    (const uint8_t *)engine_abi, size,
                                    &encoded)) return 0;
        offset += encoded;
    }
    {
        uint8_t value[4];
        size_t encoded = 0;
        pxa_wire_generated_store_u32(value, formats);
        if (offset > capacity ||
            !pxa_wire_record_encode(out + offset, capacity - offset,
                                    PXA_DEVICE_INFO_TAG_FORMATS, value,
                                    sizeof(value), &encoded)) return 0;
        offset += encoded;
    }
    *written = offset;
    return 1;
}

#endif

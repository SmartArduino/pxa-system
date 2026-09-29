#ifndef PXA_GUEST_WRITER_H
#define PXA_GUEST_WRITER_H

#include <stddef.h>
#include <stdint.h>

/* Small, transport-independent byte builder used by UI and Canvas. */
typedef struct {
    uint8_t* data;
    size_t capacity;
    size_t length;
    uint16_t records;
    uint8_t failed;
} pxa_writer_t;

static inline void pxa_writer_init(pxa_writer_t* writer, uint8_t* data,
                                   size_t capacity) {
    writer->data = data;
    writer->capacity = capacity;
    writer->length = 0;
    writer->records = 0;
    writer->failed = 0;
}

static inline int pxa_put_u8(pxa_writer_t* writer, uint8_t value) {
    if (writer == NULL || writer->failed || writer->data == NULL ||
        writer->length >= writer->capacity) {
        if (writer != NULL) writer->failed = 1;
        return 0;
    }
    writer->data[writer->length++] = value;
    return 1;
}

static inline int pxa_put_u16(pxa_writer_t* writer, uint16_t value) {
    return pxa_put_u8(writer, (uint8_t)value) &&
           pxa_put_u8(writer, (uint8_t)(value >> 8));
}

static inline int pxa_put_u32(pxa_writer_t* writer, uint32_t value) {
    return pxa_put_u8(writer, (uint8_t)value) &&
           pxa_put_u8(writer, (uint8_t)(value >> 8)) &&
           pxa_put_u8(writer, (uint8_t)(value >> 16)) &&
           pxa_put_u8(writer, (uint8_t)(value >> 24));
}

static inline int pxa_put_bytes(pxa_writer_t* writer, const uint8_t* data,
                                size_t length) {
    if (writer == NULL || writer->failed || writer->data == NULL ||
        writer->length > writer->capacity ||
        (data == NULL && length != 0) ||
        length > writer->capacity - writer->length) {
        if (writer != NULL) writer->failed = 1;
        return 0;
    }
    for (size_t i = 0; i < length; ++i) writer->data[writer->length++] = data[i];
    return 1;
}

static inline uint16_t pxa_read_u16(const uint8_t* value) {
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static inline uint32_t pxa_read_u32(const uint8_t* value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static inline uint64_t pxa_read_u64(const uint8_t* value) {
    return (uint64_t)pxa_read_u32(value) |
           ((uint64_t)pxa_read_u32(value + 4) << 32);
}

#endif

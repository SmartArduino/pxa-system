#ifndef PXA_WIRE_H
#define PXA_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/status.h"

#ifdef __cplusplus
extern "C" {
#endif
#define PXA_CORE_VERSION_MAJOR UINT16_C(0)
#define PXA_CORE_VERSION_MINOR UINT16_C(1)
#define PXA_CORE_VERSION_PATCH UINT16_C(0)
#define PXA_CORE_VERSION UINT32_C(0x00000001)
#define PXA_ENVELOPE_SIZE ((size_t)12)
#define PXA_RECORD_HEADER_SIZE ((size_t)4)
#define PXA_MAX_CONTROL_MESSAGE ((size_t)4096)
#define PXA_RECORD_OPTIONAL_MASK UINT16_C(0x8000)
#define PXA_RECORD_TAG_MASK UINT16_C(0x7fff)

typedef struct {
    const uint8_t *data;
    size_t size;
} pxa_bytes_t;

typedef struct {
    uint16_t service;
    uint16_t opcode;
    uint32_t request_id;
    pxa_bytes_t payload;
} pxa_message_view_t;

typedef struct {
    uint16_t raw_tag;
    uint16_t tag;
    uint8_t optional;
    pxa_bytes_t payload;
} pxa_record_view_t;

typedef struct {
    pxa_bytes_t bytes;
    size_t offset;
} pxa_record_iterator_t;

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t size;
    pxa_status_t status;
} pxa_writer_t;

/* Wire integers are little-endian. These byte-wise helpers are independent of
 * the Host CPU byte order and do not perform unaligned integer loads. */
uint16_t pxa_read_u16(const uint8_t *value);
uint32_t pxa_read_u32(const uint8_t *value);
uint64_t pxa_read_u64(const uint8_t *value);
void pxa_write_u16(uint8_t *output, uint16_t value);
void pxa_write_u32(uint8_t *output, uint32_t value);
void pxa_write_u64(uint8_t *output, uint64_t value);

pxa_status_t pxa_message_decode(const uint8_t *data, size_t size,
                                size_t max_size, pxa_message_view_t *output);
void pxa_record_iterator_init(pxa_record_iterator_t *iterator,
                              pxa_bytes_t bytes);
/* pxa_record_next() returns PXA_STATUS_WOULD_BLOCK at the exact end of input;
 * malformed or truncated input returns PXA_STATUS_INVALID_ARGUMENT. */
pxa_status_t pxa_record_next(pxa_record_iterator_t *iterator,
                             pxa_record_view_t *output);

void pxa_writer_init(pxa_writer_t *writer, uint8_t *data, size_t capacity);
pxa_status_t pxa_writer_bytes(pxa_writer_t *writer, const void *data,
                              size_t size);
pxa_status_t pxa_writer_u16(pxa_writer_t *writer, uint16_t value);
pxa_status_t pxa_writer_u32(pxa_writer_t *writer, uint32_t value);
pxa_status_t pxa_writer_u64(pxa_writer_t *writer, uint64_t value);
pxa_status_t pxa_writer_record(pxa_writer_t *writer, uint16_t raw_tag,
                               const void *payload, size_t payload_size);
pxa_status_t pxa_writer_message(pxa_writer_t *writer, uint16_t service,
                                uint16_t opcode, uint32_t request_id,
                                const void *payload, size_t payload_size);

#ifdef __cplusplus
}
#endif

#endif

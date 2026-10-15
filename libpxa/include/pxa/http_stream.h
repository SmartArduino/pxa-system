#ifndef PXA_HTTP_STREAM_H
#define PXA_HTTP_STREAM_H

#include "pxa/net.h"

#ifdef __cplusplus
extern "C" {
#endif

/* HTTP exchange I/O. POLL uses the shared PXA_READY_* mask. RESPONSE is an
 * immutable little-endian block: status:u16, flags:u16, header_bytes:u32,
 * length:u64, followed by {name_size:u16,value_size:u16,name,value} records.
 * Unknown body length is UINT64_MAX; framing is owned by the backend. */
#define PXA_HTTP_IO_RESPONSE UINT32_C(4)
#define PXA_HTTP_IO_RESPONSE_SIZE UINT32_C(5)
#define PXA_HTTP_IO_FINISH_UPLOAD UINT32_C(6)
#define PXA_HTTP_HEAD_BYTES ((size_t)16)
#define PXA_HTTP_MAX_BUFFER_BYTES UINT32_C(65536)
#define PXA_HTTP_MAX_HEAD_BYTES UINT32_C(4096)
#define PXA_HTTP_STREAM_HEADERS UINT32_C(1)
#define PXA_HTTP_STREAM_DONE UINT32_C(2)
#define PXA_HTTP_STREAM_CLOSED UINT32_C(4)
#define PXA_HTTP_STREAM_UPLOAD_DONE UINT32_C(8)

/* Single producer/single consumer queues. Publication uses acquire/release;
 * no locks, allocations or interrupt-disabled bulk copies on the data path.
 * Each direction permits one active operation. Host closes are serialized
 * with Guest I/O; the worker releases storage only after observing CLOSED. */
typedef struct {
    uint8_t *data;
    uint32_t capacity;
    uint32_t read_position;
    uint32_t write_position;
} pxa_http_ring_t;

typedef struct {
    pxa_http_ring_t receive;
    pxa_http_ring_t send;
    uint32_t state;
    int32_t error;
    uint64_t max_body_bytes;
    uint64_t upload_bytes;
    uint64_t received_bytes;
    uint64_t sent_bytes;
    uint64_t accepted_bytes;
    uint64_t content_length;
    uint8_t *head;
    uint32_t head_capacity;
    uint32_t head_size;
    uint16_t method;
} pxa_http_stream_t;

size_t pxa_http_stream_storage_size(uint32_t buffer_bytes, uint32_t header_bytes);
void pxa_http_stream_init(pxa_http_stream_t *stream, void *storage,
                          uint32_t buffer_bytes, uint32_t header_bytes,
                          uint64_t max_body_bytes, uint64_t upload_bytes,
                          uint16_t method);
uint32_t pxa_http_ring_readable(const pxa_http_ring_t *ring);
uint32_t pxa_http_ring_writable(const pxa_http_ring_t *ring);
size_t pxa_http_ring_peek(pxa_http_ring_t *ring, uint8_t **data, int writing);
void pxa_http_ring_advance(pxa_http_ring_t *ring, size_t count, int writing);
uint32_t pxa_http_stream_state(const pxa_http_stream_t *stream);
int pxa_http_stream_header(pxa_http_stream_t *stream, const uint8_t *name,
                           size_t name_size, const uint8_t *value, size_t value_size);
void pxa_http_stream_headers(pxa_http_stream_t *stream, uint16_t status,
                             uint64_t content_length);
void pxa_http_stream_done(pxa_http_stream_t *stream, pxa_status_t error);
int32_t pxa_http_stream_io(pxa_http_stream_t *stream, uint32_t operation, uint8_t *data,
                           size_t size);

#ifdef __cplusplus
}
#endif
#endif

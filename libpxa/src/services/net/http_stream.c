#include "pxa/http_stream.h"

#include <limits.h>
#include <string.h>

static uint32_t load(const uint32_t *value) {
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}
static void store(uint32_t *value, uint32_t input) {
    __atomic_store_n(value, input, __ATOMIC_RELEASE);
}

size_t pxa_http_stream_storage_size(uint32_t buffer_bytes, uint32_t header_bytes) {
    if (buffer_bytes < 512 || buffer_bytes > PXA_HTTP_MAX_BUFFER_BYTES ||
        (buffer_bytes & (buffer_bytes - 1u)) || header_bytes < PXA_HTTP_HEAD_BYTES ||
        header_bytes > PXA_HTTP_MAX_HEAD_BYTES)
        return 0;
    return (size_t)buffer_bytes + header_bytes;
}

void pxa_http_stream_init(pxa_http_stream_t *s, void *storage, uint32_t buffer_bytes,
                          uint32_t header_bytes, uint64_t max_body_bytes,
                          uint64_t upload_bytes, uint16_t method) {
    uint32_t send_bytes = upload_bytes ? buffer_bytes / 2u : 0;
    memset(s, 0, sizeof(*s));
    s->receive.data = storage;
    s->receive.capacity = buffer_bytes - send_bytes;
    s->send.data = (uint8_t *)storage + s->receive.capacity;
    s->send.capacity = send_bytes;
    s->head = (uint8_t *)storage + buffer_bytes;
    s->head_capacity = header_bytes;
    s->head_size = PXA_HTTP_HEAD_BYTES;
    s->max_body_bytes = max_body_bytes;
    s->upload_bytes = upload_bytes;
    s->method = method;
    s->content_length = UINT64_MAX;
    if (!upload_bytes)
        s->state = PXA_HTTP_STREAM_UPLOAD_DONE;
}

uint32_t pxa_http_ring_readable(const pxa_http_ring_t *r) {
    return load(&r->write_position) - load(&r->read_position);
}
uint32_t pxa_http_ring_writable(const pxa_http_ring_t *r) {
    return r->capacity - pxa_http_ring_readable(r);
}
size_t pxa_http_ring_peek(pxa_http_ring_t *r, uint8_t **data, int writing) {
    uint32_t position, count;
    if (!r->capacity) {
        *data = NULL;
        return 0;
    }
    position = load(writing ? &r->write_position : &r->read_position);
    count = writing ? pxa_http_ring_writable(r) : pxa_http_ring_readable(r);
    position %= r->capacity;
    if (count > r->capacity - position)
        count = r->capacity - position;
    *data = r->data + position;
    return count;
}
void pxa_http_ring_advance(pxa_http_ring_t *r, size_t count, int writing) {
    uint32_t *position = writing ? &r->write_position : &r->read_position;
    store(position, load(position) + (uint32_t)count);
}
uint32_t pxa_http_stream_state(const pxa_http_stream_t *s) {
    return load(&s->state);
}

int pxa_http_stream_header(pxa_http_stream_t *s, const uint8_t *name, size_t name_size,
                           const uint8_t *value, size_t value_size) {
    size_t i, bytes;
    pxa_bytes_t n = {name, name_size};
    if (!pxa_net_header_name_valid(n, 0) || value_size > UINT16_MAX ||
        (value_size && !value) || (pxa_http_stream_state(s) & PXA_HTTP_STREAM_HEADERS))
        return 0;
    for (i = 0; i < value_size; ++i)
        if (value[i] != '\t' && (value[i] < 0x20 || value[i] == 0x7f))
            return 0;
    bytes = 4u + name_size + value_size;
    if (bytes > s->head_capacity - s->head_size)
        return 0;
    pxa_write_u16(s->head + s->head_size, (uint16_t)name_size);
    pxa_write_u16(s->head + s->head_size + 2, (uint16_t)value_size);
    for (i = 0; i < name_size; ++i) {
        uint8_t c = name[i];
        s->head[s->head_size + 4 + i] = c >= 'A' && c <= 'Z' ? c + 32 : c;
    }
    if (value_size)
        memcpy(s->head + s->head_size + 4 + name_size, value, value_size);
    s->head_size += (uint32_t)bytes;
    return 1;
}

void pxa_http_stream_headers(pxa_http_stream_t *s, uint16_t status, uint64_t length) {
    s->content_length = length;
    pxa_write_u16(s->head, status);
    pxa_write_u16(s->head + 2, length == UINT64_MAX ? 0 : 1);
    pxa_write_u32(s->head + 4, s->head_size - PXA_HTTP_HEAD_BYTES);
    pxa_write_u64(s->head + 8, length);
    __atomic_fetch_or(&s->state, PXA_HTTP_STREAM_HEADERS, __ATOMIC_RELEASE);
}
void pxa_http_stream_done(pxa_http_stream_t *s, pxa_status_t error) {
    s->error = error;
    __atomic_fetch_or(&s->state, PXA_HTTP_STREAM_DONE, __ATOMIC_RELEASE);
}

static size_t transfer(pxa_http_ring_t *ring, uint8_t *data, size_t size, int writing) {
    size_t total = 0;
    while (total < size) {
        uint8_t *block;
        size_t count = pxa_http_ring_peek(ring, &block, writing);
        if (count > size - total)
            count = size - total;
        if (!count)
            break;
        if (writing)
            memcpy(block, data + total, count);
        else
            memcpy(data + total, block, count);
        pxa_http_ring_advance(ring, count, writing);
        total += count;
    }
    return total;
}

int32_t pxa_http_stream_io(pxa_http_stream_t *s, uint32_t operation, uint8_t *data,
                           size_t size) {
    uint32_t state = pxa_http_stream_state(s);
    size_t count;
    if ((size && !data) || size > INT32_MAX)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (state & PXA_HTTP_STREAM_CLOSED)
        return PXA_STATUS_CANCELLED;
    switch (operation) {
    case PXA_IO_POLL: {
        uint32_t ready = 0;
        if (size)
            return PXA_STATUS_INVALID_ARGUMENT;
        if (state & PXA_HTTP_STREAM_HEADERS)
            ready |= PXA_READY_HEADERS;
        if (pxa_http_ring_readable(&s->receive))
            ready |= PXA_READY_READ;
        if (!(state & (PXA_HTTP_STREAM_DONE | PXA_HTTP_STREAM_UPLOAD_DONE)) &&
            s->accepted_bytes < s->upload_bytes && pxa_http_ring_writable(&s->send))
            ready |= PXA_READY_WRITE;
        if (state & PXA_HTTP_STREAM_DONE)
            ready |= PXA_READY_END | PXA_READY_READ;
        return (int32_t)ready;
    }
    case PXA_HTTP_IO_RESPONSE_SIZE:
        if (size)
            return PXA_STATUS_INVALID_ARGUMENT;
        if (state & PXA_HTTP_STREAM_HEADERS)
            return (int32_t)s->head_size;
        return state & PXA_HTTP_STREAM_DONE ? s->error : PXA_STATUS_WOULD_BLOCK;
    case PXA_HTTP_IO_RESPONSE:
        if (!(state & PXA_HTTP_STREAM_HEADERS))
            return state & PXA_HTTP_STREAM_DONE ? s->error : PXA_STATUS_WOULD_BLOCK;
        if (size < s->head_size)
            return PXA_STATUS_LIMIT_EXCEEDED;
        memcpy(data, s->head, s->head_size);
        return (int32_t)s->head_size;
    case PXA_HTTP_IO_FINISH_UPLOAD:
        if (size)
            return PXA_STATUS_INVALID_ARGUMENT;
        if (state & PXA_HTTP_STREAM_DONE)
            return s->error ? s->error : PXA_STATUS_BAD_STATE;
        if (s->accepted_bytes != s->upload_bytes)
            return PXA_STATUS_BAD_STATE;
        __atomic_fetch_or(&s->state, PXA_HTTP_STREAM_UPLOAD_DONE, __ATOMIC_RELEASE);
        return 0;
    case PXA_IO_READ:
        if (!size)
            return PXA_STATUS_INVALID_ARGUMENT;
        count = transfer(&s->receive, data, size, 0);
        if (count)
            return (int32_t)count;
        return state & PXA_HTTP_STREAM_DONE ? s->error : PXA_STATUS_WOULD_BLOCK;
    case PXA_IO_WRITE:
        if (!size)
            return PXA_STATUS_INVALID_ARGUMENT;
        if (state & (PXA_HTTP_STREAM_DONE | PXA_HTTP_STREAM_UPLOAD_DONE))
            return (state & PXA_HTTP_STREAM_DONE) && s->error ? s->error
                                                              : PXA_STATUS_BAD_STATE;
        if (size > s->upload_bytes - s->accepted_bytes)
            return PXA_STATUS_LIMIT_EXCEEDED;
        count = transfer(&s->send, data, size, 1);
        s->accepted_bytes += count;
        return count ? (int32_t)count : PXA_STATUS_WOULD_BLOCK;
    default:
        return PXA_STATUS_UNSUPPORTED;
    }
}

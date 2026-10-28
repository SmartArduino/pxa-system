/* SPDX-License-Identifier: MIT */
#include "pxa/socket_stream.h"
#include <string.h>
pxa_status_t pxa_socket_request_parse(pxa_bytes_t payload,
                                      pxa_socket_request_t *out,
                                      pxa_handle64_t *permission) {
  if (!out || !permission)
    return PXA_STATUS_INVALID_ARGUMENT;
  memset(out, 0, sizeof(*out));
  *permission = 0;
  pxa_record_iterator_t it;
  pxa_record_view_t r;
  pxa_record_iterator_init(&it, payload);
  unsigned seen = 0, previous = 0;
  for (;;) {
    pxa_status_t status = pxa_record_next(&it, &r);
    if (status == PXA_STATUS_WOULD_BLOCK)
      break;
    if (status || r.raw_tag < previous)
      return PXA_STATUS_INVALID_ARGUMENT;
    previous = r.raw_tag;
    if (r.optional)
      continue;
    if (r.tag == 1 && !(seen & 1)) {
      out->endpoint = r.payload;
      seen |= 1;
    } else if (r.tag == 3 && !(seen & 2) && r.payload.size == 8) {
      *permission = pxa_read_u64(r.payload.data);
      seen |= 2;
    } else if (r.tag == 8 && !(seen & 4) && r.payload.size == 4) {
      out->connect_timeout_ms = pxa_read_u32(r.payload.data);
      seen |= 4;
    } else if (r.tag == 14 && !(seen & 8) && r.payload.size == 4) {
      out->buffer_bytes = pxa_read_u32(r.payload.data);
      seen |= 8;
    } else
      return PXA_STATUS_INVALID_ARGUMENT;
  }
  if (seen != 15 || !(*permission >> 32) || out->connect_timeout_ms < 100 ||
      out->connect_timeout_ms > 60000 || out->buffer_bytes < 512 ||
      out->buffer_bytes > 65536 ||
      (out->buffer_bytes & (out->buffer_bytes - 1)))
    return PXA_STATUS_INVALID_ARGUMENT;
  pxa_bytes_t endpoint = out->endpoint;
  if (endpoint.size < 9 || endpoint.size > 300 ||
      (memcmp(endpoint.data, "tcp://", 6) &&
       memcmp(endpoint.data, "tls://", 6)))
    return PXA_STATUS_INVALID_ARGUMENT;
  out->tls = endpoint.data[1] == 'l';
  size_t start = 6, end = endpoint.size, colon = end;
  if (endpoint.data[start] == '[') {
    start++;
    end = start;
    while (end < endpoint.size && endpoint.data[end] != ']') {
      uint8_t c = endpoint.data[end++];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == ':' ||
            c == '.'))
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (end == start || end + 2 >= endpoint.size ||
        endpoint.data[end + 1] != ':')
      return PXA_STATUS_INVALID_ARGUMENT;
    colon = end + 1;
  } else {
    for (size_t i = start; i < endpoint.size; i++) {
      uint8_t c = endpoint.data[i];
      if (c == ':') {
        colon = i;
        break;
      }
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
            c == '-'))
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = colon;
    if (end == start || end - start > 253 || endpoint.data[start] == '.' ||
        endpoint.data[end - 1] == '.')
      return PXA_STATUS_INVALID_ARGUMENT;
  }
  if (colon + 1 >= endpoint.size)
    return PXA_STATUS_INVALID_ARGUMENT;
  uint32_t port = 0;
  for (size_t i = colon + 1; i < endpoint.size; i++) {
    uint8_t c = endpoint.data[i];
    if (c < '0' || c > '9')
      return PXA_STATUS_INVALID_ARGUMENT;
    port = port * 10 + c - '0';
    if (port > 65535)
      return PXA_STATUS_INVALID_ARGUMENT;
  }
  if (!port || endpoint.data[colon + 1] == '0')
    return PXA_STATUS_INVALID_ARGUMENT;
  out->port = (uint16_t)port;
  out->host = (pxa_bytes_t){endpoint.data + start, end - start};
  return PXA_STATUS_OK;
}
int pxa_socket_stream_init(pxa_socket_stream_t *s, void *storage,
                           uint32_t bytes) {
  if (!s || !storage || bytes < 512 || bytes > 65536 || (bytes & (bytes - 1)))
    return 0;
  memset(s, 0, sizeof(*s));
  s->receive.data = storage;
  s->send.data = (uint8_t *)storage + bytes / 2;
  s->receive.capacity = s->send.capacity = bytes / 2;
  return 1;
}
void pxa_socket_stream_finish(pxa_socket_stream_t *s, int32_t error) {
  s->error = error;
  __atomic_fetch_or(&s->state, PXA_SOCKET_DONE, __ATOMIC_RELEASE);
}
static size_t transfer(pxa_http_ring_t *r, uint8_t *data, size_t size,
                       int writing) {
  size_t total = 0;
  while (total < size) {
    uint8_t *memory;
    size_t n = pxa_http_ring_peek(r, &memory, writing);
    if (!n)
      break;
    if (n > size - total)
      n = size - total;
    if (writing)
      memcpy(memory, data + total, n);
    else
      memcpy(data + total, memory, n);
    pxa_http_ring_advance(r, n, writing);
    total += n;
  }
  return total;
}
int32_t pxa_socket_stream_io(pxa_socket_stream_t *s, uint32_t op, uint8_t *data,
                             size_t size) {
  uint32_t state = __atomic_load_n(&s->state, __ATOMIC_ACQUIRE);
  if (op == PXA_SOCKET_IO_STATE)
    return size                           ? PXA_STATUS_INVALID_ARGUMENT
           : state & PXA_SOCKET_DONE      ? (s->error ? s->error : 0)
           : state & PXA_SOCKET_CONNECTED ? (int32_t)1
                                          : PXA_STATUS_WOULD_BLOCK;
  if (op == PXA_IO_POLL) {
    if (size)
      return PXA_STATUS_INVALID_ARGUMENT;
    return (pxa_http_ring_readable(&s->receive) ? PXA_READY_READ : 0) |
           ((state & PXA_SOCKET_CONNECTED) &&
                    !(state & (PXA_SOCKET_DONE | PXA_SOCKET_CLOSED)) &&
                    pxa_http_ring_writable(&s->send)
                ? PXA_READY_WRITE
                : 0) |
           (state & PXA_SOCKET_DONE ? PXA_READY_END : 0);
  }
  if (!data || !size)
    return PXA_STATUS_INVALID_ARGUMENT;
  if (op == PXA_IO_READ) {
    size_t n = transfer(&s->receive, data, size, 0);
    return n                         ? (int32_t)n
           : state & PXA_SOCKET_DONE ? s->error
                                     : PXA_STATUS_WOULD_BLOCK;
  }
  if (op == PXA_IO_WRITE) {
    if (state & (PXA_SOCKET_DONE | PXA_SOCKET_CLOSED))
      return s->error ? s->error : PXA_STATUS_BAD_STATE;
    if (!(state & PXA_SOCKET_CONNECTED))
      return PXA_STATUS_WOULD_BLOCK;
    size_t n = transfer(&s->send, data, size, 1);
    return n ? (int32_t)n : PXA_STATUS_WOULD_BLOCK;
  }
  return PXA_STATUS_UNSUPPORTED;
}

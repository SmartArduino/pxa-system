/* SPDX-License-Identifier: MIT */
#undef NDEBUG
#include "pxa/socket_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static size_t request(uint8_t *out, const char *endpoint, uint32_t bytes) {
  pxa_writer_t w;
  uint8_t value[8];
  pxa_writer_init(&w, out, 512);
  assert(!pxa_writer_record(&w, 1, endpoint, strlen(endpoint)));
  pxa_write_u64(value, UINT64_C(0x100000001));
  assert(!pxa_writer_record(&w, 3, value, 8));
  pxa_write_u32(value, 15000);
  assert(!pxa_writer_record(&w, 8, value, 4));
  pxa_write_u32(value, bytes);
  assert(!pxa_writer_record(&w, 14, value, 4));
  return w.size;
}
int main(void) {
  uint8_t bytes[512], storage[512], in[400], out[400];
  pxa_socket_request_t parsed;
  pxa_handle64_t permission;
  const char *valid[] = {"tcp://127.0.0.1:1234", "tls://example.com:443",
                         "tcp://[::1]:8000"};
  for (unsigned i = 0; i < 3; i++) {
    size_t n = request(bytes, valid[i], 512);
    assert(!pxa_socket_request_parse((pxa_bytes_t){bytes, n}, &parsed,
                                     &permission));
    assert(parsed.tls == (i == 1));
  }
  const char *bad[] = {"udp://host:80",      "tcp://user@host:80",
                       "tcp://host:0",       "tcp://host:65536",
                       "tcp://host:080",     "tls://host:443/path",
                       "tcp://host:80\r\nx", "tcp://host",
                       "tcp://:80",          "tcp://[::1:80"};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    size_t n = request(bytes, bad[i], 512);
    assert(pxa_socket_request_parse((pxa_bytes_t){bytes, n}, &parsed,
                                    &permission) ==
           PXA_STATUS_INVALID_ARGUMENT);
  }
  size_t n = request(bytes, valid[0], 513);
  assert(pxa_socket_request_parse((pxa_bytes_t){bytes, n}, &parsed,
                                  &permission) == PXA_STATUS_INVALID_ARGUMENT);
  pxa_socket_stream_t s;
  assert(pxa_socket_stream_init(&s, storage, 512));
  memset(in, 37, sizeof(in));
  assert(pxa_socket_stream_io(&s, 2, in, 10) == PXA_STATUS_WOULD_BLOCK);
  assert(pxa_socket_stream_io(&s, 4, NULL, 0) == PXA_STATUS_WOULD_BLOCK);
  __atomic_store_n(&s.state, PXA_SOCKET_CONNECTED, __ATOMIC_RELEASE);
  assert(pxa_socket_stream_io(&s, 2, in, 400) == 256);
  assert(pxa_socket_stream_io(&s, 2, in, 1) == PXA_STATUS_WOULD_BLOCK);
  pxa_http_ring_advance(&s.send, 251, 0);
  assert(pxa_socket_stream_io(&s, 2, in, 250) ==
         250); /* wraparound and partial writes */
  for (unsigned i = 0; i < 256; i++)
    storage[i] = (uint8_t)i;
  pxa_http_ring_advance(&s.receive, 256, 1);
  pxa_socket_stream_finish(&s, PXA_STATUS_IO_ERROR);
  assert(pxa_socket_stream_io(&s, 1, out, 256) ==
         256); /* drain before reporting EOF/error */
  for (unsigned i = 0; i < 256; i++)
    assert(out[i] == (uint8_t)i);
  assert(pxa_socket_stream_io(&s, 1, out, 1) == PXA_STATUS_IO_ERROR);
  assert(pxa_socket_stream_io(&s, 2, in, 1) == PXA_STATUS_IO_ERROR);
  assert(pxa_socket_stream_io(&s, 3, NULL, 0) & PXA_READY_END);
  assert(pxa_socket_stream_init(&s, storage, 512));
  pxa_socket_stream_finish(&s, 0);
  assert(!pxa_socket_stream_io(&s, 1, out, 1));
  puts("socket stream: endpoint validation, bounded partial I/O, wrap, "
       "backpressure and EOF/error passed");
}

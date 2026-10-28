/* SPDX-License-Identifier: MIT */
#ifndef PXA_SOCKET_STREAM_H
#define PXA_SOCKET_STREAM_H
#include "pxa/http_stream.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PXA_SOCKET_CONNECTED 1u
#define PXA_SOCKET_DONE 2u
#define PXA_SOCKET_CLOSED 4u
#define PXA_SOCKET_IO_STATE 4u
typedef struct {
  pxa_http_ring_t receive, send;
  uint32_t state;
  int32_t error;
} pxa_socket_stream_t;
/* Socket-open TLV: 1 endpoint, 3 permission:u64, 8 connect-timeout:u32,
 * 14 buffer-bytes:u32. All required, ordered and unique. */
pxa_status_t pxa_socket_request_parse(pxa_bytes_t, pxa_socket_request_t *,
                                      pxa_handle64_t *);
int pxa_socket_stream_init(pxa_socket_stream_t *, void *, uint32_t);
int32_t pxa_socket_stream_io(pxa_socket_stream_t *, uint32_t, uint8_t *,
                             size_t);
void pxa_socket_stream_finish(pxa_socket_stream_t *, int32_t);
#ifdef __cplusplus
}
#endif
#endif

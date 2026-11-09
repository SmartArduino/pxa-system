/* SPDX-License-Identifier: MIT */
#ifndef PXA_GUEST_LAN_H
#define PXA_GUEST_LAN_H
#include "pxa_core.h"
#define PXA_LOCAL_NETWORK_SERVICE 9u
#define PXA_LOCAL_NETWORK_OPEN 7u
#define PXA_LOCAL_NETWORK_CAPABILITIES 8u
#define PXA_NET_FEATURE_LOCAL_IPV4 256u
#define PXA_LOCAL_LISTENER 1u
#define PXA_LOCAL_DATAGRAM 2u
#define PXA_LOCAL_ADVERTISE 3u
#define PXA_LOCAL_BROWSE 4u
#define PXA_LOCAL_NO_DELAY 1u
#define PXA_LOCAL_BROADCAST 2u
#define PXA_LOCAL_IO_ENDPOINT 5u
#define PXA_LOCAL_IO_PEER 6u
#define PXA_LOCAL_IO_ACCEPT 7u
#define PXA_LOCAL_IO_STATS 8u
#define PXA_NET_FEATURE_LISTEN 32u
#define PXA_NET_FEATURE_DATAGRAM 64u
#define PXA_NET_FEATURE_DISCOVERY 128u
#define PXA_LOCAL_SCOPE "local-network"
typedef struct {
  uint16_t family, port;
  uint32_t scope;
  uint8_t address[16];
} pxa_local_endpoint_t;
typedef struct {
  uint64_t permission;
  uint16_t kind, flags;
  pxa_local_endpoint_t bind;
  uint32_t buffer_bytes;
  uint16_t max_items, type_size, name_size, txt_size;
  const char *type, *name;
  const uint8_t *txt;
} pxa_local_request_t;
static inline void pxa_local_endpoint_encode(uint8_t *p,
                                             const pxa_local_endpoint_t *e) {
  pxa_store_u16(p, e->family);
  pxa_store_u16(p + 2, e->port);
  pxa_store_u32(p + 4, e->scope);
  for (unsigned i = 0; i < 16; ++i)
    p[8 + i] = e->address[i];
}
/* Caller owns scratch until control admission; no hidden allocations. */
static inline int32_t pxa_local_open(uint64_t token,
                                     const pxa_local_request_t *r,
                                     uint8_t *scratch, uint32_t capacity) {
  uint32_t n;
  if (!token || !r || !scratch || !(r->permission >> 32) || r->type_size > 63 ||
      r->name_size > 54 || r->txt_size > 256 || (r->type_size && !r->type) ||
      (r->name_size && !r->name) || (r->txt_size && !r->txt))
    return -1;
  n = 48u + r->type_size + r->name_size + r->txt_size;
  if (capacity < n)
    return -1;
  pxa_store_u64(scratch, r->permission);
  pxa_store_u16(scratch + 8, r->kind);
  pxa_store_u16(scratch + 10, r->flags);
  pxa_local_endpoint_encode(scratch + 12, &r->bind);
  pxa_store_u32(scratch + 36, r->buffer_bytes);
  pxa_store_u16(scratch + 40, r->max_items);
  pxa_store_u16(scratch + 42, r->type_size);
  pxa_store_u16(scratch + 44, r->name_size);
  pxa_store_u16(scratch + 46, r->txt_size);
  uint32_t at = 48;
  for (unsigned i = 0; i < r->type_size; ++i)
    scratch[at++] = (uint8_t)r->type[i];
  for (unsigned i = 0; i < r->name_size; ++i)
    scratch[at++] = (uint8_t)r->name[i];
  for (unsigned i = 0; i < r->txt_size; ++i)
    scratch[at++] = r->txt[i];
  return pxa_control_call(9, 7, token, scratch, n);
}
#endif

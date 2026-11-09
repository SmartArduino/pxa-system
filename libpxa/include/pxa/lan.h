/* SPDX-License-Identifier: MIT */
#ifndef PXA_LAN_H
#define PXA_LAN_H
#include "pxa/net.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Net 0.7 LOCAL_OPEN. Addresses are bytes, never native sockaddr structures. */
#define PXA_NET_LOCAL_OPEN 7u
#define PXA_NET_LOCAL_CAPABILITIES 8u
#define PXA_NET_FEATURE_LOCAL_IPV4 256u
#define PXA_NET_FEATURE_LISTEN 32u
#define PXA_NET_FEATURE_DATAGRAM 64u
#define PXA_NET_FEATURE_DISCOVERY 128u
#define PXA_LAN_LISTENER 1u
#define PXA_LAN_DATAGRAM 2u
#define PXA_LAN_ADVERTISE 3u
#define PXA_LAN_BROWSE 4u
#define PXA_LAN_STREAM 5u
#define PXA_LAN_NO_DELAY 1u
#define PXA_LAN_BROADCAST 2u
#define PXA_LAN_ENDPOINT_BYTES 24u
#define PXA_LAN_OPEN_BYTES 48u
#define PXA_LAN_MAX_TYPE 21u
#define PXA_LAN_MAX_NAME 54u
#define PXA_LAN_MAX_TXT 256u
#define PXA_LAN_IO_LOCAL 5u
#define PXA_LAN_IO_PEER 6u
#define PXA_LAN_IO_ACCEPT 7u
#define PXA_LAN_IO_STATS 8u
typedef struct {
  uint16_t family, port; /* 4 or 6; host-endian scalar, network-order address */
  uint32_t scope;
  uint8_t address[16]; /* IPv4 occupies the first four bytes. */
} pxa_net_endpoint_t;
typedef struct pxa_lan_request {
  pxa_net_endpoint_t bind;
  uint32_t buffer_bytes;
  uint16_t kind, flags, max_items;
  /* Host-derived permission scope; never read from Guest flags. */
  uint8_t network_scope;
  pxa_bytes_t service_type, instance, txt;
} pxa_lan_request_t;
/* All callbacks run on the runtime owner. OPEN only allocates bounded state;
 * Native work belongs to a provider worker; POLL snapshots readiness changes.
 */
typedef struct pxa_lan_backend {
  uint32_t struct_size, features;
  void *context;
  pxa_status_t (*open)(void *, const pxa_lan_request_t *, void **);
  int32_t (*io)(void *, void *, uint32_t, uint8_t *, size_t);
  pxa_status_t (*accept)(void *, void *, void **);
  void (*close)(void *, void *);
  int (*poll)(void *);
  uint16_t max_resources;
  uint32_t max_buffer_bytes;
} pxa_lan_backend_t;
pxa_status_t pxa_lan_request_parse(pxa_bytes_t, pxa_lan_request_t *,
                                   pxa_handle64_t *);
void pxa_net_endpoint_encode(uint8_t *, const pxa_net_endpoint_t *);
pxa_status_t pxa_net_endpoint_decode(const uint8_t *, size_t,
                                     pxa_net_endpoint_t *);
#ifdef __cplusplus
}
#endif
#endif

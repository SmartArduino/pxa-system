/* SPDX-License-Identifier: MIT */
#ifndef PXA_LAN_NATIVE_H
#define PXA_LAN_NATIVE_H
#include "pxa/lan.h"
/* IPv4 interface/address and mask in network byte order. Desktop supplies a
 * selected active LAN interface; ESP uses its active default netif. */
typedef struct {
  void *context;
  int (*interface)(void *, uint32_t *, uint32_t *);
  void (*notify)(void *);
  uint16_t max_resources;
  uint32_t max_buffer_bytes;
} pxa_lan_native_config_t;
int pxa_lan_native_create(const pxa_lan_native_config_t *, pxa_lan_backend_t *);
void pxa_lan_native_destroy(pxa_lan_backend_t *);
#endif

/* SPDX-License-Identifier: MIT */
#include "common/bytes_internal.h"
#include "pxa/lan.h"
#include <string.h>
void pxa_net_endpoint_encode(uint8_t *p, const pxa_net_endpoint_t *e) {
  pxa_write_u16(p, e->family);
  pxa_write_u16(p + 2, e->port);
  pxa_write_u32(p + 4, e->scope);
  memcpy(p + 8, e->address, 16);
}
pxa_status_t pxa_net_endpoint_decode(const uint8_t *p, size_t n,
                                     pxa_net_endpoint_t *e) {
  if (!p || !e || n < 24)
    return PXA_STATUS_INVALID_ARGUMENT;
  memset(e, 0, sizeof(*e));
  e->family = pxa_read_u16(p);
  e->port = pxa_read_u16(p + 2);
  e->scope = pxa_read_u32(p + 4);
  memcpy(e->address, p + 8, 16);
  if (e->family != 4 && e->family != 6)
    return PXA_STATUS_INVALID_ARGUMENT;
  if (e->family == 4) {
    if (e->scope)
      return PXA_STATUS_INVALID_ARGUMENT;
    for (unsigned i = 4; i < 16; ++i)
      if (e->address[i])
        return PXA_STATUS_INVALID_ARGUMENT;
  }
  return PXA_STATUS_OK;
}
static int service_type(pxa_bytes_t t) {
  if (t.size < 7 || t.size > 21 || t.data[0] != '_')
    return 0;
  size_t n = t.size - 5;
  if (n < 2 ||
      (memcmp(t.data + n, "._tcp", 5) && memcmp(t.data + n, "._udp", 5)))
    return 0;
  for (size_t i = 1; i < n; ++i)
    if (!((t.data[i] >= 'a' && t.data[i] <= 'z') ||
          (t.data[i] >= '0' && t.data[i] <= '9') || t.data[i] == '-'))
      return 0;
  return t.data[1] != '-' && t.data[n - 1] != '-';
}
pxa_status_t pxa_lan_request_parse(pxa_bytes_t b, pxa_lan_request_t *r,
                                   pxa_handle64_t *permission) {
  if (!r || !permission || !b.data || b.size < 48)
    return PXA_STATUS_INVALID_ARGUMENT;
  memset(r, 0, sizeof(*r));
  *permission = pxa_read_u64(b.data);
  r->kind = pxa_read_u16(b.data + 8);
  r->flags = pxa_read_u16(b.data + 10);
  if (!(*permission >> 32) ||
      pxa_net_endpoint_decode(b.data + 12, 24, &r->bind))
    return PXA_STATUS_INVALID_ARGUMENT;
  r->buffer_bytes = pxa_read_u32(b.data + 36);
  r->max_items = pxa_read_u16(b.data + 40);
  size_t type = pxa_read_u16(b.data + 42), name = pxa_read_u16(b.data + 44),
         txt = pxa_read_u16(b.data + 46);
  if (type > 63 || name > 63 || txt > 256 || b.size != 48 + type + name + txt ||
      !r->max_items || r->max_items > 8 || r->buffer_bytes < 512 ||
      r->buffer_bytes > 16384 || (r->buffer_bytes & (r->buffer_bytes - 1)))
    return PXA_STATUS_INVALID_ARGUMENT;
  r->service_type = (pxa_bytes_t){b.data + 48, type};
  r->instance = (pxa_bytes_t){b.data + 48 + type, name};
  r->txt = (pxa_bytes_t){b.data + 48 + type + name, txt};
  if (r->kind == PXA_LAN_LISTENER || r->kind == PXA_LAN_DATAGRAM) {
    if (type || name || txt ||
        r->flags & ~(r->kind == PXA_LAN_LISTENER ? 1u : 2u))
      return PXA_STATUS_INVALID_ARGUMENT;
  } else if (r->kind == PXA_LAN_ADVERTISE || r->kind == PXA_LAN_BROWSE) {
    if (r->flags || !service_type(r->service_type))
      return PXA_STATUS_INVALID_ARGUMENT;
    if (r->kind == PXA_LAN_ADVERTISE && (!name || name > 54 || !r->bind.port))
      return PXA_STATUS_INVALID_ARGUMENT;
    if (r->kind == PXA_LAN_BROWSE && (name || txt || r->bind.port))
      return PXA_STATUS_INVALID_ARGUMENT;
    if (!pxa_utf8_validate(r->instance.data, name,
                           PXA_UTF8_REJECT_C0 | PXA_UTF8_REJECT_DEL |
                               PXA_UTF8_REJECT_C1))
      return PXA_STATUS_INVALID_ARGUMENT;
    /* One UTF-8 DNS label, without escapes. */
    for (size_t i = 0; i < name; ++i)
      if (r->instance.data[i] < 32 || r->instance.data[i] == 127 ||
          r->instance.data[i] == '.' || r->instance.data[i] == '\\')
        return PXA_STATUS_INVALID_ARGUMENT;
    for (size_t at = 0; at < txt;) {
      size_t len = r->txt.data[at++];
      if (len > txt - at || (!len && txt != 1))
        return PXA_STATUS_INVALID_ARGUMENT;
      size_t key = 0;
      while (key < len && r->txt.data[at + key] != '=') {
        if (r->txt.data[at + key] < 0x20 || r->txt.data[at + key] > 0x7e)
          return PXA_STATUS_INVALID_ARGUMENT;
        ++key;
      }
      if (len && !key)
        return PXA_STATUS_INVALID_ARGUMENT;
      at += len;
    }
  } else
    return PXA_STATUS_INVALID_ARGUMENT;
  return PXA_STATUS_OK;
}

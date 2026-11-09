/* SPDX-License-Identifier: MIT */
#ifndef PXA_MDNS_PROBE_H
#define PXA_MDNS_PROBE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Canonical, uncompressed DNS RDATA. Only a bounded prefix is needed: our
 * largest unique RDATA is 256 bytes. Foreign longer TXT values compare after
 * this prefix and cannot equal ours. Three remote records suffice against
 * our at-most-two-record set, including the list-length tiebreak. */
#define PXA_MDNS_RDATA_BYTES 272
struct pxa_mdns_record_key {
  uint16_t type, length;
  uint8_t data[PXA_MDNS_RDATA_BYTES];
};
static uint16_t pxa_mdns_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}
static uint8_t pxa_mdns_lower(uint8_t c) {
  return c >= 'A' && c <= 'Z' ? (uint8_t)(c + 'a' - 'A') : c;
}
static int pxa_mdns_text_name(const char *name, uint8_t *out, size_t *length) {
  size_t at = 0;
  while (*name) {
    const char *end = strchr(name, '.');
    size_t n = end ? (size_t)(end - name) : strlen(name);
    if (!n || n > 63 || at + n + 2 > 256)
      return 0;
    out[at++] = (uint8_t)n;
    for (size_t i = 0; i < n; ++i)
      out[at++] = pxa_mdns_lower((uint8_t)name[i]);
    name += n;
    if (*name == '.')
      ++name;
  }
  out[at++] = 0;
  *length = at;
  return 1;
}
static int pxa_mdns_read_name(const uint8_t *packet, size_t size,
                              size_t *offset, uint8_t *out, size_t *length) {
  size_t at = *offset, end = 0, used = 0;
  for (unsigned hops = 0; hops < 128; ++hops) {
    if (at >= size)
      return 0;
    unsigned n = packet[at++];
    if ((n & 0xc0u) == 0xc0u) {
      if (at >= size)
        return 0;
      if (!end)
        end = at + 1;
      at = ((n & 63u) << 8) | packet[at];
      continue;
    }
    if (n > 63 || n > size - at || used + n + 1 > 255)
      return 0;
    out[used++] = (uint8_t)n;
    if (!n) {
      *offset = end ? end : at;
      *length = used;
      return 1;
    }
    for (unsigned i = 0; i < n; ++i)
      out[used++] = pxa_mdns_lower(packet[at++]);
  }
  return 0;
}
static int pxa_mdns_key_compare(const struct pxa_mdns_record_key *a,
                                const struct pxa_mdns_record_key *b) {
  if (a->type != b->type)
    return a->type > b->type ? 1 : -1;
  size_t n = a->length < b->length ? a->length : b->length;
  if (n > PXA_MDNS_RDATA_BYTES)
    n = PXA_MDNS_RDATA_BYTES;
  int result = memcmp(a->data, b->data, n);
  if (result)
    return result > 0 ? 1 : -1;
  return a->length == b->length ? 0 : a->length > b->length ? 1 : -1;
}
static int pxa_mdns_record_key(struct pxa_mdns_record_key *key, uint16_t type,
                               const uint8_t *packet, size_t size,
                               size_t offset, size_t length) {
  if (offset > size || length > size - offset || length > UINT16_MAX)
    return 0;
  key->type = type;
  key->length = (uint16_t)length;
  if (type == 33) {
    if (length < 7)
      return 0;
    memcpy(key->data, packet + offset, 6);
    size_t at = offset + 6, name_size;
    if (!pxa_mdns_read_name(packet, size, &at, key->data + 6, &name_size) ||
        at != offset + length)
      return 0;
    key->length = (uint16_t)(6 + name_size);
  } else {
    if (type == 1 && length != 4)
      return 0;
    memcpy(key->data, packet + offset,
           length < PXA_MDNS_RDATA_BYTES ? length : PXA_MDNS_RDATA_BYTES);
  }
  return 1;
}
/* Compare only the query's Authority Section, never stale Known Answers.
 * Positive means our sorted set wins; negative means defer for one second.
 * Zero also covers malformed/irrelevant packets, which cannot force rename. */
static int pxa_mdns_probe_compare(const uint8_t *packet, size_t size,
                                  const char *owner,
                                  const struct pxa_mdns_record_key *own,
                                  size_t own_count) {
  if (!packet || size < 12 || (packet[2] & 0x80) || !own_count || own_count > 2)
    return 0;
  uint8_t expected[256], name[256];
  size_t expected_size, name_size, at = 12;
  if (!pxa_mdns_text_name(owner, expected, &expected_size))
    return 0;
  unsigned questions = pxa_mdns_u16(packet + 4);
  for (unsigned i = 0; i < questions; ++i) {
    if (!pxa_mdns_read_name(packet, size, &at, name, &name_size) ||
        size - at < 4)
      return 0;
    at += 4;
  }
  struct pxa_mdns_record_key remote[3], key;
  size_t count = 0;
  for (unsigned section = 0; section < 3; ++section) {
    unsigned records = pxa_mdns_u16(packet + 6 + section * 2);
    for (unsigned i = 0; i < records; ++i) {
      if (!pxa_mdns_read_name(packet, size, &at, name, &name_size) ||
          size - at < 10)
        return 0;
      uint16_t type = pxa_mdns_u16(packet + at),
               klass = pxa_mdns_u16(packet + at + 2);
      size_t length = pxa_mdns_u16(packet + at + 8);
      at += 10;
      if (length > size - at)
        return 0;
      if (section == 1 && (klass & 0x7fff) == 1 && name_size == expected_size &&
          !memcmp(name, expected, name_size)) {
        if (!pxa_mdns_record_key(&key, type, packet, size, at, length))
          return 0;
        size_t pos = 0;
        while (pos < count && pxa_mdns_key_compare(&remote[pos], &key) < 0)
          ++pos;
        if (pos == count || pxa_mdns_key_compare(&remote[pos], &key)) {
          if (pos < 3) {
            if (count < 3)
              ++count;
            for (size_t j = count - 1; j > pos; --j)
              remote[j] = remote[j - 1];
            remote[pos] = key;
          }
        }
      }
      at += length;
    }
  }
  if (!count)
    return 0;
  for (size_t i = 0; i < count && i < own_count; ++i) {
    int compared = pxa_mdns_key_compare(&own[i], &remote[i]);
    if (compared)
      return compared;
  }
  return count == own_count ? 0 : own_count > count ? 1 : -1;
}
#endif

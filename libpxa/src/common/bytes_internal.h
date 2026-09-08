#ifndef PXA_INTERNAL_UTF8_H
#define PXA_INTERNAL_UTF8_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/wire.h"

#define PXA_UTF8_REJECT_C0 UINT8_C(1)
#define PXA_UTF8_REJECT_DEL UINT8_C(2)
#define PXA_UTF8_REJECT_C1 UINT8_C(4)
#define PXA_UTF8_ALLOW_TEXT_WHITESPACE UINT8_C(8)

int pxa_bytes_compare_internal(pxa_bytes_t left, pxa_bytes_t right);
int pxa_bytes_equal_internal(pxa_bytes_t left, pxa_bytes_t right);
int pxa_utf8_validate(const uint8_t *data, size_t size, uint8_t policy);

#endif

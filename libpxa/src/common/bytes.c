#include "common/bytes_internal.h"

#include <string.h>

int pxa_bytes_compare_internal(pxa_bytes_t left, pxa_bytes_t right) {
    size_t common = left.size < right.size ? left.size : right.size;
    int result = common == 0 ? 0 : memcmp(left.data, right.data, common);
    if (result != 0) return result;
    if (left.size < right.size) return -1;
    if (left.size > right.size) return 1;
    return 0;
}

int pxa_bytes_equal_internal(pxa_bytes_t left, pxa_bytes_t right) {
    return left.size == right.size &&
           (left.size == 0 || memcmp(left.data, right.data, left.size) == 0);
}

static int control_allowed(uint32_t codepoint, uint8_t policy) {
    if ((policy & PXA_UTF8_REJECT_C0) != 0 && codepoint < UINT32_C(0x20)) {
        return (policy & PXA_UTF8_ALLOW_TEXT_WHITESPACE) != 0 &&
               (codepoint == '\t' || codepoint == '\n' || codepoint == '\r');
    }
    if ((policy & PXA_UTF8_REJECT_DEL) != 0 && codepoint == UINT32_C(0x7f))
        return 0;
    if ((policy & PXA_UTF8_REJECT_C1) != 0 &&
        codepoint >= UINT32_C(0x80) && codepoint <= UINT32_C(0x9f))
        return 0;
    return 1;
}

int pxa_utf8_validate(const uint8_t *data, size_t size, uint8_t policy) {
    size_t offset = 0;
    if (data == NULL && size != 0) return 0;
    while (offset < size) {
        uint8_t first = data[offset++];
        uint32_t codepoint;
        uint32_t minimum;
        uint8_t remaining;
        if (first < UINT8_C(0x80)) {
            if (!control_allowed(first, policy)) return 0;
            continue;
        }
        if ((first & UINT8_C(0xe0)) == UINT8_C(0xc0)) {
            codepoint = first & UINT8_C(0x1f);
            minimum = UINT32_C(0x80);
            remaining = 1;
        } else if ((first & UINT8_C(0xf0)) == UINT8_C(0xe0)) {
            codepoint = first & UINT8_C(0x0f);
            minimum = UINT32_C(0x800);
            remaining = 2;
        } else if ((first & UINT8_C(0xf8)) == UINT8_C(0xf0)) {
            codepoint = first & UINT8_C(0x07);
            minimum = UINT32_C(0x10000);
            remaining = 3;
        } else {
            return 0;
        }
        while (remaining-- != 0) {
            uint8_t next;
            if (offset >= size) return 0;
            next = data[offset++];
            if ((next & UINT8_C(0xc0)) != UINT8_C(0x80)) return 0;
            codepoint = (codepoint << 6) | (next & UINT8_C(0x3f));
        }
        if (codepoint < minimum || codepoint > UINT32_C(0x10ffff) ||
            (codepoint >= UINT32_C(0xd800) &&
             codepoint <= UINT32_C(0xdfff)) ||
            !control_allowed(codepoint, policy)) {
            return 0;
        }
    }
    return 1;
}

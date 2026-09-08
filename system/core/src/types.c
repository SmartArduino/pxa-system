#include "pxsys/types.h"

#include <string.h>

pxsys_bytes_t pxsys_bytes(const void* data, size_t size) {
    pxsys_bytes_t value;
    value.data = (const uint8_t*)data;
    value.size = size;
    return value;
}

pxsys_string_t pxsys_string(const char* data, size_t size) {
    pxsys_string_t value;
    value.data = data;
    value.size = size;
    return value;
}

pxsys_string_t pxsys_string_from_cstr(const char* text) {
    return pxsys_string(text, text == NULL ? 0 : strlen(text));
}

int pxsys_identifier_validate(pxsys_string_t value, size_t maximum) {
    size_t index;
    if (value.data == NULL || value.size == 0 || value.size > maximum)
        return 0;
    for (index = 0; index < value.size; ++index) {
        const unsigned char byte = (unsigned char)value.data[index];
        const int allowed = (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
                            byte == '.' || byte == '_' || byte == '-';
        if (!allowed)
            return 0;
    }
    return 1;
}

static int utf8_continuation(unsigned char byte) { return (byte & 0xc0u) == 0x80u; }

int pxsys_display_text_validate(pxsys_string_t value, size_t maximum) {
    size_t index = 0;
    if (value.data == NULL || value.size == 0 || value.size > maximum)
        return 0;
    while (index < value.size) {
        const unsigned char first = (unsigned char)value.data[index++];
        uint32_t codepoint;
        size_t remaining;
        if (first < 0x80u) {
            if (first < 0x20u || first == 0x7fu)
                return 0;
            continue;
        }
        if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = first & 0x1fu;
            remaining = 1;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = first & 0x0fu;
            remaining = 2;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = first & 0x07u;
            remaining = 3;
        } else {
            return 0;
        }
        if (remaining > value.size - index)
            return 0;
        while (remaining-- > 0) {
            const unsigned char next = (unsigned char)value.data[index++];
            if (!utf8_continuation(next))
                return 0;
            codepoint = (codepoint << 6) | (uint32_t)(next & 0x3fu);
        }
        if ((codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
            (codepoint >= 0x80u && codepoint <= 0x9fu) || codepoint > 0x10ffffu ||
            codepoint == 0xfffeu || codepoint == 0xffffu) {
            return 0;
        }
        if (codepoint < 0x80u || (codepoint < 0x800u && first >= 0xe0u) ||
            (codepoint < 0x10000u && first >= 0xf0u)) {
            return 0;
        }
    }
    return 1;
}

#ifndef PXA_GUEST_FS_PATH_H
#define PXA_GUEST_FS_PATH_H

#include <stddef.h>
#include <stdint.h>

static inline int pxa_fs_equal_bytes(const void *left, const void *right,
                                     size_t length) {
    const uint8_t *left_bytes = (const uint8_t *)left;
    const uint8_t *right_bytes = (const uint8_t *)right;
    if ((left_bytes == NULL || right_bytes == NULL) && length != 0) return 0;
    for (size_t index = 0; index < length; ++index)
        if (left_bytes[index] != right_bytes[index]) return 0;
    return 1;
}

static inline int pxa_fs_contains_byte(const uint8_t *value, size_t length,
                                       uint8_t needle) {
    if (value == NULL && length != 0) return 0;
    for (size_t index = 0; index < length; ++index)
        if (value[index] == needle) return 1;
    return 0;
}

static inline int pxa_fs_valid_utf8(const char *text, size_t length) {
    size_t index = 0;
    if (text == NULL) return 0;
    while (index < length) {
        const uint8_t first = (uint8_t)text[index++];
        if (first < 0x80) continue;
        uint32_t codepoint;
        uint32_t minimum;
        size_t continuation;
        if ((first & 0xe0) == 0xc0) {
            codepoint = first & 0x1f;
            minimum = 0x80;
            continuation = 1;
        } else if ((first & 0xf0) == 0xe0) {
            codepoint = first & 0x0f;
            minimum = 0x800;
            continuation = 2;
        } else if ((first & 0xf8) == 0xf0) {
            codepoint = first & 0x07;
            minimum = 0x10000;
            continuation = 3;
        } else {
            return 0;
        }
        if (continuation > length - index) return 0;
        for (size_t count = 0; count < continuation; ++count) {
            const uint8_t next = (uint8_t)text[index++];
            if ((next & 0xc0) != 0x80) return 0;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return 0;
    }
    return 1;
}

static inline int pxa_fs_valid_path(const char *path, size_t length) {
    size_t segment_start = 0;
    if (path == NULL || length == 0 || length > 255 || path[0] == '/' ||
        path[length - 1] == '/') return 0;
    for (size_t index = 0; index <= length; ++index) {
        if (index != length && path[index] != '/') {
            const uint8_t byte = (uint8_t)path[index];
            if (byte == 0 || byte < 0x20 || byte == 0x7f || byte == '\\')
                return 0;
            continue;
        }
        const size_t segment_length = index - segment_start;
        if (segment_length == 0 || segment_length > 64 ||
            (segment_length == 1 && path[segment_start] == '.') ||
            (segment_length == 2 && path[segment_start] == '.' &&
             path[segment_start + 1] == '.') ||
            (segment_length >= 5 && path[segment_start] == '.' &&
             path[segment_start + 1] == 'p' && path[segment_start + 2] == 'x' &&
             path[segment_start + 3] == 'a' && path[segment_start + 4] == '-'))
            return 0;
        segment_start = index + 1;
    }
    return pxa_fs_valid_utf8(path, length);
}

#endif

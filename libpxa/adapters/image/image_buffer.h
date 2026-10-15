#ifndef PXA_IMAGE_BUFFER_H
#define PXA_IMAGE_BUFFER_H
#include "pxa/image_decode.h"
#include <string.h>
/* Shared by native codecs that require a seekable compressed input. Does not
 * allocate a source-sized pixel image. Every grow charges old + new storage. */
typedef struct {
    pxa_image_target_t *target;
    const pxa_memory_allocator_t *temporary;
    uint8_t *data;
    size_t size, capacity;
} pxa_image_buffer_t;
static inline pxa_status_t pxa_image_buffer_write(pxa_image_buffer_t *s, pxa_bytes_t input) {
    uint32_t limit = pxa_image_target_options(s->target)->max_encoded_bytes;
    if (input.size > limit - s->size)
        return PXA_STATUS_LIMIT_EXCEEDED;
    size_t needed = s->size + input.size;
    if (needed > s->capacity) {
        size_t capacity = s->capacity ? s->capacity * 2 : 4096;
        if (capacity < needed)
            capacity = needed;
        if (capacity > limit)
            capacity = limit;
        void *data = pxa_memory_resize(s->temporary, s->data, capacity);
        if (!data)
            return PXA_STATUS_RESOURCE_LIMIT;
        s->data = data;
        s->capacity = capacity;
    }
    memcpy(s->data + s->size, input.data, input.size);
    s->size = needed;
    return PXA_STATUS_OK;
}
/* Metadata/structure validation only, not entropy decoding. The common
 * baseline subset is accepted by ROM, LVGL and hardware implementations.
 * Check table categories before entering immutable ROM/native code. */
static inline pxa_status_t pxa_image_jpeg_info(pxa_bytes_t bytes, uint16_t *width,
                                               uint16_t *height) {
    *width = *height = 0;
    if (bytes.size < 4 || bytes.data[0] != 255 || bytes.data[1] != 216 ||
        bytes.data[bytes.size - 2] != 255 || bytes.data[bytes.size - 1] != 217)
        return PXA_STATUS_PROTOCOL_ERROR;
    size_t at = 2;
    while (at < bytes.size - 2) {
        if (bytes.data[at++] != 255)
            return PXA_STATUS_PROTOCOL_ERROR;
        while (at < bytes.size && bytes.data[at] == 255)
            ++at;
        if (at >= bytes.size)
            return PXA_STATUS_PROTOCOL_ERROR;
        uint8_t marker = bytes.data[at++];
        if (marker == 0 || marker == 216 || marker == 217 || (marker >= 208 && marker <= 215))
            return PXA_STATUS_PROTOCOL_ERROR;
        if (at + 2 > bytes.size)
            return PXA_STATUS_PROTOCOL_ERROR;
        size_t length = (size_t)bytes.data[at] * 256 + bytes.data[at + 1];
        if (length < 2 || length > bytes.size - at)
            return PXA_STATUS_PROTOCOL_ERROR;
        const uint8_t *p = bytes.data + at + 2;
        size_t n = length - 2;
        at += length;
        if (marker == 192) {
            if (*width || n < 6 || p[0] != 8)
                return PXA_STATUS_PROTOCOL_ERROR;
            *height = (uint16_t)(p[1] * 256u + p[2]);
            *width = (uint16_t)(p[3] * 256u + p[4]);
            if (!*width || !*height || *width > 4096 || *height > 4096 ||
                (uint32_t)*width * *height > 1048576)
                return PXA_STATUS_LIMIT_EXCEEDED;
            if ((p[5] != 1 && p[5] != 3) || n != 6u + 3u * p[5])
                return PXA_STATUS_UNSUPPORTED;
            for (unsigned i = 0; i < p[5]; ++i) {
                uint8_t sampling = p[7 + 3 * i];
                if (!(sampling >> 4) || !(sampling & 15) || (sampling >> 4) > 2 ||
                    (sampling & 15) > 2 || p[8 + 3 * i] > 3)
                    return PXA_STATUS_UNSUPPORTED;
            }
        } else if (marker >= 193 && marker <= 207 && marker != 196 && marker != 200 &&
                   marker != 204)
            return PXA_STATUS_UNSUPPORTED;
        else if (marker == 196) {
            size_t j = 0;
            while (j < n) {
                if (n - j < 17 || (p[j] & 0xee))
                    return PXA_STATUS_PROTOCOL_ERROR;
                int ac = p[j++] >> 4, available = 1;
                unsigned count = 0;
                for (unsigned k = 0; k < 16; ++k) {
                    available = available * 2 - p[j + k];
                    if (available < 0)
                        return PXA_STATUS_PROTOCOL_ERROR;
                    count += p[j + k];
                }
                j += 16;
                if (count > 256 || count > n - j)
                    return PXA_STATUS_PROTOCOL_ERROR;
                for (unsigned k = 0; k < count; ++k) {
                    uint8_t value = p[j + k];
                    if ((!ac && value > 11) ||
                        (ac &&
                         ((value & 15) > 10 || (!(value & 15) && value != 0 && value != 240))))
                        return PXA_STATUS_PROTOCOL_ERROR;
                }
                j += count;
            }
        } else if (marker == 218) {
            if (!*width || n < 4 || n != 1u + 2u * p[0] + 3u || (p[0] != 1 && p[0] != 3) ||
                p[n - 3] != 0 || p[n - 2] != 63 || p[n - 1] != 0)
                return PXA_STATUS_UNSUPPORTED;
            for (unsigned i = 0; i < p[0]; ++i)
                if (p[2 + 2 * i] & 0xee)
                    return PXA_STATUS_UNSUPPORTED;
            return PXA_STATUS_OK;
        }
    }
    return PXA_STATUS_PROTOCOL_ERROR;
}
#endif

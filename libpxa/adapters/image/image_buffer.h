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
#include "image_jpeg.h"
typedef struct { const uint8_t *data; size_t size; } pxa_image_bytes_cursor_t;
static inline size_t pxa_image_bytes_read(void *context, uint8_t *out, size_t n) {
    pxa_image_bytes_cursor_t *s = context;
    if (n > s->size) n = s->size;
    if (out) memcpy(out, s->data, n);
    s->data += n;
    s->size -= n;
    return n;
}
static inline pxa_status_t pxa_image_jpeg_info(pxa_bytes_t bytes, uint16_t *width,
                                               uint16_t *height) {
    *width = *height = 0;
    if (bytes.size < 4 || bytes.data[bytes.size - 2] != 255 || bytes.data[bytes.size - 1] != 217)
        return PXA_STATUS_PROTOCOL_ERROR;
    pxa_image_bytes_cursor_t cursor = {bytes.data, bytes.size};
    pxa_image_jpeg_reader_t reader = {&cursor, pxa_image_bytes_read, bytes.size - 2};
    return pxa_image_jpeg_check(&reader, width, height);
}
#endif

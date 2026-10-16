#ifndef PXA_IMAGE_CHUNKS_H
#define PXA_IMAGE_CHUNKS_H
#include "pxa/image_decode.h"
#include <string.h>

/* Seekable compressed input for pull decoders. A block is never relocated:
 * downloading N bytes copies N bytes, and growing never holds old + new input.
 * No filesystem, global allocator hooks, or source-sized pixel buffer. */
enum { PXA_IMAGE_CHUNK_BYTES = 4096 };
typedef struct pxa_image_chunk {
    struct pxa_image_chunk *next;
    size_t used;
    uint8_t data[PXA_IMAGE_CHUNK_BYTES];
} pxa_image_chunk_t;
typedef struct {
    pxa_image_target_t *target;
    const pxa_memory_allocator_t *temporary;
    pxa_image_chunk_t *first, *last;
    size_t size;
    uint8_t tail[2];
} pxa_image_chunks_t;
typedef struct {
    const pxa_image_chunk_t *block;
    size_t offset;
} pxa_image_cursor_t;
static inline pxa_status_t pxa_image_chunks_write(pxa_image_chunks_t *s, pxa_bytes_t bytes) {
    size_t limit = pxa_image_target_options(s->target)->max_encoded_bytes;
    if (bytes.size > limit - s->size) return PXA_STATUS_LIMIT_EXCEEDED;
    while (bytes.size) {
        if (!s->last || s->last->used == PXA_IMAGE_CHUNK_BYTES) {
            pxa_image_chunk_t *block = pxa_memory_allocate(s->temporary, sizeof(*block));
            if (!block) return PXA_STATUS_RESOURCE_LIMIT;
            block->next = NULL;
            block->used = 0;
            if (s->last) s->last->next = block;
            else s->first = block;
            s->last = block;
        }
        size_t n = PXA_IMAGE_CHUNK_BYTES - s->last->used;
        if (n > bytes.size) n = bytes.size;
        memcpy(s->last->data + s->last->used, bytes.data, n);
        s->tail[0] = n == 1 ? s->tail[1] : bytes.data[n - 2];
        s->tail[1] = bytes.data[n - 1];
        s->last->used += n;
        s->size += n;
        bytes.data += n;
        bytes.size -= n;
    }
    return PXA_STATUS_OK;
}
/* NULL output skips bytes. Return the actual count, including short input. */
static inline size_t pxa_image_cursor_read(void *context, uint8_t *output, size_t bytes) {
    pxa_image_cursor_t *cursor = context;
    size_t copied = 0;
    while (copied < bytes && cursor->block) {
        size_t n = cursor->block->used - cursor->offset;
        if (n > bytes - copied) n = bytes - copied;
        if (output) memcpy(output + copied, cursor->block->data + cursor->offset, n);
        copied += n;
        cursor->offset += n;
        if (cursor->offset == cursor->block->used) {
            cursor->block = cursor->block->next;
            cursor->offset = 0;
        }
    }
    return copied;
}
static inline void pxa_image_chunks_close(pxa_image_chunks_t *s) {
    while (s->first) {
        pxa_image_chunk_t *next = s->first->next;
        pxa_memory_release(s->first);
        s->first = next;
    }
    s->last = NULL;
    s->size = 0;
}
#include "image_jpeg.h"
static inline pxa_status_t pxa_image_chunks_jpeg_info(const pxa_image_chunks_t *s,
    uint16_t *width, uint16_t *height) {
    *width = *height = 0;
    if (s->size < 4 || s->tail[0] != 255 || s->tail[1] != 217)
        return PXA_STATUS_PROTOCOL_ERROR;
    pxa_image_cursor_t cursor = {s->first, 0};
    pxa_image_jpeg_reader_t reader = {&cursor, pxa_image_cursor_read, s->size - 2};
    return pxa_image_jpeg_check(&reader, width, height);
}
#endif

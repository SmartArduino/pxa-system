#include "../image/image_chunks.h"
#include "pxa/image/codecs.h"
#include <setjmp.h>
#include <stdio.h>

#include <jpeglib.h>

typedef struct {
    pxa_image_chunks_t input;
    struct jpeg_source_mgr source;
    const pxa_image_chunk_t *next;
    struct jpeg_decompress_struct decoder;
    struct jpeg_error_mgr error;
    jmp_buf failure;
    uint8_t *row;
    int created;
} jpeg_session_t;
static void jpeg_error(j_common_ptr decoder) {
    jpeg_session_t *s = (void *)((uint8_t *)decoder->err - offsetof(jpeg_session_t, error));
    longjmp(s->failure, 1);
}
static void jpeg_message(j_common_ptr decoder) { (void)decoder; }
static void source_init(j_decompress_ptr decoder) { (void)decoder; }
static void source_term(j_decompress_ptr decoder) { (void)decoder; }
static boolean source_fill(j_decompress_ptr decoder) {
    jpeg_session_t *s = decoder->client_data;
    if (!s->next) {
        /* Never fabricate an EOI for a truncated untrusted image. */
        (*decoder->err->error_exit)((j_common_ptr)decoder);
        return FALSE;
    }
    s->source.next_input_byte = s->next->data;
    s->source.bytes_in_buffer = s->next->used;
    s->next = s->next->next;
    return TRUE;
}
static void source_skip(j_decompress_ptr decoder, long count) {
    if (count <= 0) return;
    while ((size_t)count > decoder->src->bytes_in_buffer) {
        count -= (long)decoder->src->bytes_in_buffer;
        source_fill(decoder);
    }
    decoder->src->next_input_byte += count;
    decoder->src->bytes_in_buffer -= (size_t)count;
}
static pxa_status_t open_jpeg(pxa_bytes_t signature, pxa_image_target_t *target,
                              const pxa_memory_allocator_t *temporary, void **session) {
    *session = NULL;
    if (signature.size < 3 || signature.data[0] != 255 || signature.data[1] != 216 ||
        signature.data[2] != 255)
        return PXA_STATUS_UNSUPPORTED;
    jpeg_session_t *s = pxa_memory_allocate(temporary, sizeof(*s));
    if (!s)
        return PXA_STATUS_RESOURCE_LIMIT;
    memset(s, 0, sizeof(*s));
    s->input.target = target;
    s->input.temporary = temporary;
    *session = s;
    return PXA_STATUS_OK;
}
static pxa_status_t write_jpeg(void *session, pxa_bytes_t bytes) {
    return pxa_image_chunks_write(&((jpeg_session_t *)session)->input, bytes);
}
static pxa_status_t finish_jpeg(void *session) {
    jpeg_session_t *s = session;
    uint16_t w, h;
    pxa_status_t status = pxa_image_chunks_jpeg_info(&s->input, &w, &h);
    if (status)
        return status;
    s->decoder.err = jpeg_std_error(&s->error);
    s->error.error_exit = jpeg_error;
    s->error.output_message = jpeg_message;
    s->decoder.client_data = s;
    if (setjmp(s->failure))
        return PXA_STATUS_PROTOCOL_ERROR;
    s->created = 1;
    jpeg_create_decompress(&s->decoder);
    s->decoder.client_data = s;
    s->next = s->input.first;
    s->source = (struct jpeg_source_mgr){.init_source = source_init,
        .fill_input_buffer = source_fill, .skip_input_data = source_skip,
        .resync_to_restart = jpeg_resync_to_restart, .term_source = source_term};
    s->decoder.src = &s->source;
    if (jpeg_read_header(&s->decoder, TRUE) != JPEG_HEADER_OK)
        return PXA_STATUS_PROTOCOL_ERROR;
    if (s->decoder.image_width != w || s->decoder.image_height != h)
        return PXA_STATUS_PROTOCOL_ERROR;
    status = pxa_image_target_begin(s->input.target, w, h, 0);
    if (status)
        return status;
    uint16_t out_w, out_h;
    pxa_image_target_dimensions(s->input.target, &out_w, &out_h);
    unsigned factor = 1;
    while (factor < 8 && w / (factor * 2) >= out_w && h / (factor * 2) >= out_h)
        factor *= 2;
    s->decoder.scale_num = 1;
    s->decoder.scale_denom = factor;
    s->decoder.out_color_space = JCS_RGB;
    if (!jpeg_start_decompress(&s->decoder) || s->decoder.output_components != 3)
        return PXA_STATUS_PROTOCOL_ERROR;
    size_t stride = (size_t)s->decoder.output_width * 3;
    s->row = pxa_memory_allocate(s->input.temporary, stride);
    if (!s->row)
        return PXA_STATUS_RESOURCE_LIMIT;
    while (s->decoder.output_scanline < s->decoder.output_height) {
        uint32_t y = s->decoder.output_scanline;
        JSAMPROW row = s->row;
        if (jpeg_read_scanlines(&s->decoder, &row, 1) != 1)
            return PXA_STATUS_PROTOCOL_ERROR;
        pxa_image_tile_t tile = {0,
                                 y * factor,
                                 w,
                                 factor,
                                 s->decoder.output_width,
                                 1,
                                 (uint32_t)stride,
                                 PXA_IMAGE_RGB888,
                                 s->row};
        status = pxa_image_target_blit(s->input.target, &tile);
        if (status)
            return status;
    }
    if (!jpeg_finish_decompress(&s->decoder) || s->error.num_warnings)
        return PXA_STATUS_PROTOCOL_ERROR;
    return PXA_STATUS_OK;
}
static void close_jpeg(void *session) {
    jpeg_session_t *s = session;
    if (s->created)
        jpeg_destroy_decompress(&s->decoder);
    pxa_memory_release(s->row);
    pxa_image_chunks_close(&s->input);
    pxa_memory_release(s);
}
const pxa_image_codec_t pxa_posix_jpeg_codec = {.open = open_jpeg, .write = write_jpeg, .finish = finish_jpeg, .close = close_jpeg};
const pxa_image_backend_t *pxa_posix_image_backend(void) {
    static const pxa_image_codec_t *const codecs[] = {&pxa_stream_png_codec, &pxa_posix_jpeg_codec};
    static const pxa_image_backend_t backend = {codecs, sizeof(codecs) / sizeof(codecs[0])};
    return &backend;
}

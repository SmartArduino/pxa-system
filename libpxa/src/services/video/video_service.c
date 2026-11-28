// SPDX-License-Identifier: Apache-2.0
#include "pxa/video.h"
#include "pxa/surface.h"
#include <stdbool.h>
#include <string.h>
#include "video_scale.h"
#ifdef PXA_VIDEO_COPY_PIXELS
/* Platform bulk copy must finish before returning to the Guest. */
extern void PXA_VIDEO_COPY_PIXELS(void *out, const void *in, size_t bytes);
#else
#define PXA_VIDEO_COPY_PIXELS memcpy
#endif
#ifdef PXA_VIDEO_COPY_ROWS
extern bool PXA_VIDEO_COPY_ROWS(uint8_t *out, uint32_t stride, const uint8_t *in,
                               uint32_t width, uint32_t height);
#endif
struct pxa_video_service {
    pxa_runtime_t *runtime;
    pxa_video_backend_t backend;
    pxa_component_t foreground, owner;
    pxa_handle64_t handle;
    pxa_handle64_t surface;
    pxa_surface_desc_t output;
    pxa_surface_damage_rect_t region;
    uint32_t initialized_buffers;
    void *decoder;
    pxa_video_frame_t frame;
    pxa_status_t decode_status;
    bool decode_pending, decode_ready, decode_notified;
    size_t read_offset;
    uint16_t index, width, height, scaled_width, scaled_height;
    uint32_t scaled_stride_bytes;
};
size_t pxa_video_service_workspace_size(void) { return sizeof(pxa_video_service_t); }
pxa_status_t pxa_video_service_init(void *workspace, size_t size, pxa_runtime_t *runtime,
    const pxa_video_backend_t *backend, pxa_video_service_t **out) {
    if (!out) return PXA_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (!workspace || size < sizeof(pxa_video_service_t) || !runtime ||
        (uintptr_t)workspace % sizeof(uint64_t)) return PXA_STATUS_INVALID_ARGUMENT;
    const size_t backend_size = backend ? pxa_video_backend_copy_size(backend->struct_size) : 0;
    const bool async_fields = backend_size >= PXA_VIDEO_BACKEND_ASYNC_SIZE;
    const bool deferred = backend_size >= PXA_VIDEO_BACKEND_DEFERRED_SIZE;
    if (backend && (!backend_size ||
        (deferred && !!backend->render_frame != !!backend->materialize_frame) ||
        (async_fields && !!backend->queue_decode != !!backend->poll_decode) ||
        (backend->count && (!backend->capabilities || !backend->open || !backend->decode ||
                           !backend->reset || !backend->close)))) return PXA_STATUS_INVALID_ARGUMENT;
    if (backend) for (unsigned i = 0; i < backend->count; ++i) {
        const pxa_video_capability_t *c = backend->capabilities + i;
        if (!c->codec || !c->implementation || (c->audio_codecs && !backend->audio_io) || !c->max_width || !c->max_height ||
            !c->max_pixels || !c->max_packet_bytes || c->max_packet_bytes > INT32_MAX - 16u ||
            !c->recommended_width || !c->recommended_height ||
            c->recommended_width > c->max_width || c->recommended_height > c->max_height ||
            (uint32_t)c->recommended_width * c->recommended_height > c->max_pixels)
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    pxa_video_service_t *s = workspace;
    memset(s, 0, sizeof(*s)); s->runtime = runtime;
    if (backend) {
        memcpy(&s->backend, backend, backend_size);
        s->backend.struct_size = (uint32_t)backend_size;
    }
    *out = s; return PXA_STATUS_OK;
}
static void close_decoder(void *context) {
    pxa_video_service_t *s = context;
    if (s->surface) pxa_surface_unbind_producer(s->runtime, s->owner, s->surface, s->handle);
    s->surface = 0;
    if (s->decoder) s->backend.close(s->decoder);
    s->decoder = NULL; s->handle = 0; s->owner = 0;
    s->decode_pending = s->decode_ready = s->decode_notified = false;
    s->scaled_width = s->scaled_height = 0;
    s->scaled_stride_bytes = 0;
    memset(&s->frame, 0, sizeof(s->frame)); s->read_offset = 0;
}
static pxa_status_t check_frame(pxa_video_service_t *s, pxa_status_t status) {
    if (!pxa_status_is_known(status)) status = PXA_STATUS_IO_ERROR;
    if (!status && s->frame.size && !s->frame.rgb565 && !s->backend.render_frame)
        status = PXA_STATUS_PROTOCOL_ERROR;
    if (status == PXA_STATUS_OK && (s->frame.rgb565 || s->frame.size) &&
        (!s->frame.width || !s->frame.height || s->frame.width > s->width ||
         s->frame.height > s->height || s->frame.stride_bytes != (uint32_t)s->frame.width * 2 ||
         s->frame.stride_bytes > UINT32_MAX / s->frame.height ||
         s->frame.size != (size_t)s->frame.stride_bytes * s->frame.height))
        status = PXA_STATUS_PROTOCOL_ERROR;
    if (status) memset(&s->frame, 0, sizeof(s->frame));
    return status;
}

static void collect_decode(pxa_video_service_t *s) {
    if (!s->decode_pending || s->decode_ready) return;
    const pxa_status_t status = s->backend.poll_decode(s->decoder, &s->frame);
    if (status == PXA_STATUS_WOULD_BLOCK) return;
    s->decode_status = check_frame(s, status);
    s->decode_ready = true;
}

void pxa_video_service_dispatch(pxa_video_service_t *s) {
    if (!s || !s->decode_pending || s->owner != s->foreground) return;
    collect_decode(s);
    if (s->decode_ready && !s->decode_notified) {
        uint8_t payload[8];
        pxa_write_u64(payload, s->handle);
        const pxa_bytes_t bytes = {payload, sizeof(payload)};
        const uint64_t key = (uint64_t)PXA_VIDEO_SERVICE_ID << 32 | PXA_VIDEO_DECODE_READY;
        s->decode_notified = pxa_event_post_message(s->runtime, s->owner,
            PXA_VIDEO_SERVICE_ID, PXA_VIDEO_DECODE_READY, 0, bytes, 0, key) == PXA_STATUS_OK;
    }
}

static void scale_frame(const pxa_video_service_t *s, uint8_t *out,
                        uint32_t w, uint32_t h, uint32_t stride) {
    if (!s->frame.rgb565) {
        s->backend.render_frame(s->decoder, out, w, h, stride);
        return;
    }
#ifdef PXA_VIDEO_COPY_ROWS
    if (w == s->frame.width && h == s->frame.height &&
        PXA_VIDEO_COPY_ROWS(out, stride, s->frame.rgb565, w, h))
        return;
#endif
    if (stride == w*2 && w == s->frame.width && h == s->frame.height) {
        PXA_VIDEO_COPY_PIXELS(out, s->frame.rgb565, (size_t)stride*h);
        return;
    }
    /* Exact floor(x*source/destination) sampling without a division per
     * pixel, intermediate allocations, or a full-size Guest frame copy. */
    const uint32_t dx = s->frame.width/w, rx = s->frame.width%w;
    const uint32_t dy = s->frame.height/h, ry = s->frame.height%h;
#if defined(__GNUC__) || defined(__clang__)
    const bool aligned = !(((uintptr_t)out | (uintptr_t)s->frame.rgb565) & 1u);
    const bool three_quarters = aligned && w*4 == s->frame.width*3;
    const bool four_thirds = aligned && w*3 == s->frame.width*4;
    pxa_scale_copy_span spans[PXA_SCALE_MAX_DELTA + 1];
    const unsigned span_count = pxa_scale_copy_spans(s->frame.width, w, spans);
    pxa_scale_run runs[PXA_SCALE_MAX_RUNS];
    const unsigned run_count = aligned && !four_thirds && h >= 16
        ? pxa_scale_four_thirds_runs(s->frame.width, w, runs) : 0;
#endif
    uint32_t sy = 0, ey = 0, previous_sy = UINT32_MAX;
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t *row = s->frame.rgb565+(size_t)sy*s->frame.stride_bytes;
        uint8_t *pixel = out+(size_t)y*stride;
        uint32_t sx = 0, ex = 0;
        if (sy == previous_sy && w != s->frame.width) {
            /* Vertical expansion can reuse the horizontally sampled row.
             * Leave all caller-owned stride padding untouched. */
            memcpy(pixel, pixel-stride, w*2);
        } else if (w == s->frame.width) {
            memcpy(pixel,row,w*2);
#if defined(__GNUC__) || defined(__clang__)
        } else if (span_count) {
            pxa_scale_copy_row(pixel, row, spans, span_count);
        } else if (aligned && s->frame.width == w*2) {
            for (uint32_t x = 0; x < w; ++x)
                memcpy(__builtin_assume_aligned(pixel+x*2,2),
                       __builtin_assume_aligned(row+x*4,2),2);
        } else if (three_quarters) {
            // Floor sampling takes the first three pixels of each group of four.
            for (uint32_t x = 0; x < w; x += 3, row += 8, pixel += 6) {
                const uint8_t *source = __builtin_assume_aligned(row,2);
                uint8_t *destination = __builtin_assume_aligned(pixel,2);
                memcpy(destination,source,2);
                memcpy(destination+2,source+2,2);
                memcpy(destination+4,source+4,2);
            }
        } else if (four_thirds) {
            pxa_scale_expand_groups(pixel, row, w / 4);
        } else if (run_count) {
            pxa_scale_run_row(pixel, row, s->frame.width, w, runs, run_count);
#endif
        } else {
#if defined(__GNUC__) || defined(__clang__)
            if (!(((uintptr_t)pixel|(uintptr_t)row)&1u)) {
                for (uint32_t x = 0; x < w; ++x) {
                    memcpy(__builtin_assume_aligned(pixel,2),
                        __builtin_assume_aligned(row+sx*2,2),2);
                    pixel += 2; sx += dx; ex += rx;
                    if (ex >= w) { ex -= w; ++sx; }
                }
            } else
#endif
            {
                for (uint32_t x = 0; x < w; ++x) {
                    *pixel++ = row[sx*2]; *pixel++ = row[sx*2+1];
                    sx += dx; ex += rx;
                    if (ex >= w) { ex -= w; ++sx; }
                }
            }
        }
        previous_sy = sy;
        sy += dy; ey += ry;
        if (ey >= h) { ey -= h; ++sy; }
    }
}

static void initialize_surface_border(pxa_video_service_t *s, uint8_t *pixels, uint32_t stride, uint8_t index) {
    const pxa_surface_damage_rect_t r = s->region;
    const uint32_t bit = index < 32 ? UINT32_C(1) << index : 0;
    if (!(s->initialized_buffers & bit) &&
        (r.width != s->output.width || r.height != s->output.height)) {
        const size_t left = (size_t)r.x * 2;
        const size_t right = left + (size_t)r.width * 2;
        uint8_t *row = pixels + (size_t)r.y * stride;
        memset(pixels, 0, (size_t)r.y * stride);
        if (left || right < stride) {
            for (uint16_t y = 0; y < r.height; ++y, row += stride) {
                memset(row, 0, left);
                memset(row + right, 0, stride - right);
            }
        } else {
            row += (size_t)r.height * stride;
        }
        memset(row, 0, (size_t)(s->output.height - r.y - r.height) * stride);
        s->initialized_buffers |= bit;
    }
}

static void fill_surface(void *context, uint8_t *pixels, uint32_t stride, uint8_t index) {
    pxa_video_service_t *s = context;
    const pxa_surface_damage_rect_t r = s->region;
    initialize_surface_border(s, pixels, stride, index);
    if (s->backend.render_surface && s->backend.render_surface(s->decoder, pixels, stride,
            s->output.height, r.x, r.y, r.width, r.height)) return;
    scale_frame(s, pixels + (size_t)r.y * stride + r.x*2, r.width, r.height, stride);
}

struct next_packet {
    pxa_video_service_t *service;
    const uint8_t *data;
    size_t size;
    pxa_status_t status;
};
static void fill_and_queue(void *context, uint8_t *pixels, uint32_t stride, uint8_t index) {
    struct next_packet *next = context;
    pxa_video_service_t *s = next->service;
    const pxa_surface_damage_rect_t r = s->region;
    initialize_surface_border(s, pixels, stride, index);
    next->status = s->backend.prepare_and_queue(s->decoder, next->data+16, next->size-16,
        pxa_read_u64(next->data), pxa_read_u32(next->data+8), pixels, stride, s->output.height,
        r.x, r.y, r.width, r.height);
}

static pxa_status_t bind_surface(pxa_video_service_t *s, const uint8_t *data, size_t size) {
    if (size != 16) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_handle64_t target = pxa_read_u64(data);
    pxa_surface_damage_rect_t r = {pxa_read_u16(data+8), pxa_read_u16(data+10),
                                   pxa_read_u16(data+12), pxa_read_u16(data+14)};
    pxa_surface_desc_t output = {0};
    if (target) {
        pxa_status_t status = pxa_surface_bind_producer(s->runtime, s->owner, target, s->handle, &output);
        if (status) return status;
        if (!r.width || !r.height || (uint32_t)r.x+r.width > output.width ||
            (uint32_t)r.y+r.height > output.height) {
            if (target != s->surface)
                pxa_surface_unbind_producer(s->runtime, s->owner, target, s->handle);
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    } else if (r.x || r.y || r.width || r.height) return PXA_STATUS_INVALID_ARGUMENT;
    if (s->surface && target == s->surface)
        (void)pxa_surface_stage_producer(s->runtime, s->owner, target, s->handle, NULL, NULL);
    else if (s->surface)
        pxa_surface_unbind_producer(s->runtime, s->owner, s->surface, s->handle);
    s->surface = target; s->region = r; s->output = output;
    s->initialized_buffers = 0;
    if (s->backend.set_surface_size)
        s->backend.set_surface_size(s->decoder, r.width, r.height);
    return PXA_STATUS_OK;
}

static void write_frame_info(const pxa_video_frame_t *f, uint8_t *data) {
    pxa_write_u16(data, f->width); pxa_write_u16(data+2, f->height);
    pxa_write_u32(data+4, f->stride_bytes); pxa_write_u64(data+8, f->pts_us);
    pxa_write_u64(data+16, f->decode_us); pxa_write_u32(data+24, (uint32_t)f->size);
    pxa_write_u32(data+28, 0);
}

static int32_t decoder_io(void *context, uint32_t operation, uint8_t *data, size_t size) {
    pxa_video_service_t *s = context;
    if (!s->decoder || !s->foreground || s->foreground != s->owner) return PXA_STATUS_DENIED;
    if (size && !data) return PXA_STATUS_INVALID_ARGUMENT;
    if (operation == PXA_VIDEO_IO_PREFERRED_SIZE) {
        if (size != 4) return PXA_STATUS_INVALID_ARGUMENT;
        const uint16_t max_w = pxa_read_u16(data), max_h = pxa_read_u16(data+2);
        uint16_t w = max_w, h = max_h;
        if (!w || !h) return PXA_STATUS_INVALID_ARGUMENT;
        if (!s->backend.preferred_size || !s->backend.preferred_size(s->decoder, &w, &h))
            return PXA_STATUS_UNSUPPORTED;
        if (!w || !h || w > max_w || h > max_h) return PXA_STATUS_IO_ERROR;
        pxa_write_u16(data, w); pxa_write_u16(data+2, h);
        return 4;
    }
    if (operation == PXA_VIDEO_IO_BIND_SURFACE) return bind_surface(s, data, size);
    if (operation == PXA_VIDEO_IO_PREPARE_SURFACE) {
        if (size) return PXA_STATUS_INVALID_ARGUMENT;
        if (!s->surface) return PXA_STATUS_BAD_STATE;
        if (s->decode_pending || !s->frame.size) return PXA_STATUS_WOULD_BLOCK;
        pxa_surface_region_fill_t region = {fill_surface, s, s->region, NULL};
        return pxa_surface_stage_producer(s->runtime, s->owner, s->surface, s->handle,
                                          pxa_surface_fill_region, &region);
    }
    if (operation == PXA_VIDEO_IO_PRESENT_SURFACE || operation == PXA_VIDEO_IO_PRESENT_SURFACE_ORDERED) {
        if (size != 8) return PXA_STATUS_INVALID_ARGUMENT;
        if (!s->surface) return PXA_STATUS_BAD_STATE;
        const uint64_t frame_id = pxa_read_u64(data);
        if (operation == PXA_VIDEO_IO_PRESENT_SURFACE_ORDERED)
            return pxa_surface_present_producer_ordered(s->runtime, s->owner, s->surface, s->handle, frame_id);
        return pxa_surface_present_producer(s->runtime, s->owner, s->surface, s->handle, frame_id);
    }
    if (operation == PXA_VIDEO_IO_POLL_DECODE || operation == PXA_VIDEO_IO_POLL_FRAME) {
        const bool with_frame = operation == PXA_VIDEO_IO_POLL_FRAME;
        if (size != (with_frame ? PXA_VIDEO_FRAME_BYTES : 0u)) return PXA_STATUS_INVALID_ARGUMENT;
        if (!s->backend.poll_decode) return PXA_STATUS_UNSUPPORTED;
        if (!s->decode_pending) return PXA_STATUS_NOT_FOUND;
        collect_decode(s);
        if (!s->decode_ready) return PXA_STATUS_WOULD_BLOCK;
        s->decode_pending = s->decode_ready = s->decode_notified = false;
        if (with_frame && !s->decode_status && s->frame.size) {
            write_frame_info(&s->frame, data);
            return PXA_VIDEO_FRAME_BYTES;
        }
        return s->decode_status;
    }
    if (operation == PXA_VIDEO_IO_DECODE || operation == PXA_VIDEO_IO_QUEUE_DECODE ||
        operation == PXA_VIDEO_IO_PREPARE_AND_QUEUE) {
        if (s->decode_pending) return PXA_STATUS_BUSY;
        if (operation != PXA_VIDEO_IO_DECODE && !s->backend.queue_decode)
            return PXA_STATUS_UNSUPPORTED;
        if (size < 16 || pxa_read_u32(data+12) || (pxa_read_u32(data+8) & ~PXA_VIDEO_PACKET_DRAIN))
            return PXA_STATUS_INVALID_ARGUMENT;
        if (size - 16 > s->backend.capabilities[s->index].max_packet_bytes)
            return PXA_STATUS_LIMIT_EXCEEDED;
        const uint32_t flags = pxa_read_u32(data+8);
        if ((!flags && size == 16) || (flags && size != 16)) return PXA_STATUS_INVALID_ARGUMENT;
        if (operation != PXA_VIDEO_IO_DECODE) {
            pxa_status_t status;
            if (operation == PXA_VIDEO_IO_PREPARE_AND_QUEUE) {
                if (!s->backend.prepare_and_queue) return PXA_STATUS_UNSUPPORTED;
                if (!s->surface || !s->frame.size) return PXA_STATUS_BAD_STATE;
                struct next_packet next = {s, data, size, PXA_STATUS_BAD_STATE};
                pxa_surface_region_fill_t region = {fill_and_queue, &next, s->region, &next.status};
                status = pxa_surface_stage_producer(s->runtime, s->owner, s->surface, s->handle,
                    pxa_surface_fill_region, &region);
                if (status) {
                    // A provider may close concurrently while the fill is in flight.
                    if (next.status == PXA_STATUS_OK) {
                        s->backend.reset(s->decoder);
                        memset(&s->frame, 0, sizeof(s->frame));
                        s->read_offset = 0;
                        return PXA_STATUS_BAD_STATE;
                    }
                    return status;
                }
                status = next.status;
                if (status)
                    (void)pxa_surface_stage_producer(s->runtime,s->owner,s->surface,s->handle,NULL,NULL);
            } else {
                status = s->backend.queue_decode(s->decoder, data+16, size-16, pxa_read_u64(data), flags);
            }
            if (status) return pxa_status_is_known(status) ? status : PXA_STATUS_IO_ERROR;
            memset(&s->frame, 0, sizeof(s->frame)); s->read_offset = 0;
            s->decode_pending = true;
            return PXA_STATUS_OK;
        }
        memset(&s->frame, 0, sizeof(s->frame)); s->read_offset = 0;
        return check_frame(s, s->backend.decode(s->decoder, data+16, size-16,
            pxa_read_u64(data), flags, &s->frame));
    }
    if (operation == PXA_VIDEO_IO_RESET) {
        if (size) return PXA_STATUS_INVALID_ARGUMENT;
        if (s->surface)
            (void)pxa_surface_stage_producer(s->runtime, s->owner, s->surface, s->handle, NULL, NULL);
        s->decode_pending = s->decode_ready = s->decode_notified = false;
        memset(&s->frame, 0, sizeof(s->frame)); s->read_offset = 0;
        return s->backend.reset(s->decoder);
    }
    if (operation == PXA_VIDEO_IO_FRAME_INFO) {
        if (size != PXA_VIDEO_FRAME_BYTES) return PXA_STATUS_INVALID_ARGUMENT;
        if (s->decode_pending || !s->frame.size) return PXA_STATUS_WOULD_BLOCK;
        write_frame_info(&s->frame, data);
        return PXA_STATUS_OK;
    }
    if (operation == PXA_VIDEO_IO_CONFIGURE_SCALED) {
        if (size != 4 && size != 8) return PXA_STATUS_INVALID_ARGUMENT;
        const uint32_t w = pxa_read_u16(data), h = pxa_read_u16(data+2);
        if (!w || !h || w > 4096 || h > 4096 || (uint64_t)w*h > 4u*1024u*1024u)
            return PXA_STATUS_LIMIT_EXCEEDED;
        const uint32_t stride = size == 8 ? pxa_read_u32(data+4) : w*2;
        if (stride < w*2 || (stride & 1u) ||
            (uint64_t)stride*(h-1)+w*2 > 8u*1024u*1024u)
            return PXA_STATUS_INVALID_ARGUMENT;
        s->scaled_width = (uint16_t)w; s->scaled_height = (uint16_t)h;
        s->scaled_stride_bytes = stride;
        return PXA_STATUS_OK;
    }
    if (operation == PXA_VIDEO_IO_READ_SCALED || operation == PXA_VIDEO_IO_READ_SCALED_PIXELS) {
        const size_t prefix = operation == PXA_VIDEO_IO_READ_SCALED ? 8 : 0;
        if (prefix && (size < prefix || pxa_read_u32(data+4))) return PXA_STATUS_INVALID_ARGUMENT;
        if (!prefix && !s->scaled_width) return PXA_STATUS_BAD_STATE;
        if (s->decode_pending || !s->frame.size) return PXA_STATUS_WOULD_BLOCK;
        const uint32_t w = prefix ? pxa_read_u16(data) : s->scaled_width;
        const uint32_t h = prefix ? pxa_read_u16(data+2) : s->scaled_height;
        if (!w || !h || w > 4096 || h > 4096 || (uint64_t)w*h > 4u*1024u*1024u)
            return PXA_STATUS_LIMIT_EXCEEDED;
        const uint32_t stride = prefix ? w*2 : s->scaled_stride_bytes;
        const uint64_t bytes = (uint64_t)stride*(h-1)+w*2;
        if (bytes > SIZE_MAX-prefix || size != bytes+prefix) return PXA_STATUS_INVALID_ARGUMENT;
        uint8_t *out = data+prefix;
        scale_frame(s, out, w, h, stride);
        return PXA_STATUS_OK;
    }
    if (operation == PXA_IO_READ) {
        if (s->decode_pending || !s->frame.size) return PXA_STATUS_WOULD_BLOCK;
        if (!s->frame.rgb565 && size) {
            pxa_status_t status = s->backend.materialize_frame(s->decoder, &s->frame);
            if (status) return pxa_status_is_known(status) ? status : PXA_STATUS_IO_ERROR;
            if (!s->frame.rgb565) return PXA_STATUS_PROTOCOL_ERROR;
        }
        size_t count = s->frame.size - s->read_offset;
        if (count > size) count = size;
        if (count > INT32_MAX) count = INT32_MAX;
        if (count) PXA_VIDEO_COPY_PIXELS(data, s->frame.rgb565 + s->read_offset, count);
        s->read_offset += count; return (int32_t)count;
    }
    if (operation >= PXA_VIDEO_IO_AUDIO_DECODE && operation <= PXA_VIDEO_IO_AUDIO_READ && s->backend.audio_io)
        return s->backend.audio_io(s->decoder,operation,data,size);
    return PXA_STATUS_UNSUPPORTED;
}
static const pxa_resource_ops_t io_ops = {sizeof(io_ops), decoder_io};
static pxa_status_t control(void *context, pxa_runtime_t *runtime, pxa_component_t component,
    const pxa_message_view_t *m) {
    pxa_video_service_t *s = context;
    uint8_t result[PXA_VIDEO_CAP_BYTES] = {0}; size_t bytes = 0;
    uint16_t profile;
    if (!m->request_token) return PXA_STATUS_INVALID_ARGUMENT;
    if (m->opcode != PXA_VIDEO_CAPABILITIES && m->opcode != PXA_VIDEO_OPEN) return PXA_STATUS_UNSUPPORTED;
    if (pxa_component_wire_profile(runtime, component, &profile) || profile != 1) return PXA_STATUS_UNSUPPORTED;
    pxa_status_t status = pxa_request_begin_reserved(runtime, component, m->request_token,
        PXA_VIDEO_SERVICE_ID, m->opcode, 0, sizeof(result));
    if (status) return status;
    status = pxa_request_commit(runtime, component, m->request_token);
    if (status) { (void)pxa_request_cancel(runtime, component, m->request_token); return status; }
    if (m->opcode == PXA_VIDEO_CAPABILITIES) {
        if (m->payload.size != 2) status = PXA_STATUS_INVALID_ARGUMENT;
        else {
            unsigned index = pxa_read_u16(m->payload.data);
            pxa_write_u16(result, s->backend.count); pxa_write_u16(result+2, (uint16_t)index);
            if (index < s->backend.count) {
                const pxa_video_capability_t *c = s->backend.capabilities + index;
                pxa_write_u16(result+4, c->codec); pxa_write_u16(result+6, c->implementation);
                pxa_write_u32(result+8, c->profiles);
                pxa_write_u16(result+12, c->recommended_width); pxa_write_u16(result+14, c->recommended_height);
                pxa_write_u16(result+16, c->max_width); pxa_write_u16(result+18, c->max_height);
                pxa_write_u32(result+20, c->max_pixels); pxa_write_u32(result+24, c->max_packet_bytes);
                pxa_write_u16(result+28, c->recommended_fps); pxa_write_u16(result+30, c->max_level);
                pxa_write_u32(result+32, c->implementation_id);
                pxa_write_u16(result+38, c->audio_codecs);
                pxa_write_u16(result+36, 1); /* tightly packed little-endian RGB565 */
            } else if (index || s->backend.count) status = PXA_STATUS_NOT_FOUND;
            bytes = sizeof(result);
        }
    } else if (m->payload.size != 8 || pxa_read_u16(m->payload.data+6)) status = PXA_STATUS_INVALID_ARGUMENT;
    else if (s->foreground != component) status = PXA_STATUS_DENIED;
    else if (s->handle) status = PXA_STATUS_BUSY;
    else {
        unsigned index = pxa_read_u16(m->payload.data);
        uint16_t w = pxa_read_u16(m->payload.data+2), h = pxa_read_u16(m->payload.data+4);
        if (index >= s->backend.count) status = PXA_STATUS_UNSUPPORTED;
        else {
            const pxa_video_capability_t *c = s->backend.capabilities + index;
            if (!w || !h || w > c->max_width || h > c->max_height || (uint32_t)w*h > c->max_pixels)
                status = PXA_STATUS_LIMIT_EXCEEDED;
            else {
                status = s->backend.open(s->backend.context, (uint16_t)index, w, h, &s->decoder);
                if (!pxa_status_is_known(status)) status = PXA_STATUS_IO_ERROR;
                if (!status && !s->decoder) status = PXA_STATUS_IO_ERROR;
                if (!status) {
                    pxa_resource_t resource = {s, &io_ops, close_decoder};
                    status = pxa_handle_open64(runtime, component, PXA_RESOURCE_VIDEO_DECODER, 0, &resource, &s->handle);
                    if (!status) { s->owner = component; s->index = (uint16_t)index; s->width = w; s->height = h;
                        pxa_write_u64(result, s->handle); bytes = 8; }
                }
                if (status) close_decoder(s);
            }
        }
    }
    pxa_status_t completion = pxa_request_complete(runtime, component, m->request_token, status,
        status ? NULL : result, status ? 0 : bytes);
    if (completion) {
        if (!status && m->opcode == PXA_VIDEO_OPEN) (void)pxa_handle_close64(runtime, component, s->handle);
        (void)pxa_request_cancel(runtime, component, m->request_token);
    }
    return PXA_STATUS_OK;
}
pxa_status_t pxa_video_service_register(pxa_video_service_t *s) {
    if (!s) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_service_ops_t ops = {0}; ops.struct_size = sizeof(ops);
    ops.service_id = PXA_VIDEO_SERVICE_ID; ops.minor = PXA_VIDEO_SERVICE_MINOR;
    ops.context = s; ops.control = control; return pxa_service_register(s->runtime, &ops);
}
void pxa_video_service_set_foreground(pxa_video_service_t *s, pxa_component_t component) {
    if (!s) return;
    if (s->handle && s->owner != component) (void)pxa_handle_close64(s->runtime, s->owner, s->handle);
    s->foreground = component;
}
void pxa_video_service_deinit(pxa_video_service_t *s) {
    if (s && s->handle) (void)pxa_handle_close64(s->runtime, s->owner, s->handle);
}

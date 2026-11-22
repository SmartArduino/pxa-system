// SPDX-License-Identifier: Apache-2.0
#include "pxa/video.h"
#include <string.h>
struct pxa_video_service {
    pxa_runtime_t *runtime;
    pxa_video_backend_t backend;
    pxa_component_t foreground, owner;
    pxa_handle64_t handle;
    void *decoder;
    pxa_video_frame_t frame;
    size_t read_offset;
    uint16_t index, width, height, scaled_width, scaled_height;
};
size_t pxa_video_service_workspace_size(void) { return sizeof(pxa_video_service_t); }
pxa_status_t pxa_video_service_init(void *workspace, size_t size, pxa_runtime_t *runtime,
    const pxa_video_backend_t *backend, pxa_video_service_t **out) {
    if (!out) return PXA_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (!workspace || size < sizeof(pxa_video_service_t) || !runtime ||
        (uintptr_t)workspace % sizeof(uint64_t)) return PXA_STATUS_INVALID_ARGUMENT;
    if (backend && (backend->struct_size < sizeof(*backend) ||
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
    if (backend) s->backend = *backend;
    *out = s; return PXA_STATUS_OK;
}
static void close_decoder(void *context) {
    pxa_video_service_t *s = context;
    if (s->decoder) s->backend.close(s->decoder);
    s->decoder = NULL; s->handle = 0; s->owner = 0;
    s->scaled_width = s->scaled_height = 0;
    memset(&s->frame, 0, sizeof(s->frame)); s->read_offset = 0;
}
static int32_t decoder_io(void *context, uint32_t operation, uint8_t *data, size_t size) {
    pxa_video_service_t *s = context;
    if (!s->decoder || !s->foreground || s->foreground != s->owner) return PXA_STATUS_DENIED;
    if (size && !data) return PXA_STATUS_INVALID_ARGUMENT;
    if (operation == PXA_VIDEO_IO_DECODE) {
        if (size < 16 || pxa_read_u32(data+12) || (pxa_read_u32(data+8) & ~PXA_VIDEO_PACKET_DRAIN))
            return PXA_STATUS_INVALID_ARGUMENT;
        if (size - 16 > s->backend.capabilities[s->index].max_packet_bytes)
            return PXA_STATUS_LIMIT_EXCEEDED;
        const uint32_t flags = pxa_read_u32(data+8);
        if ((!flags && size == 16) || (flags && size != 16)) return PXA_STATUS_INVALID_ARGUMENT;
        memset(&s->frame, 0, sizeof(s->frame)); s->read_offset = 0;
        pxa_status_t status = s->backend.decode(s->decoder, data+16, size-16,
            pxa_read_u64(data), flags, &s->frame);
        if (!pxa_status_is_known(status)) status = PXA_STATUS_IO_ERROR;
        if (status == PXA_STATUS_OK && s->frame.rgb565 &&
            (!s->frame.width || !s->frame.height || s->frame.width > s->width ||
             s->frame.height > s->height || s->frame.stride_bytes != (uint32_t)s->frame.width * 2 ||
             s->frame.stride_bytes > UINT32_MAX / s->frame.height ||
             s->frame.size != (size_t)s->frame.stride_bytes * s->frame.height))
            status = PXA_STATUS_PROTOCOL_ERROR;
        if (status) memset(&s->frame, 0, sizeof(s->frame));
        return status;
    }
    if (operation == PXA_VIDEO_IO_RESET) {
        if (size) return PXA_STATUS_INVALID_ARGUMENT;
        memset(&s->frame, 0, sizeof(s->frame)); s->read_offset = 0;
        return s->backend.reset(s->decoder);
    }
    if (operation == PXA_VIDEO_IO_FRAME_INFO) {
        if (size != PXA_VIDEO_FRAME_BYTES) return PXA_STATUS_INVALID_ARGUMENT;
        if (!s->frame.rgb565) return PXA_STATUS_WOULD_BLOCK;
        memset(data, 0, size);
        pxa_write_u16(data, s->frame.width); pxa_write_u16(data+2, s->frame.height);
        pxa_write_u32(data+4, s->frame.stride_bytes); pxa_write_u64(data+8, s->frame.pts_us);
        pxa_write_u64(data+16, s->frame.decode_us); pxa_write_u32(data+24, (uint32_t)s->frame.size);
        return PXA_STATUS_OK;
    }
    if (operation == PXA_VIDEO_IO_CONFIGURE_SCALED) {
        if (size != 4) return PXA_STATUS_INVALID_ARGUMENT;
        const uint32_t w = pxa_read_u16(data), h = pxa_read_u16(data+2);
        if (!w || !h || w > 4096 || h > 4096 || (uint64_t)w*h > 4u*1024u*1024u)
            return PXA_STATUS_LIMIT_EXCEEDED;
        s->scaled_width = (uint16_t)w; s->scaled_height = (uint16_t)h;
        return PXA_STATUS_OK;
    }
    if (operation == PXA_VIDEO_IO_READ_SCALED || operation == PXA_VIDEO_IO_READ_SCALED_PIXELS) {
        const size_t prefix = operation == PXA_VIDEO_IO_READ_SCALED ? 8 : 0;
        if (prefix && (size < prefix || pxa_read_u32(data+4))) return PXA_STATUS_INVALID_ARGUMENT;
        if (!prefix && !s->scaled_width) return PXA_STATUS_BAD_STATE;
        if (!s->frame.rgb565) return PXA_STATUS_WOULD_BLOCK;
        const uint32_t w = prefix ? pxa_read_u16(data) : s->scaled_width;
        const uint32_t h = prefix ? pxa_read_u16(data+2) : s->scaled_height;
        if (!w || !h || w > 4096 || h > 4096 || (uint64_t)w*h > 4u*1024u*1024u)
            return PXA_STATUS_LIMIT_EXCEEDED;
        const uint64_t bytes = (uint64_t)w*h*2;
        if (bytes > SIZE_MAX-prefix || size != bytes+prefix) return PXA_STATUS_INVALID_ARGUMENT;
        uint8_t *out = data+prefix;
        if (w == s->frame.width && h == s->frame.height) {
            memcpy(out, s->frame.rgb565, (size_t)bytes);
            return PXA_STATUS_OK;
        }
        /* Exact floor(x*source/destination) sampling without a division per
         * pixel, intermediate allocations, or a full-size Guest frame copy. */
        const uint32_t dx = s->frame.width/w, rx = s->frame.width%w;
        const uint32_t dy = s->frame.height/h, ry = s->frame.height%h;
        uint32_t sy = 0, ey = 0;
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t *row = s->frame.rgb565+(size_t)sy*s->frame.stride_bytes;
            uint32_t sx = 0, ex = 0;
#if defined(__GNUC__) || defined(__clang__)
            if (!(((uintptr_t)out|(uintptr_t)row)&1u)) {
                for (uint32_t x = 0; x < w; ++x) {
                    memcpy(__builtin_assume_aligned(out,2),
                        __builtin_assume_aligned(row+sx*2,2),2);
                    out += 2; sx += dx; ex += rx;
                    if (ex >= w) { ex -= w; ++sx; }
                }
            } else
#endif
            {
                for (uint32_t x = 0; x < w; ++x) {
                    *out++ = row[sx*2]; *out++ = row[sx*2+1];
                    sx += dx; ex += rx;
                    if (ex >= w) { ex -= w; ++sx; }
                }
            }
            sy += dy; ey += ry;
            if (ey >= h) { ey -= h; ++sy; }
        }
        return PXA_STATUS_OK;
    }
    if (operation == PXA_IO_READ) {
        if (!s->frame.rgb565) return PXA_STATUS_WOULD_BLOCK;
        size_t count = s->frame.size - s->read_offset;
        if (count > size) count = size;
        if (count > INT32_MAX) count = INT32_MAX;
        if (count) memcpy(data, s->frame.rgb565 + s->read_offset, count);
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

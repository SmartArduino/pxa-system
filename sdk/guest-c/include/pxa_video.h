// SPDX-License-Identifier: Apache-2.0
#ifndef PXA_GUEST_VIDEO_H
#define PXA_GUEST_VIDEO_H
#include "pxa_core.h"
#define PXA_VIDEO_SERVICE 24u
#define PXA_VIDEO_CAPABILITIES 1u
#define PXA_VIDEO_OPEN 2u
#define PXA_VIDEO_H264 1u
#define PXA_VIDEO_MJPEG 2u
#define PXA_VIDEO_SOFTWARE 1u
#define PXA_VIDEO_HARDWARE 2u
#define PXA_VIDEO_PROFILE_BASELINE 1u
#define PXA_VIDEO_PROFILE_MAIN 2u
#define PXA_VIDEO_PROFILE_HIGH 4u
#define PXA_VIDEO_IO_DECODE 0x100u
#define PXA_VIDEO_IO_FRAME_INFO 0x101u
#define PXA_VIDEO_IO_RESET 0x102u
#define PXA_VIDEO_IO_READ_SCALED 0x103u
#define PXA_VIDEO_IO_CONFIGURE_SCALED 0x104u
#define PXA_VIDEO_IO_READ_SCALED_PIXELS 0x105u
#define PXA_VIDEO_IO_AUDIO_DECODE 0x110u
#define PXA_VIDEO_IO_AUDIO_INFO 0x111u
#define PXA_VIDEO_IO_AUDIO_READ 0x112u
#define PXA_VIDEO_AUDIO_AAC_ADTS 1u
typedef struct {
    int32_t status;
    uint16_t count, index, codec, implementation;
    uint32_t profiles;
    uint16_t recommended_width, recommended_height, max_width, max_height;
    uint32_t max_pixels, max_packet_bytes;
    uint16_t recommended_fps, max_level;
    uint32_t implementation_id;
    uint16_t audio_codecs;
} pxa_video_capability_t;
typedef struct {
    uint16_t width, height;
    uint32_t stride_bytes;
    uint64_t pts_us, decode_us;
    uint32_t frame_bytes;
} pxa_video_frame_t;
typedef struct {
    uint32_t sample_rate, frame_bytes;
    uint16_t channels, bits_per_sample;
    uint64_t pts_us;
} pxa_video_audio_info_t;
static inline int32_t pxa_video_request_capability(uint64_t token, uint16_t index) {
    uint8_t body[2]; pxa_store_u16(body,index);
    if (!token) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_control_call(24,1,token,body,sizeof(body));
}
static inline int32_t pxa_video_request_open(uint64_t token, uint16_t index, uint16_t width, uint16_t height) {
    uint8_t body[8]={0};
    if (!token || !width || !height) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u16(body,index); pxa_store_u16(body+2,width); pxa_store_u16(body+4,height);
    return pxa_control_call(24,2,token,body,sizeof(body));
}
static inline int pxa_video_parse_capability(const pxa_event_t *e, uint64_t token, pxa_video_capability_t *c) {
    if (!c) return 0;
    pxa_zero(c,sizeof(*c));
    if (!e || !token || e->token!=token || e->service!=24 || e->opcode!=1 || !e->payload || e->payload_size<4) return 0;
    c->status=(int32_t)pxa_load_u32(e->payload);
    if (c->status) return e->payload_size==4;
    if (e->payload_size!=44) return 0;
    const uint8_t *p=e->payload+4;
    c->count=pxa_load_u16(p); c->index=pxa_load_u16(p+2);
    c->codec=pxa_load_u16(p+4); c->implementation=pxa_load_u16(p+6); c->profiles=pxa_load_u32(p+8);
    c->recommended_width=pxa_load_u16(p+12); c->recommended_height=pxa_load_u16(p+14);
    c->max_width=pxa_load_u16(p+16); c->max_height=pxa_load_u16(p+18);
    c->max_pixels=pxa_load_u32(p+20); c->max_packet_bytes=pxa_load_u32(p+24);
    c->recommended_fps=pxa_load_u16(p+28); c->max_level=pxa_load_u16(p+30);
    c->implementation_id=pxa_load_u32(p+32); c->audio_codecs=pxa_load_u16(p+38);
    if (!c->count) return c->index==0 && c->codec==0 && pxa_load_u16(p+36)==0 && !c->audio_codecs;
    return c->index<c->count && c->codec && c->implementation && c->max_width && c->max_height &&
        c->max_pixels && c->max_packet_bytes && pxa_load_u16(p+36)==1;
}
/* packet is caller-owned writable storage: 16 bytes of header followed by one
 * Annex B access unit (H264) or complete JPEG. May be reused after this call.
 * drain uses a 16-byte packet with flags=1; call until frame_info would block. */
static inline int32_t pxa_video_decode(uint64_t handle, uint8_t *packet, uint32_t size, uint64_t pts_us, int drain) {
    if (!handle || !packet || size<16 || (drain && size!=16)) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u64(packet,pts_us); pxa_store_u32(packet+8,drain ? 1u : 0u); pxa_store_u32(packet+12,0);
    return pxa_io(handle,0x100,packet,size);
}
static inline int32_t pxa_video_frame_info(uint64_t handle, pxa_video_frame_t *out) {
    uint8_t body[32]; if (!out) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_zero(out,sizeof(*out));
    int32_t status=pxa_io(handle,0x101,body,sizeof(body)); if (status) return status;
    out->width=pxa_load_u16(body); out->height=pxa_load_u16(body+2); out->stride_bytes=pxa_load_u32(body+4);
    out->pts_us=pxa_load_u64(body+8); out->decode_us=pxa_load_u64(body+16); out->frame_bytes=pxa_load_u32(body+24);
    if (!out->width || !out->height || out->stride_bytes!=(uint32_t)out->width*2 ||
        out->frame_bytes!=(uint64_t)out->stride_bytes*out->height || pxa_load_u32(body+28)) return PXA_STATUS_PROTOCOL_ERROR;
    return 0;
}
/* Video 0.2: packet is eight header bytes followed by width*height*2 output
 * bytes. Nearest-neighbor resampling; does not advance the ordinary read cursor.
 * Older Hosts return UNSUPPORTED. Pixels start at packet+8 on success. */
static inline int32_t pxa_video_read_scaled(uint64_t handle, uint8_t *packet,
    uint32_t bytes, uint16_t width, uint16_t height) {
    if (!handle || !packet || !width || !height ||
        (uint64_t)width*height*2+8!=bytes) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u16(packet,width); pxa_store_u16(packet+2,height); pxa_store_u32(packet+4,0);
    return pxa_io(handle,PXA_VIDEO_IO_READ_SCALED,packet,bytes);
}
/* Video 0.3: configure once, then read pure pixels into e.g. an acquired
 * guest-mapped Surface buffer. Configuration survives RESET; no pointer is
 * retained. Both scaled read operations leave the ordinary cursor unchanged. */
static inline int32_t pxa_video_configure_scaled(uint64_t handle, uint16_t width, uint16_t height) {
    uint8_t body[4];
    if (!handle || !width || !height) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u16(body,width); pxa_store_u16(body+2,height);
    return pxa_io(handle,PXA_VIDEO_IO_CONFIGURE_SCALED,body,sizeof(body));
}
static inline int32_t pxa_video_read_scaled_pixels(uint64_t handle, uint8_t *pixels, uint32_t bytes) {
    if (!handle || !pixels || !bytes) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_io(handle,PXA_VIDEO_IO_READ_SCALED_PIXELS,pixels,bytes);
}
/* AAC input: pts_us:u64 followed by exactly one complete ADTS frame. */
static inline int32_t pxa_video_audio_decode(uint64_t handle, uint8_t *packet, uint32_t bytes, uint64_t pts_us) {
    if (!handle || !packet || bytes<15 || bytes>8192) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u64(packet,pts_us);
    return pxa_io(handle,PXA_VIDEO_IO_AUDIO_DECODE,packet,bytes);
}
static inline int32_t pxa_video_audio_info(uint64_t handle, pxa_video_audio_info_t *out) {
    uint8_t b[24]; if (!out) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_zero(out,sizeof(*out));
    int32_t status=pxa_io(handle,PXA_VIDEO_IO_AUDIO_INFO,b,sizeof(b)); if (status) return status;
    out->sample_rate=pxa_load_u32(b); out->channels=pxa_load_u16(b+4); out->bits_per_sample=pxa_load_u16(b+6);
    out->frame_bytes=pxa_load_u32(b+8); out->pts_us=pxa_load_u64(b+16);
    if (!out->sample_rate || !out->channels || out->channels>2 || out->bits_per_sample!=16 ||
        out->frame_bytes>16384 || out->frame_bytes%(out->channels*2) || pxa_load_u32(b+12)) return PXA_STATUS_PROTOCOL_ERROR;
    return 0;
}
#endif

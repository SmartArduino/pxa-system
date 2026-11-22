// SPDX-License-Identifier: Apache-2.0
#ifndef PXA_VIDEO_H
#define PXA_VIDEO_H
#include "pxa/runtime.h"
#include "pxa/service.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PXA_VIDEO_SERVICE_ID UINT16_C(24)
#define PXA_VIDEO_SERVICE_MAJOR UINT16_C(0)
#define PXA_VIDEO_SERVICE_MINOR UINT16_C(3)
#define PXA_VIDEO_SERVICE_PATCH UINT16_C(0)
#define PXA_VIDEO_CAPABILITIES UINT16_C(1)
#define PXA_VIDEO_OPEN UINT16_C(2)
#define PXA_VIDEO_H264 UINT16_C(1)
#define PXA_VIDEO_MJPEG UINT16_C(2)
#define PXA_VIDEO_SOFTWARE UINT16_C(1)
#define PXA_VIDEO_HARDWARE UINT16_C(2)
#define PXA_VIDEO_PROFILE_BASELINE UINT32_C(1)
#define PXA_VIDEO_PROFILE_MAIN UINT32_C(2)
#define PXA_VIDEO_PROFILE_HIGH UINT32_C(4)
#define PXA_VIDEO_IO_DECODE UINT32_C(0x100)
#define PXA_VIDEO_IO_FRAME_INFO UINT32_C(0x101)
#define PXA_VIDEO_IO_RESET UINT32_C(0x102)
#define PXA_VIDEO_IO_READ_SCALED UINT32_C(0x103)
#define PXA_VIDEO_IO_CONFIGURE_SCALED UINT32_C(0x104)
#define PXA_VIDEO_IO_READ_SCALED_PIXELS UINT32_C(0x105)
#define PXA_VIDEO_IO_AUDIO_DECODE UINT32_C(0x110)
#define PXA_VIDEO_IO_AUDIO_INFO UINT32_C(0x111)
#define PXA_VIDEO_IO_AUDIO_READ UINT32_C(0x112)
#define PXA_VIDEO_AUDIO_AAC_ADTS UINT16_C(1)
#define PXA_VIDEO_PACKET_DRAIN UINT32_C(1)
#define PXA_VIDEO_CAP_BYTES 40u
#define PXA_VIDEO_FRAME_BYTES 32u
/* One entry per implementation, allowing software and hardware for one codec.
 * Dimensions are visible picture dimensions; max_pixels also applies (including
 * rotation). Adapters bound coded padding before allocating reference pictures.
 * Recommendations are policy ceilings, not a real-time throughput guarantee. */
typedef struct {
    uint16_t codec, implementation;
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
    const uint8_t *rgb565;
    size_t size;
} pxa_video_frame_t;
typedef struct {
    uint32_t struct_size;
    void *context;
    uint16_t count;
    const pxa_video_capability_t *capabilities;
    pxa_status_t (*open)(void *, uint16_t index, uint16_t width, uint16_t height, void **);
    pxa_status_t (*decode)(void *, const uint8_t *, size_t, uint64_t pts, uint32_t flags,
                           pxa_video_frame_t *);
    pxa_status_t (*reset)(void *);
    void (*close)(void *);
    int32_t (*audio_io)(void *, uint32_t, uint8_t *, size_t);
} pxa_video_backend_t;
typedef struct pxa_video_service pxa_video_service_t;
size_t pxa_video_service_workspace_size(void);
pxa_status_t pxa_video_service_init(void *, size_t, pxa_runtime_t *,
    const pxa_video_backend_t *, pxa_video_service_t **);
pxa_status_t pxa_video_service_register(pxa_video_service_t *);
void pxa_video_service_set_foreground(pxa_video_service_t *, pxa_component_t);
void pxa_video_service_deinit(pxa_video_service_t *);
#ifdef __cplusplus
}
#endif
#endif

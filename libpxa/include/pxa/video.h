// SPDX-License-Identifier: Apache-2.0
#ifndef PXA_VIDEO_H
#define PXA_VIDEO_H
#include "pxa/runtime.h"
#include <stdbool.h>
#include "pxa/service.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PXA_VIDEO_SERVICE_ID UINT16_C(24)
#define PXA_VIDEO_SERVICE_MAJOR UINT16_C(0)
#define PXA_VIDEO_SERVICE_MINOR UINT16_C(4)
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
#define PXA_VIDEO_IO_QUEUE_DECODE UINT32_C(0x106)
#define PXA_VIDEO_IO_POLL_DECODE UINT32_C(0x107)
#define PXA_VIDEO_IO_BIND_SURFACE UINT32_C(0x109)
#define PXA_VIDEO_IO_PREPARE_SURFACE UINT32_C(0x10a)
#define PXA_VIDEO_IO_PRESENT_SURFACE UINT32_C(0x10b)
#define PXA_VIDEO_IO_PREFERRED_SIZE UINT32_C(0x10c)
#define PXA_VIDEO_IO_PREPARE_AND_QUEUE UINT32_C(0x10d)
#define PXA_VIDEO_IO_PRESENT_SURFACE_ORDERED UINT32_C(0x10e)
#define PXA_VIDEO_IO_POLL_FRAME UINT32_C(0x10f)
#define PXA_VIDEO_DECODE_READY UINT16_C(0x8001)
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
    /* Optional pair. Queue owns its copy before returning. Poll returns
     * WOULD_BLOCK while running, then consumes the result. Reset and close
     * must retire the job before changing decoder storage. */
    pxa_status_t (*queue_decode)(void *, const uint8_t *, size_t, uint64_t, uint32_t);
    pxa_status_t (*poll_decode)(void *, pxa_video_frame_t *);
    /* Optional pair. A decoded frame with nonzero logical RGB565 size and
     * NULL rgb565 is borrowed until the next decode/reset/close. Render into
     * a caller-selected RGB565 rectangle, or materialize for sequential READ. */
    void (*render_frame)(void *, uint8_t *out, uint32_t width, uint32_t height, uint32_t stride);
    pxa_status_t (*materialize_frame)(void *, pxa_video_frame_t *);
    /* Hint for subsequent packets. Zero dimensions restore ordinary output. */
    void (*set_surface_size)(void *, uint16_t width, uint16_t height);
    /* Optional native write into a bound RGB565 Surface. Return true only
     * after all pixels in the rectangle are ready and DMA has completed.
     * Other pixels and row padding belong to the Surface. Returning false
     * selects the regular render/copy path for this picture. */
    bool (*render_surface)(void *, uint8_t *base, uint32_t stride, uint16_t rows,
                           uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    /* Optional. Return a hardware-friendly aspect fit within both supplied bounds. */
    bool (*preferred_size)(void *, uint16_t *width, uint16_t *height);
    /* Stage the current picture and accept the next AU. On failure both
     * the current picture and the caller's packet remain available for retry. */
    pxa_status_t (*prepare_and_queue)(void *, const uint8_t *, size_t, uint64_t, uint32_t,
        uint8_t *pixels, uint32_t stride, uint16_t rows, uint16_t x, uint16_t y,
        uint16_t width, uint16_t height);
} pxa_video_backend_t;
#define PXA_VIDEO_BACKEND_PREFERRED_SIZE offsetof(pxa_video_backend_t, prepare_and_queue)
#define PXA_VIDEO_BACKEND_SURFACE_SIZE offsetof(pxa_video_backend_t, preferred_size)
#define PXA_VIDEO_BACKEND_DEFERRED_SIZE offsetof(pxa_video_backend_t, render_surface)
#define PXA_VIDEO_BACKEND_ASYNC_SIZE offsetof(pxa_video_backend_t, render_frame)
#define PXA_VIDEO_BACKEND_SYNC_SIZE offsetof(pxa_video_backend_t, queue_decode)
/* Copy only complete callback groups from the provider's advertised ABI. */
static inline size_t pxa_video_backend_copy_size(size_t size) {
    if (size >= sizeof(pxa_video_backend_t)) return sizeof(pxa_video_backend_t);
    if (size >= PXA_VIDEO_BACKEND_PREFERRED_SIZE) return PXA_VIDEO_BACKEND_PREFERRED_SIZE;
    if (size >= PXA_VIDEO_BACKEND_SURFACE_SIZE) return PXA_VIDEO_BACKEND_SURFACE_SIZE;
    if (size >= PXA_VIDEO_BACKEND_DEFERRED_SIZE) return PXA_VIDEO_BACKEND_DEFERRED_SIZE;
    if (size >= PXA_VIDEO_BACKEND_ASYNC_SIZE) return PXA_VIDEO_BACKEND_ASYNC_SIZE;
    return size == PXA_VIDEO_BACKEND_SYNC_SIZE ? size : 0;
}
typedef struct pxa_video_service pxa_video_service_t;
size_t pxa_video_service_workspace_size(void);
pxa_status_t pxa_video_service_init(void *, size_t, pxa_runtime_t *,
    const pxa_video_backend_t *, pxa_video_service_t **);
pxa_status_t pxa_video_service_register(pxa_video_service_t *);
void pxa_video_service_set_foreground(pxa_video_service_t *, pxa_component_t);
void pxa_video_service_dispatch(pxa_video_service_t *);
void pxa_video_service_deinit(pxa_video_service_t *);
#ifdef __cplusplus
}
#endif
#endif

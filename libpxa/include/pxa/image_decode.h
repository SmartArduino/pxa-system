#ifndef PXA_IMAGE_DECODE_H
#define PXA_IMAGE_DECODE_H
#include "pxa/asset_object.h"
#include "pxa/resource_budget.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct pxa_image_decoder pxa_image_decoder_t;
typedef struct pxa_image_target pxa_image_target_t;
typedef struct {
    uint16_t max_width, max_height;
    uint32_t max_pixels, max_encoded_bytes;
    /* Zero preserves the ordinary limits. Applied only to sequence codecs. */
    uint32_t max_animation_pixels, max_animation_encoded_bytes;
    uint8_t formats; /* zero=any; otherwise PNG=1, JPEG=2, GIF=4 */
} pxa_image_decode_options_t;
typedef struct {
    pxa_asset_object_t *image;
    pxa_asset_info_t info;
    uint16_t source_width, source_height;
} pxa_decoded_image_t;
typedef struct {
    uint32_t delay_ms, iteration;
    uint8_t last;
} pxa_image_frame_info_t;

/* Platform extension, not a Guest ABI. An ordered, immutable codec list.
 * open probes eight signature bytes; UNSUPPORTED tries the next codec.
 * Input is borrowed only for write; finish seals input, next may follow. Callbacks are
 * serialized on the runtime thread. No filesystem, network or Guest access.
 * Core closes any returned session even when open fails, and on cancellation.
 * Use the temporary allocator for workspace and the target for output: this
 * keeps limits, scaling, immutable ownership and pixel formats in one place. */
typedef struct {
    pxa_status_t (*open)(pxa_bytes_t signature, pxa_image_target_t *,
                         const pxa_memory_allocator_t *temporary, void **session);
    pxa_status_t (*write)(void *session, pxa_bytes_t);
    pxa_status_t (*finish)(void *session);
    void (*close)(void *session);
    /* Optional sequence support. finish validates/prepares input; next then
     * composes one frame into the target. NOT_FOUND means end of sequence.
     * Frames are immutable snapshots. No timers or automatic frame queue. */
    pxa_status_t (*next)(void *session, pxa_image_frame_info_t *);
} pxa_image_codec_t;
typedef struct {
    const pxa_image_codec_t *const *codecs;
    size_t count;
} pxa_image_backend_t;

enum {
    PXA_IMAGE_RGBA8888,
    PXA_IMAGE_BGRA8888,
    PXA_IMAGE_RGB888,
    PXA_IMAGE_BGR888,
    PXA_IMAGE_RGB565
};
typedef struct {
    /* Area in the original image. Pixel dimensions may differ (native IDCT
     * scaling, hardware resizing, or one replicated Adam7 sample). */
    uint32_t x, y, width, height;
    uint32_t pixel_width, pixel_height, stride;
    uint8_t format;
    const uint8_t *pixels;
} pxa_image_tile_t;
const pxa_image_decode_options_t *pxa_image_target_options(const pxa_image_target_t *);
/* Optional serialized work hint, e.g. a software PNG codec can request short
 * owner-loop slices. Zero restores the 4096-byte default. This bounds encoded
 * input per poll, not wall time; finish/next remain synchronous. The target
 * and hint belong to the decoder; no codec descriptor/storage ABI changes. */
void pxa_image_target_set_input_budget(pxa_image_target_t *, size_t bytes);
pxa_status_t pxa_image_target_begin(pxa_image_target_t *, uint32_t width, uint32_t height,
                                    int alpha);
void pxa_image_target_dimensions(const pxa_image_target_t *, uint16_t *, uint16_t *);
pxa_status_t pxa_image_target_blit(pxa_image_target_t *, const pxa_image_tile_t *);
/* Create the next writable frame, optionally copying a matching immutable
 * snapshot. Source dimensions and output limits remain those of begin(). */
pxa_status_t pxa_image_target_begin_frame(pxa_image_target_t *, const pxa_asset_object_t *base);
/* Retained snapshot of the completed frame; release when no longer needed. */
pxa_asset_object_t *pxa_image_target_retain_frame(pxa_image_target_t *);
/* Independent immutable copy, for a restore-previous checkpoint that differs
 * from the last published frame. Charges the same bounded pixel allocator. */
pxa_status_t pxa_image_target_copy_frame(pxa_image_target_t *, pxa_asset_object_t **);

/* Core owns dispatch and output; codecs are optional adapters. No decoder
 * state or pixel storage is allocated until an image is requested. */
pxa_status_t pxa_image_decoder_create(const pxa_image_decode_options_t *,
                                      const pxa_memory_allocator_t *pixels,
                                      const pxa_memory_allocator_t *temporary,
                                      const pxa_image_backend_t *, pxa_image_decoder_t **);
pxa_status_t pxa_image_decoder_write(pxa_image_decoder_t *, pxa_bytes_t);
/* Before signature selection use a small probe. After selection clamp the
 * target hint to the service's 4096-byte default. Does not allocate storage. */
size_t pxa_image_decoder_input_budget(const pxa_image_decoder_t *);
pxa_status_t pxa_image_decoder_finish(pxa_image_decoder_t *, pxa_decoded_image_t *);
/* Finalizes input on the first call, then pulls frames on demand. OK with a
 * null image and last=1 means EOF. Static images yield one frame. A caller may
 * keep any published frame alive independently of this decoder. */
pxa_status_t pxa_image_decoder_next(pxa_image_decoder_t *, pxa_decoded_image_t *,
                                    pxa_image_frame_info_t *);
void pxa_image_decoder_destroy(pxa_image_decoder_t *);
#ifdef __cplusplus
}
#endif
#endif

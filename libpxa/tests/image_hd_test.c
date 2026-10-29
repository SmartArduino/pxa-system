// SPDX-License-Identifier: MIT
#undef NDEBUG
#include "pxa/image_decode.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static size_t allocations, fail_at;
static int alpha;
static pxa_image_target_t *target;
static void *allocate(void *context, size_t bytes) {
    (void)context;
    return ++allocations == fail_at ? NULL : malloc(bytes);
}
static void release(void *context, void *memory) { (void)context; free(memory); }
static pxa_status_t open_image(pxa_bytes_t signature, pxa_image_target_t *t,
    const pxa_memory_allocator_t *temporary, void **session) {
    (void)signature; (void)temporary; target = t; *session = t; return 0;
}
static pxa_status_t write_image(void *session, pxa_bytes_t bytes) {
    (void)session; (void)bytes; return 0;
}
static pxa_status_t finish_image(void *session) {
    (void)session; return pxa_image_target_begin(target, 680, 1274, alpha);
}
static void close_image(void *session) { (void)session; }
static pxa_status_t next_frame(void *session, pxa_image_frame_info_t *frame) {
    (void)session; frame->last = 1; return 0;
}
static pxa_status_t exercise(size_t quota, uint32_t pixels, int sequence,
    uint32_t expected_pixels) {
    pxa_memory_budget_t budget;
    pxa_memory_budget_config_t config = {.limit = {0, quota},
        .temporary_limit = {0, 65536}};
    assert(!pxa_memory_budget_init(&budget, &config));
    pxa_memory_owner_t owner;
    assert(!pxa_memory_owner_open(&budget, config.limit, &owner));
    pxa_memory_allocator_t memory = {&budget, owner, 1, PXA_MEMORY_IMAGE,
        NULL, allocate, release}, temporary = memory;
    temporary.kind = PXA_MEMORY_TEMPORARY;
    const pxa_image_codec_t codec = {.open = open_image, .write = write_image,
        .finish = finish_image, .close = close_image,
        .next = sequence ? next_frame : NULL};
    const pxa_image_codec_t *codecs[] = {&codec};
    const pxa_image_backend_t backend = {codecs, 1};
    pxa_image_decode_options_t options = {.max_width=2048, .max_height=2048,
        .max_pixels=pixels, .max_encoded_bytes=131072};
    pxa_image_decoder_t *decoder = NULL;
    pxa_status_t status = pxa_image_decoder_create(&options,&memory,&temporary,&backend,&decoder);
    const uint8_t signature[8] = {0};
    if (!status) status=pxa_image_decoder_write(decoder,(pxa_bytes_t){signature,8});
    pxa_decoded_image_t image = {0};
    if (!status) status=pxa_image_decoder_finish(decoder,&image);
    if (!status) {
        pxa_asset_object_view_t view; pxa_asset_object_view(image.image,&view);
        assert(view.width * view.height <= expected_pixels);
        if (expected_pixels >= 680u*1274u)
            assert(view.width==680 && view.height==1274);
        assert(view.bytes == (uint32_t)view.width*view.height*(alpha ? 4u : 2u));
        assert(image.source_width==680 && image.source_height==1274);
    }
    pxa_image_decoder_destroy(decoder);
    pxa_asset_object_release(image.image);
    pxa_memory_stats_t stats;
    assert(!pxa_memory_budget_stats(&budget,owner,&stats) && !stats.charged[1]);
    assert(!pxa_memory_owner_close(&budget,owner));
    return status;
}
int main(void) {
    for (alpha=0;alpha<2;++alpha) {
        allocations=0;
        assert(!exercise(5u*1024*1024,1048576,0,1048576));
        size_t total=allocations;
        for (size_t n=1;n<=total;++n) {
            allocations=0;fail_at=n;
            assert(exercise(5u*1024*1024,1048576,0,1048576));
        }
        fail_at=0;
        assert(exercise(512u*1024,1048576,0,1048576)==PXA_STATUS_RESOURCE_LIMIT);
        assert(!exercise(512u*1024,65536,0,65536));
        assert(!exercise(512u*1024,1048576,1,65536));
        assert(exercise(5u*1024*1024,1048577,0,1048576)==PXA_STATUS_INVALID_ARGUMENT);
    }
    puts("HD static images: original dimensions, alpha, quota, allocation failures and sequence ceiling PASS");
}

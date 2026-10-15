#undef NDEBUG
#include "pxa/image/codecs.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocations, fail_at;
static void *allocate(void *context, size_t bytes) {
    (void)context;
    return ++allocations == fail_at ? NULL : malloc(bytes);
}
static void release(void *context, void *memory) { (void)context; free(memory); }
static uint8_t *read_file(const char *path, size_t *bytes) {
    FILE *file = fopen(path, "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END)); long n = ftell(file); assert(n > 0);
    rewind(file); uint8_t *data = malloc((size_t)n); assert(data);
    assert(fread(data, 1, (size_t)n, file) == (size_t)n); fclose(file);
    *bytes = (size_t)n; return data;
}
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t checksum(pxa_asset_object_t *image) {
    pxa_asset_object_view_t view; pxa_asset_object_view(image, &view);
    uint64_t hash = 14695981039346656037ULL;
    for (uint32_t i = 0; i < view.bytes; ++i) hash = (hash ^ view.data[i]) * 1099511628211ULL;
    return hash;
}
static pxa_status_t decode(const uint8_t *data, size_t bytes, size_t chunk,
                           const uint8_t *reference, size_t reference_bytes, size_t cancel_at,
                           size_t *peak) {
    pxa_memory_budget_config_t config = {0};
    config.limit[1] = 512 * 1024; config.temporary_limit[1] = 384 * 1024;
    pxa_memory_budget_t budget; pxa_memory_owner_t owner;
    assert(!pxa_memory_budget_init(&budget, &config));
    assert(!pxa_memory_owner_open(&budget, config.limit, &owner));
    pxa_memory_allocator_t pixels = {&budget, owner, 1, PXA_MEMORY_IMAGE, NULL, allocate, release};
    pxa_memory_allocator_t temporary = pixels; temporary.kind = PXA_MEMORY_TEMPORARY;
    static const pxa_image_codec_t *const codecs[] = {&pxa_lvgl_gif_codec};
    static const pxa_image_backend_t backend = {codecs, 1};
    pxa_image_decode_options_t options = {.max_width=64, .max_height=64, .max_pixels=4096, .max_encoded_bytes=131072};
    pxa_image_decoder_t *decoder = NULL;
    pxa_asset_object_t *held = NULL; uint64_t held_hash = 0;
    pxa_status_t status = pxa_image_decoder_create(&options, &pixels, &temporary, &backend, &decoder);
    for (size_t at = 0; !status && at < bytes && at < cancel_at;) {
        size_t n = bytes - at;
        if (n > chunk) n = chunk;
        if (n > cancel_at - at) n = cancel_at - at;
        status = pxa_image_decoder_write(decoder, (pxa_bytes_t){data + at, n}); at += n;
    }
    unsigned frames = reference ? word(reference + 8) : 12;
    const uint8_t *expected = reference ? reference + 16 : NULL;
    for (unsigned i = 0; !status && i < frames && cancel_at >= bytes; ++i) {
        pxa_decoded_image_t image; pxa_image_frame_info_t info;
        status = pxa_image_decoder_next(decoder, &image, &info);
        if (status) break;
        if (!image.image) { assert(!reference); break; }
        pxa_asset_object_view_t view; pxa_asset_object_view(image.image, &view);
        if (held) assert(checksum(held) == held_hash);
        if (reference) {
            assert(view.width == word(reference) && view.height == word(reference + 4));
            const size_t pixels = (size_t)view.width * view.height;
            assert((size_t)(expected - reference) + 12 + pixels * 4 <= reference_bytes);
            assert(info.delay_ms == word(expected) && info.iteration == word(expected + 4) &&
                   info.last == word(expected + 8));
            expected += 12;
            for (size_t pixel = 0; pixel < pixels; ++pixel) {
                const uint8_t *want = expected + pixel * 4;
                if (view.encoding == PXA_ASSET_ENCODING_RGB565) {
                    uint16_t actual; memcpy(&actual, view.data + pixel * 2, 2);
                    uint16_t rgb = (uint16_t)((want[2] & 248) << 8 | (want[1] & 252) << 3 | want[0] >> 3);
                    assert(want[3] == 255 && actual == rgb);
                } else {
                    assert(view.encoding == PXA_ASSET_ENCODING_BGRA8888);
                    const uint8_t *actual = view.data + pixel * 4;
                    if (actual[3] != want[3] || (want[3] && memcmp(actual, want, 3))) {
                        fprintf(stderr, "frame=%u pixel=%zu got=%u,%u,%u,%u expected=%u,%u,%u,%u\n", i,
                                pixel, actual[0], actual[1], actual[2], actual[3], want[0], want[1], want[2], want[3]);
                        abort();
                    }
                }
            }
            expected += pixels * 4;
        }
        if (!held) { held = image.image; pxa_asset_object_retain(held); held_hash = checksum(held); }
        pxa_asset_object_release(image.image);
    }
    if (!status && reference && cancel_at >= bytes && word(reference + 12)) {
        pxa_decoded_image_t image; pxa_image_frame_info_t info;
        status = pxa_image_decoder_next(decoder, &image, &info);
        assert(!status && !image.image && info.last);
        assert(!pxa_image_decoder_next(decoder, &image, &info) && !image.image && info.last);
        assert(expected == reference + reference_bytes);
    }
    pxa_image_decoder_destroy(decoder);
    if (held) { assert(checksum(held) == held_hash); pxa_asset_object_release(held); }
    pxa_memory_stats_t stats; assert(!pxa_memory_budget_stats(&budget, owner, &stats));
    assert(!stats.charged[0] && !stats.charged[1]);
    assert(!pxa_memory_owner_close(&budget, owner));
    if (peak) *peak = stats.peak[1];
    return status;
}
int main(int argc, char **argv) {
    if (!LV_USE_GIF || !pxa_lvgl_gif_codec.next) return 77;
    assert(argc == 3); lv_init();
    size_t bytes, reference_bytes, peak;
    uint8_t *data = read_file(argv[1], &bytes), *reference = read_file(argv[2], &reference_bytes);
    assert(reference_bytes >= 16 && word(reference + 8));
    const size_t chunks[] = {1, 7, 4096};
    for (unsigned i = 0; i < sizeof(chunks)/sizeof(chunks[0]); ++i) {
        allocations = 0;
        assert(!decode(data, bytes, chunks[i], reference, reference_bytes, SIZE_MAX, &peak));
    }
    size_t count = allocations;
    for (size_t i = 1; i <= count; ++i) {
        allocations = 0; fail_at = i;
        assert(decode(data, bytes, 4096, reference, reference_bytes, SIZE_MAX, NULL) == PXA_STATUS_RESOURCE_LIMIT);
    }
    fail_at = 0;
    for (size_t at = 0; at < bytes; at += bytes / 17 + 1) {
        assert(!decode(data, bytes, 7, NULL, 0, at, NULL)); /* cancellation before finish */
        assert(decode(data, at, 4096, NULL, 0, SIZE_MAX, NULL)); /* truncated input */
    }
    uint32_t random = 0x9854f00d;
    for (unsigned i = 0; i < 192; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        size_t at = random % bytes; uint8_t old = data[at]; data[at] ^= (uint8_t)(1u << (i % 8));
        (void)decode(data, bytes, 4096, NULL, 0, SIZE_MAX, NULL); data[at] = old;
    }
    printf("GIF PASS frames=%u peak=%zu allocations=%zu: %s\n", word(reference + 8), peak, count, argv[1]);
    free(reference); free(data); lv_deinit(); return 0;
}

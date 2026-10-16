#undef NDEBUG
#include "pxa/image/codecs.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocations, fail_at;
static int mutated;
static const pxa_image_backend_t *backend;
static pxa_status_t probe_status;
static unsigned probe_closed;
static pxa_status_t probe_open(pxa_bytes_t signature, pxa_image_target_t *target,
    const pxa_memory_allocator_t *temporary, void **session) {
    (void)signature; (void)target;
    *session = pxa_memory_allocate(temporary, 1);
    assert(*session);
    return probe_status;
}
static pxa_status_t probe_write(void *session, pxa_bytes_t bytes) {
    (void)session; (void)bytes; abort();
}
static pxa_status_t probe_finish(void *session) {
    (void)session; abort();
}
static void probe_close(void *session) {
    ++probe_closed;
    pxa_memory_release(session);
}
static pxa_image_target_t *tile_target;
static unsigned tile_width, tile_height, tile_format;
static pxa_status_t tile_open(pxa_bytes_t signature, pxa_image_target_t *target,
    const pxa_memory_allocator_t *temporary, void **session) {
    (void)signature; (void)temporary;
    tile_target = target;
    *session = target;
    return PXA_STATUS_OK;
}
static pxa_status_t tile_write(void *session, pxa_bytes_t bytes) {
    (void)session; (void)bytes;
    return PXA_STATUS_OK;
}
static void tile_close(void *session) { (void)session; }
static uint16_t sample565(unsigned x, unsigned y) {
    return (uint16_t)(((x * 3 + y * 7) & 31) << 11 | ((x * 11 + y) & 63) << 5 |
                      ((x + y * 13) & 31));
}
static pxa_status_t tile_finish(void *session) {
    (void)session;
    pxa_status_t status = pxa_image_target_begin(tile_target, 97, 65, tile_format != PXA_IMAGE_RGB565);
    for (unsigned y = 0; !status && y < 65; y += tile_height)
        for (unsigned x = 0; !status && x < 97; x += tile_width) {
            uint8_t pixels[7 * 3 * 4];
            unsigned bpp = tile_format == PXA_IMAGE_RGB565 ? 2 : 4;
            for (unsigned j = 0; j < 3; ++j)
                for (unsigned i = 0; i < 7; ++i) {
                    uint16_t rgb = sample565(x + i, y + j);
                    uint8_t *p = pixels + (j * 7 + i) * bpp;
                    if (bpp == 2) memcpy(p, &rgb, 2);
                    else {
                        p[0] = (uint8_t)((rgb >> 11) * 255 / 31);
                        p[1] = (uint8_t)(((rgb >> 5) & 63) * 255 / 63);
                        p[2] = (uint8_t)((rgb & 31) * 255 / 31);
                        p[3] = (uint8_t)(x + i + y + j);
                    }
                }
            pxa_image_tile_t tile = {x, y, tile_width, tile_height, 7, 3, 7 * bpp,
                                     (uint8_t)tile_format, pixels};
            status = pxa_image_target_blit(tile_target, &tile);
        }
    return status;
}
static void *raw_alloc(void *context, size_t bytes) {
    (void)context;
    return ++allocations == fail_at ? NULL : malloc(bytes);
}
static void raw_free(void *context, void *memory) {
    (void)context;
    free(memory);
}
static void test_tiles(void) {
    const pxa_image_codec_t codec = {.open = tile_open, .write = tile_write, .finish = tile_finish, .close = tile_close};
    const pxa_image_codec_t *codecs[] = {&codec};
    const pxa_image_backend_t tile_backend = {codecs, 1};
    const unsigned widths[] = {3, 7, 14, 21, 28, 35};
    const unsigned heights[] = {2, 3, 6, 9};
    for (unsigned alpha = 0; alpha < 2; ++alpha)
        for (unsigned i = 0; i < 6; ++i)
            for (unsigned j = 0; j < 4; ++j) {
                tile_width = widths[i]; tile_height = heights[j];
                tile_format = alpha ? PXA_IMAGE_RGBA8888 : PXA_IMAGE_RGB565;
                pxa_memory_budget_t budget;
                pxa_memory_budget_config_t config = {0};
                config.limit[1] = config.temporary_limit[1] = 512 * 1024;
                assert(!pxa_memory_budget_init(&budget, &config));
                pxa_memory_owner_t owner;
                assert(!pxa_memory_owner_open(&budget, config.limit, &owner));
                pxa_memory_allocator_t memory = {&budget, owner, 1, PXA_MEMORY_IMAGE,
                                                 NULL, raw_alloc, raw_free};
                pxa_memory_allocator_t temporary = memory;
                temporary.kind = PXA_MEMORY_TEMPORARY;
                pxa_image_decode_options_t options = {.max_width=64, .max_height=64, .max_pixels=4096, .max_encoded_bytes=1024};
                pxa_image_decoder_t *decoder;
                assert(!pxa_image_decoder_create(&options, &memory, &temporary, &tile_backend, &decoder));
                const uint8_t signature[8] = {1};
                assert(!pxa_image_decoder_write(decoder, (pxa_bytes_t){signature, 8}));
                pxa_decoded_image_t decoded;
                if (i == 0 && j == 0) {
                    pxa_image_frame_info_t frame;
                    assert(!pxa_image_decoder_next(decoder, &decoded, &frame) && frame.last);
                    pxa_decoded_image_t end;
                    assert(!pxa_image_decoder_next(decoder, &end, &frame) && !end.image && frame.last);
                    assert(pxa_image_decoder_finish(decoder, &end) == PXA_STATUS_BAD_STATE);
                } else assert(!pxa_image_decoder_finish(decoder, &decoded));
                pxa_image_decoder_destroy(decoder);
                pxa_asset_object_view_t view;
                pxa_asset_object_view(decoded.image, &view);
                for (unsigned y = 0; y < view.height; ++y)
                    for (unsigned x = 0; x < view.width; ++x) {
                        unsigned sx = x * 97 / view.width, sy = y * 65 / view.height;
                        unsigned tx = sx / tile_width * tile_width, ty = sy / tile_height * tile_height;
                        sx = tx + (sx - tx) * 7 / tile_width;
                        sy = ty + (sy - ty) * 3 / tile_height;
                        uint16_t expected = sample565(sx, sy);
                        const uint8_t *actual = view.data + (y * view.width + x) * (alpha ? 4 : 2);
                        if (!alpha) assert(!memcmp(actual, &expected, 2));
                        else assert(actual[0] == (expected & 31) * 255 / 31 &&
                                    actual[1] == ((expected >> 5) & 63) * 255 / 63 &&
                                    actual[2] == (expected >> 11) * 255 / 31 &&
                                    actual[3] == (uint8_t)(sx + sy));
                    }
                pxa_asset_object_release(decoded.image);
                pxa_memory_stats_t stats;
                assert(!pxa_memory_budget_stats(&budget, owner, &stats) && !stats.charged[1]);
                assert(!pxa_memory_owner_close(&budget, owner));
            }
}
static uint8_t *read_file(const char *directory, const char *name, size_t *bytes) {
    char path[2048];
    assert(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0);
    rewind(file);
    uint8_t *data = malloc((size_t)length);
    assert(data);
    assert(fread(data, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    *bytes = (size_t)length;
    return data;
}
static pxa_status_t decode(const uint8_t *data, size_t bytes, size_t chunk, int png,
                           const uint8_t *expected) {
    pxa_memory_budget_t budget;
    pxa_memory_budget_config_t config = {0};
    config.limit[1] = 512 * 1024;
    config.temporary_limit[1] = 384 * 1024;
    assert(!pxa_memory_budget_init(&budget, &config));
    pxa_memory_owner_t owner;
    assert(!pxa_memory_owner_open(&budget, config.limit, &owner));
    pxa_memory_allocator_t pixels = {&budget, owner,     1,       PXA_MEMORY_IMAGE,
                                     NULL,    raw_alloc, raw_free};
    pxa_memory_allocator_t temporary = pixels;
    temporary.kind = PXA_MEMORY_TEMPORARY;
    pxa_image_decode_options_t options = {.max_width=64, .max_height=64, .max_pixels=4096, .max_encoded_bytes=131072};
    pxa_image_decoder_t *decoder = NULL;
    pxa_status_t status = pxa_image_decoder_create(
        &options, &pixels, &temporary, backend ? backend : pxa_posix_image_backend(), &decoder);
    for (size_t at = 0; !status && at < bytes;) {
        size_t n = bytes - at;
        if (n > chunk)
            n = chunk;
        status = pxa_image_decoder_write(decoder, (pxa_bytes_t){data + at, n});
        at += n;
    }
    pxa_decoded_image_t decoded = {0};
    if (!status)
        status = pxa_image_decoder_finish(decoder, &decoded);
    pxa_image_decoder_destroy(decoder);
    if (!status) {
        if (!mutated)
            assert(decoded.source_width == 320 && decoded.source_height == 180);
        pxa_asset_object_view_t view;
        pxa_asset_object_view(decoded.image, &view);
        assert(view.width <= 64 && view.height <= 64);
        if (!mutated)
            assert(view.width == 64 && view.height == 36);
        assert(view.bytes == (uint32_t)view.width * view.height * (png ? 4u : 2u));
        if (expected && png)
            for (unsigned y = 0; y < view.height; ++y)
                for (unsigned x = 0; x < view.width; ++x) {
                    const uint8_t *source = expected + 4 * ((y * 180 / 36) * 320 + x * 320 / 64);
                    const uint8_t *actual = view.data + 4 * (y * 64 + x);
                    assert(actual[0] == source[2] && actual[1] == source[1] &&
                           actual[2] == source[0] && actual[3] == source[3]);
                }
        if (expected && !png) {
            unsigned long error = 0;
            for (unsigned i = 0; i < 64 * 36; ++i) {
                uint16_t value;
                memcpy(&value, view.data + 2 * i, 2);
                int channels[3] = {(value >> 11) * 255 / 31, ((value >> 5) & 63) * 255 / 63,
                                   (value & 31) * 255 / 31};
                for (unsigned c = 0; c < 3; ++c)
                    error += (unsigned)abs(channels[c] - expected[3 * i + c]);
            }
            assert(error < 64 * 36 * 3 * 18); /* IDCT scaling and RGB565 are intentionally lossy. */
        }
        /* Pixels remain valid after destroying the decoder that allocated them. */
        pxa_asset_object_retain(decoded.image);
        pxa_asset_object_release(decoded.image);
        pxa_asset_object_release(decoded.image);
    }
    pxa_memory_stats_t stats;
    assert(!pxa_memory_budget_stats(&budget, owner, &stats));
    assert(stats.charged[0] == 0 && stats.charged[1] == 0);
    assert(!pxa_memory_owner_close(&budget, owner));
    return status;
}
static pxa_status_t large_jpeg(const uint8_t *data, size_t bytes, const uint8_t *reference,
    size_t reference_bytes, unsigned width, unsigned height, size_t chunk) {
    pxa_memory_budget_t budget;
    pxa_memory_budget_config_t config = {0};
    config.limit[1] = 512 * 1024;
    config.temporary_limit[1] = 384 * 1024;
    assert(!pxa_memory_budget_init(&budget, &config));
    pxa_memory_owner_t owner;
    assert(!pxa_memory_owner_open(&budget, config.limit, &owner));
    pxa_memory_allocator_t pixels = {&budget, owner, 1, PXA_MEMORY_IMAGE, NULL, raw_alloc, raw_free};
    pxa_memory_allocator_t temporary = pixels;
    temporary.kind = PXA_MEMORY_TEMPORARY;
    pxa_image_decode_options_t options = {.max_width=256, .max_height=1024,
        .max_pixels=32768, .max_encoded_bytes=524288};
    pxa_image_decoder_t *decoder = NULL;
    pxa_status_t status = pxa_image_decoder_create(&options,&pixels,&temporary,pxa_posix_image_backend(),&decoder);
    for (size_t at=0; !status && at<bytes;) {
        size_t n=bytes-at;
        if (n>chunk) n=chunk;
        status=pxa_image_decoder_write(decoder,(pxa_bytes_t){data+at,n});
        at+=n;
    }
    pxa_decoded_image_t decoded = {0};
    if (!status) status=pxa_image_decoder_finish(decoder,&decoded);
    pxa_image_decoder_destroy(decoder);
    if (!status) {
        assert(decoded.source_width==width && decoded.source_height==height);
        pxa_asset_object_view_t view;
        pxa_asset_object_view(decoded.image,&view);
        assert(view.width*view.height<=32768 && reference_bytes==(size_t)view.width*view.height*3);
        unsigned long error=0;
        for (unsigned i=0;i<view.width*view.height;++i) {
            uint16_t rgb;
            memcpy(&rgb,view.data+2*i,2);
            int channels[3]={(rgb>>11)*255/31,((rgb>>5)&63)*255/63,(rgb&31)*255/31};
            for (unsigned c=0;c<3;++c) error+=(unsigned)abs(channels[c]-reference[3*i+c]);
        }
        assert(error<reference_bytes*18); /* Same intentional IDCT/RGB565 loss. */
        pxa_asset_object_release(decoded.image);
    }
    pxa_memory_stats_t stats;
    assert(!pxa_memory_budget_stats(&budget,owner,&stats) && !stats.charged[1]);
    if (!status) {
        /* Input + bounded block slack/metadata, not a geometric resize peak.
         * libjpeg's private allocator is not part of this budget statistic. */
        assert(stats.temporary_peak[1] < bytes + 16000);
        printf("JPEG_INPUT bytes=%zu source=%ux%u temporary_peak=%zu charged_peak=%zu\n",
            bytes,width,height,stats.temporary_peak[1],stats.peak[1]);
    }
    assert(!pxa_memory_owner_close(&budget,owner));
    return status;
}
int main(int argc, char **argv) {
    lv_init();
    test_tiles();
    assert(argc == 2);
    const char *names[] = {"alpha.png",      "palette.png", "gray.png", "interlaced.png",
                           "background.png", "photo.jpg",   "gray.jpg"};
    for (unsigned i=0;i<2;++i) {
        const char *name=i ? "portrait.jpg" : "padded.jpg";
        size_t n, reference_bytes;
        uint8_t *data=read_file(argv[1],name,&n);
        char reference_name[64];snprintf(reference_name,sizeof(reference_name),"%s.rgb",name);
        uint8_t *reference=read_file(argv[1],reference_name,&reference_bytes);
        for (unsigned chunk=1;chunk<=4096;chunk*=16)
            assert(!large_jpeg(data,n,reference,reference_bytes,i?1199:320,i?2533:180,chunk));
        /* Fail every block/output/session allocation; every path returns all
         * charges, and a later fresh decoder still succeeds. */
        allocations=0;
        assert(!large_jpeg(data,n,reference,reference_bytes,i?1199:320,i?2533:180,4096));
        const size_t total_allocations=allocations;
        for (size_t failure=1;failure<=total_allocations;++failure) {
            allocations=0;fail_at=failure;
            assert(large_jpeg(data,n,reference,reference_bytes,i?1199:320,i?2533:180,4096));
        }
        fail_at=0;
        free(reference);free(data);
    }
    const size_t chunks[] = {1, 7, 1024, 4096};
    for (unsigned i = 0; i < 7; ++i) {
        int png = i < 5;
        size_t bytes;
        uint8_t *data = read_file(argv[1], names[i], &bytes), *expected = NULL;
        {
            char file[64];
            snprintf(file, sizeof(file), "%s.%s", names[i], png ? "rgba" : "rgb");
            size_t n;
            expected = read_file(argv[1], file, &n);
            assert(n == (png ? 320 * 180 * 4 : 64 * 36 * 3));
        }
        for (unsigned j = 0; j < 4; ++j)
            assert(!decode(data, bytes, chunks[j], png, expected));
        for (size_t end = 1; end < bytes; end += 97)
            assert(decode(data, end, 7, png, NULL));
        for (unsigned failure = 1; failure <= 16; ++failure) {
            allocations = 0;
            fail_at = failure;
            (void)decode(data, bytes, 1024, png, NULL);
            fail_at = 0;
        }
        uint32_t random = 42;
        mutated = 1;
        for (unsigned trial = 0; trial < 500; ++trial) {
            uint8_t *noise = malloc(bytes);
            assert(noise);
            memcpy(noise, data, bytes);
            for (unsigned j = 0; j < 3; ++j) {
                random = random * 1664525 + 1013904223;
                size_t at = random % bytes;
                noise[at] ^= (uint8_t)(1u << (random % 8));
            }
            (void)decode(noise, bytes, 1024, png, NULL);
            free(noise);
        }
        mutated = 0;
        free(expected);
        free(data);
    }
    const char *invalid[] = {"broken.png", "progressive.jpg", "unsupported.svg", "extra-pixels.png",
                             "metadata-before-header.png"};
    for (unsigned i = 0; i < 5; ++i) {
        size_t n;
        uint8_t *data = read_file(argv[1], invalid[i], &n);
        assert(decode(data, n, 7, i != 1, NULL));
        free(data);
    }
    /* A partially opened codec must be closed both on UNSUPPORTED (before
     * trying the next codec) and on a terminal error. decode checks charge=0. */
    const pxa_image_codec_t probe = {.open = probe_open, .write = probe_write, .finish = probe_finish, .close = probe_close};
    const pxa_image_codec_t *fallback[] = {&probe, &pxa_stream_png_codec};
    const pxa_image_backend_t candidates = {fallback, 2};
    backend = &candidates;
    size_t size;
    uint8_t *data = read_file(argv[1], "alpha.png", &size);
    probe_status = PXA_STATUS_UNSUPPORTED;
    assert(!decode(data, size, 7, 1, NULL) && probe_closed == 1);
    probe_status = PXA_STATUS_IO_ERROR;
    assert(decode(data, size, 7, 1, NULL) == probe_status && probe_closed == 2);
    free(data);
    /* Exercise the common LVGL adapter using an actual registered LodePNG
     * decoder as well, including channel order/alpha and a full native buffer. */
    const pxa_image_codec_t *native_codecs[] = {&pxa_lvgl_image_codec};
    const pxa_image_backend_t native = {native_codecs, 1};
    backend = &native;
    for (unsigned i = 0; i < 5; ++i) {
        size_t n, ref_n;
        uint8_t *data = read_file(argv[1], names[i], &n);
        char name[64];
        snprintf(name, sizeof(name), "%s.rgba", names[i]);
        uint8_t *expected = read_file(argv[1], name, &ref_n);
        assert(!decode(data, n, 1024, 1, expected));
        free(expected);
        free(data);
    }
    puts("streaming images PASS: pixels/alpha, split input, JPEG, truncation, allocation failure, "
         "mutation, lifetime");
}

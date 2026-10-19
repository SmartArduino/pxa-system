/* Optional POSIX microbenchmark: compressed input is loaded outside the
 * measurement window. Reports owned budget peaks, not libjpeg's private heap
 * or LCD FPS. Output checksums must match when comparing implementations. */
#undef NDEBUG
#include "pxa/image/codecs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static void *allocate(void *p, size_t n) {
    (void)p;
    return malloc(n);
}
static void release(void *p, void *m) {
    (void)p;
    free(m);
}
static double micros(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
int main(int argc, char **argv) {
    assert(argc >= 2 && argc <= 4);
    unsigned repeats=argc>2?(unsigned)strtoul(argv[2],NULL,10):100;
    unsigned chunk=argc>3?(unsigned)strtoul(argv[3],NULL,10):4096;
    assert(repeats>0 && repeats<=10000 && chunk>0 && chunk<=4096);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    size_t n = ftell(f);
    rewind(f);
    unsigned char *data = malloc(n);
    assert(fread(data, 1, n, f) == n);
    fclose(f);
    double write_us = 0, finish_us = 0, max_write_us=0;
    size_t temp_peak = 0, peak = 0;
    unsigned checksum = 0;
    for (unsigned repeat = 0; repeat <= repeats; ++repeat) {
        pxa_memory_budget_t b;
        pxa_memory_budget_config_t c = {0};
        c.limit[1] = 4 * 1024 * 1024;
        c.temporary_limit[1] = 2 * 1024 * 1024;
        assert(!pxa_memory_budget_init(&b, &c));
        pxa_memory_owner_t owner;
        assert(!pxa_memory_owner_open(&b, c.limit, &owner));
        pxa_memory_allocator_t pixels = {&b, owner,    1,      PXA_MEMORY_IMAGE,
                                         0,  allocate, release},
                               temporary = pixels;
        temporary.kind = PXA_MEMORY_TEMPORARY;
        pxa_image_decode_options_t o = {.max_width = 256,
                                        .max_height = 1024,
                                        .max_pixels = 32768,
                                        .max_encoded_bytes = 524288};
        pxa_image_decoder_t *d;
        assert(!pxa_image_decoder_create(&o, &pixels, &temporary,
                                         pxa_posix_image_backend(), &d));
        double t = micros();
        for (size_t at = 0; at < n;) {
            size_t size = n - at;
            if (size > chunk)
                size = chunk;
            double slice_start=micros();
            assert(!pxa_image_decoder_write(d, (pxa_bytes_t){data + at, size}));
            double elapsed=micros()-slice_start;
            if (repeat && elapsed>max_write_us) max_write_us=elapsed;
            at += size;
        }
        double end = micros();
        if (repeat)
            write_us += end - t;
        pxa_decoded_image_t image;
        assert(!pxa_image_decoder_finish(d, &image));
        if (repeat)
            finish_us += micros() - end;
        pxa_asset_object_view_t view;
        pxa_asset_object_view(image.image, &view);
        unsigned hash = 2166136261u;
        for (unsigned i = 0; i < view.bytes; ++i)
            hash = (hash ^ view.data[i]) * 16777619u;
        if (repeat)
            assert(checksum == hash);
        checksum = hash;
        pxa_image_decoder_destroy(d);
        pxa_asset_object_release(image.image);
        pxa_memory_stats_t s;
        assert(!pxa_memory_budget_stats(&b, owner, &s) && !s.charged[1]);
        temp_peak = s.temporary_peak[1];
        peak = s.peak[1];
        assert(!pxa_memory_owner_close(&b, owner));
    }
    printf("{\"bytes\":%zu,\"repeats\":%u,\"chunk\":%u,\"max_write_us\":%.3f,\"write_mean_us\":%.3f,\"finish_"
           "mean_us\":%.3f,\"temporary_peak\":%zu,\"charged_peak\":%zu,"
           "\"checksum\":%u}\n",
           n, repeats,chunk,max_write_us,write_us / repeats, finish_us / repeats, temp_peak, peak, checksum);
    free(data);
}

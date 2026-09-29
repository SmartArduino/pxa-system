/* Fixed visual workload, no VSync or pacing. stdout is machine-readable CSV.
 * Override the include with a saved pre-change product_runner.c to compare
 * identical workloads without reverting a dirty worktree. */
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#ifndef PXA_BENCH_RUNNER
#define PXA_BENCH_RUNNER "../product_runner.c"
#endif
#include PXA_BENCH_RUNNER
#include "../../../libpxa/tests/raster_snapshot_scenario.h"

int main(int argc, char **argv) {
    product_host_t *host = calloc(1, sizeof(*host));
    pxa_game_render_desc_t desc = {240, 240, 3, 0,
                                  PXA_GAME_RENDER_SCRATCH_NONE, 4096};
    uint8_t *texture = calloc(1, PXA_RASTER_UPLOAD_HEADER_BYTES + 65536);
    uint8_t palette[PXA_RASTER_UPLOAD_HEADER_BYTES + 512] = {0};
    uint8_t draw[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES +
                 4 * PXA_RASTER_SPRITE_BYTES] = {0};
    uint32_t frames = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 2000;
    uint64_t surface;
    uint32_t capabilities;
    uint32_t checksum = 2166136261u;
    assert(host && texture && frames > 0 && frames <= 100000);
#ifdef PRODUCT_RESOURCE_BUDGET
    assert(resource_memory_init(host, 128 * 1024, 2 * 1024 * 1024, 16 * 1024, 512 * 1024) == 0);
#endif
    lv_init();
    host->display = lv_display_create(240, 240);
    assert(host->display);
    host->width = host->height = 240;
    host->content_parent = lv_display_get_screen_active(host->display);
    assert(game_render_create(host, &desc, &surface, &capabilities) == 0);
    snapshot_upload_header(palette, PXA_RASTER_UPLOAD_PALETTE_RGB565, 256, 1);
    for (unsigned i = 0; i < 256; ++i)
        pxa_write_u16(palette + PXA_RASTER_UPLOAD_HEADER_BYTES + 2 * i,
                       (uint16_t)(i * 251));
    assert(surface_raster_upload(host, surface, palette, sizeof(palette)) == 0);
    for (unsigned slot = 0; slot < 20; ++slot) {
        snapshot_upload_header(texture, PXA_RASTER_UPLOAD_TEXTURE_INDEX8, 256, 256);
        texture[9] = (uint8_t)slot;
        for (unsigned i = 0; i < 65536; ++i)
            texture[PXA_RASTER_UPLOAD_HEADER_BYTES + i] =
                (uint8_t)((i ^ (i >> 8)) + slot * 37);
        assert(surface_raster_upload(host, surface, texture,
                                      PXA_RASTER_UPLOAD_HEADER_BYTES + 65536) == 0);
    }
    free(texture);
    pxa_write_u32(draw, PXA_RASTER_DRAW_MAGIC);
    pxa_write_u16(draw + 4, PXA_RASTER_ABI_MAJOR);
    pxa_write_u16(draw + 6, PXA_RASTER_ABI_MINOR);
    pxa_write_u32(draw + 8, sizeof(draw));
    pxa_write_u32(draw + 16, 5);
    draw[PXA_RASTER_DRAW_HEADER_BYTES] = PXA_RASTER_RECORD_CLEAR_RGB565;
    pxa_write_u16(draw + PXA_RASTER_DRAW_HEADER_BYTES + 2, PXA_RASTER_CLEAR_BYTES);
    for (unsigned slot = 0; slot < 4; ++slot) {
        uint8_t *r = draw + PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES +
                     slot * PXA_RASTER_SPRITE_BYTES;
        r[0] = PXA_RASTER_RECORD_SPRITE;
        r[4] = (uint8_t)slot;
        pxa_write_u16(r + 2, PXA_RASTER_SPRITE_BYTES);
        pxa_write_u16(r + 8, (uint16_t)((slot & 1) * 120));
        pxa_write_u16(r + 10, (uint16_t)((slot >> 1) * 120));
        pxa_write_u16(r + 12, 120);
        pxa_write_u16(r + 14, 120);
        pxa_write_u16(r + 20, 256);
        pxa_write_u16(r + 22, 256);
    }
    puts("frame,submit_us,execute_us,render_present_us,total_us");
    for (uint32_t i = 0; i < frames + 200; ++i) {
        uint64_t start, submitted, finished;
        pxa_write_u64(draw + 20, (uint64_t)i + 1);
        start = now_us(NULL);
        assert(surface_raster_submit(host, surface, draw, sizeof(draw)) == 0);
        submitted = now_us(NULL);
        assert(surface_process_pending(host));
        finished = now_us(NULL);
        if (i >= 200)
            printf("%u,%llu,%u,%llu,%llu\n", i - 200,
                   (unsigned long long)(submitted - start),
                   host->raster_telemetry.last_host_raster_us,
                   (unsigned long long)(finished - submitted),
                   (unsigned long long)(finished - start));
    }
    for (unsigned i = 0; i < host->surface_display_frame_bytes; ++i)
        checksum = (checksum ^ host->surface_display_buffer[i]) * 16777619u;
    fprintf(stderr, "240x240 INDEX8,20 resident textures,4 visible,checksum=%08x\n",
            checksum);
    surface_close(host, surface);
    lv_display_delete(host->display);
#ifdef PRODUCT_RESOURCE_BUDGET
    assert(resource_memory_end(host) == 0);
#endif
    free(host);
    return 0;
}

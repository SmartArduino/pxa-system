#include "pxa/raster_spans.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef PXA_TILE_TEST_THREADS
#include <pthread.h>
typedef struct { const pxa_raster_spans_t *batch; unsigned split; } worker_job;
static void *worker(void *arg) {
    const worker_job *j = arg;
    pxa_raster_spans_execute_range(j->batch, j->split, j->batch->target_bytes);
    return NULL;
}
#endif

enum { MEMORY = 65536, PACKET = 16384, PACKET_BYTES = 16384, COLOR = 64,
       PRIORITY = 4096, PIXELS = 1024, PITCH = 37 };
static uint32_t seed = 1;
static uint32_t next(void) { seed = seed * 1664525u + 1013904223u; return seed; }
static unsigned pick(unsigned n) { return (next() >> 8) % n; }
static uint8_t *actual, *expected, *initial;
static pxa_game_render_tiles_header_wire_t header;

static void make_packet(unsigned count) {
    for (unsigned i = 0; i < MEMORY; ++i) actual[i] = next() >> 24;
    header = (pxa_game_render_tiles_header_wire_t){PXA_GAME_RENDER_TILES_MAGIC, 1, 0,
        count, 32 + count * 24, COLOR, PRIORITY, PIXELS, PITCH};
    uint8_t *p = actual + PACKET;
    pxa_game_render_tiles_header_encode(p, 32, &header);
    for (unsigned i = 0; i < count; ++i) {
        unsigned x = pick(8), y = pick(8), colors = 1u << (2u << pick(3));
        pxa_game_render_tile_wire_t t = {0};
        t.source_x = x; t.source_y = y; t.width = 1 + pick(8 - x); t.height = 1 + pick(8 - y);
        t.destination = pick(17) * PITCH + pick(PITCH - 8);
        t.tile_offset = 4096 + i * 576; t.palette_offset = t.tile_offset + 64;
        t.palette_entries = colors; t.flags = pick(4);
        t.compare_depth = next() >> 24; t.write_depth = next() >> 24;
        for (unsigned j = 0; j < 64; ++j) p[t.tile_offset + j] = pick(colors);
        for (unsigned j = 0; j < colors; ++j)
            pxa_wire_generated_store_u16(p + t.palette_offset + j * 2, next() >> 8);
        pxa_game_render_tile_encode(p + 32 + i * 24, 24, &t);
    }
    memcpy(expected, actual, MEMORY); memcpy(initial, actual, MEMORY);
}
static void reference(void) {
    const uint8_t *p = expected + PACKET;
    for (unsigned i = 0; i < header.count; ++i) {
        pxa_game_render_tile_wire_t t;
        assert(pxa_game_render_tile_decode(p + 32 + i * 24, 24, &t));
        /* Walk source space, unlike the kernel's destination-space loops. */
        for (unsigned sy = 0; sy < 8; ++sy) for (unsigned sx = 0; sx < 8; ++sx) {
            unsigned dx = t.flags & 1 ? 7 - sx : sx, dy = t.flags & 2 ? 7 - sy : sy;
            if (dx < t.source_x || dx >= t.source_x + t.width ||
                dy < t.source_y || dy >= t.source_y + t.height) continue;
            unsigned at = t.destination + (dy - t.source_y) * header.pitch + dx - t.source_x;
            unsigned index = p[t.tile_offset + sy * 8 + sx];
            if (index && t.compare_depth > expected[PRIORITY + at]) {
                memcpy(expected + COLOR + at * 2, p + t.palette_offset + index * 2, 2);
                expected[PRIORITY + at] = t.write_depth;
            }
        }
    }
}
static void reject(void) {
    pxa_raster_spans_t b;
    memcpy(expected, actual, MEMORY);
    assert(pxa_raster_priority_tiles_validate(actual + PACKET, PACKET_BYTES,
        actual, MEMORY, 24, &b) < 0);
    assert(!memcmp(actual, expected, MEMORY));
    assert(!b.target && !b.count);
}
int main(void) {
    actual = malloc(MEMORY); expected = malloc(MEMORY); initial = malloc(MEMORY);
    assert(actual && expected && initial);
    for (unsigned trial = 0; trial < 1000; ++trial) {
        make_packet(1 + pick(20)); reference();
        pxa_raster_spans_t b;
        assert(pxa_raster_priority_tiles_validate(actual + PACKET, PACKET_BYTES,
            actual, MEMORY, 24, &b) == 0);
        unsigned split = trial < PIXELS ? trial : pick(PIXELS + 1);
        pxa_raster_spans_execute_range(&b, split, UINT32_MAX);
        pxa_raster_spans_execute_range(&b, 0, split);
        assert(!memcmp(actual, expected, MEMORY));
        memcpy(actual, initial, MEMORY);
        pxa_raster_spans_execute_range(&b, 0, PIXELS);
        assert(!memcmp(actual, expected, MEMORY));
#ifdef PXA_TILE_TEST_THREADS
        if (trial % 5 == 0) {
            memcpy(actual, initial, MEMORY);
            worker_job job = {&b, split}; pthread_t thread;
            assert(pthread_create(&thread, NULL, worker, &job) == 0);
            pxa_raster_spans_execute_range(&b, 0, split);
            assert(pthread_join(thread, NULL) == 0);
            assert(!memcmp(actual, expected, MEMORY));
        }
#endif
    }
    make_packet(2);
    /* Late malformed commands must reject the entire batch. */
    const unsigned positions[] = {0, 4, 6, 8, 12, 16, 20, 24, 28,
        56, 60, 64, 68, 70, 71, 72, 73, 74, 77, 78};
    const uint32_t values[] = {0, 2, 1, 4097, 79, COLOR+1, COLOR, UINT32_MAX, 0,
        UINT32_MAX, 32, PACKET_BYTES-1, 3, 8, 0, 8, 0, 4, 1, 1};
    const unsigned widths[] = {4,2,2,4,4,4,4,4,4,4,4,4,2,1,1,1,1,1,1,2};
    for (unsigned i = 0; i < sizeof(positions)/sizeof(*positions); ++i) {
        memcpy(actual, initial, MEMORY);
        for (unsigned j = 0; j < widths[i]; ++j)
            actual[PACKET + positions[i] + j] = (uint8_t)(values[i] >> (j*8));
        reject();
    }
    memcpy(actual, initial, MEMORY);
    pxa_game_render_tile_wire_t t;
    pxa_game_render_tile_decode(actual+PACKET+56,24,&t);
    t.palette_entries=4; pxa_game_render_tile_encode(actual+PACKET+56,24,&t);
    actual[PACKET+t.tile_offset+63]=4; reject();
    memcpy(actual, initial, MEMORY);
    pxa_wire_generated_store_u32(actual+PACKET+16,PACKET); reject();
    memcpy(actual, initial, MEMORY);
    pxa_wire_generated_store_u32(actual+PACKET+20,PACKET); reject();
    memcpy(actual, initial, MEMORY);
    pxa_raster_spans_t b;
    assert(pxa_raster_priority_tiles_validate(actual+PACKET,31,actual,MEMORY,24,&b)<0);
    assert(pxa_raster_priority_tiles_validate(actual+PACKET,PACKET_BYTES,actual,MEMORY,1,&b)==PXA_STATUS_LIMIT_EXCEEDED);
    assert(pxa_raster_priority_tiles_validate(actual+PACKET,PACKET_BYTES,actual,MEMORY,24,&b)==0);
    assert(!pxa_raster_spans_partition_safe(&b, PIXELS+1,64));
    assert(!pxa_raster_spans_partition_safe(&b,0,3));
    /* Check both output planes, not just the priority-plane split. */
    unsigned aligned=(unsigned)(-(uintptr_t)b.target)&63u;
    int expected_safe=!(((uintptr_t)b.state.tiles.color+2u*aligned)&63u);
    assert(pxa_raster_spans_partition_safe(&b,aligned,64)==expected_safe);
    ++b.state.tiles.color;
    assert(!pxa_raster_spans_partition_safe(&b,aligned,64));
    /* Virtual 64-byte alignment is insufficient when a runtime promises
     * only an 8-byte-aligned linear-memory base. Test every such residue. */
    _Alignas(64) uint8_t planes[2048];
    for (unsigned residue=0;residue<64;residue+=8) {
        b.state.tiles.color=planes+residue;
        b.target=planes+1024+residue;
        assert(pxa_raster_spans_partition_safe(&b,64-residue,64)==(residue==0));
    }
    /* Different virtual residues can compensate for the two pixel sizes. */
    b.state.tiles.color=planes+16;b.target=planes+1024+8;
    assert(pxa_raster_spans_partition_safe(&b,56,64));
    free(actual); free(expected); free(initial);
    puts("priority tile reference/partitions, bounds, aliases and late failure passed");
}

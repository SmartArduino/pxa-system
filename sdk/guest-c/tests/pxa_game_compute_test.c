#include "../../testing/pxa_control_capture.h"
#include "pxa_game_render_compute.h"
#include "pxa_game_render_tiles.h"
#include <assert.h>
#include <string.h>

static uint8_t captured[1024];
static uint32_t captured_operation;
static int32_t failure;
static int short_reply, reserved;
int32_t pxa_test_control(const uint8_t *p, uint32_t n) {
    (void)p; (void)n; return PXA_STATUS_OK;
}
int32_t pxa_io(uint64_t handle, uint32_t op, uint8_t *p, uint32_t n) {
    assert(handle == 77);
    captured_operation = op;
    if (failure) return failure;
    if (op == PXA_GAME_RENDER_IO_BLIT_INDEXED_FRAME || op == PXA_GAME_RENDER_IO_BLIT_INDEXED_RECT || op == PXA_GAME_RENDER_IO_EXECUTE_PRIORITY_TILES) {
        assert(n <= sizeof(captured)); memcpy(captured, p, n);
    } else {
        assert(op == PXA_GAME_RENDER_IO_COMPUTE_TELEMETRY && n == 64);
        memset(p, 0, n); pxa_store_u64(p, UINT64_C(0x123456789));
        pxa_store_u64(p+24, 64000); pxa_store_u32(p+56, 256);
        pxa_store_u32(p+60, reserved);
    }
    return (int32_t)n-short_reply;
}
int main(void) {
    pxa_game_render_compute_telemetry_t stats;
    assert(pxa_game_render_query_compute_telemetry(77, &stats) == 0);
    assert(stats.batches == UINT64_C(0x123456789) && stats.pixels == 64000 && stats.max_commands == 256);
    short_reply = 1;
    assert(pxa_game_render_query_compute_telemetry(77, &stats) == PXA_STATUS_PROTOCOL_ERROR);
    assert(!stats.batches && !stats.max_commands);
    short_reply = 0; reserved = 1;
    assert(pxa_game_render_query_compute_telemetry(77, &stats) == PXA_STATUS_PROTOCOL_ERROR);
    reserved = 0;
    pxa_game_render_blit_wire_t blit = {0}, blit_read;
    blit.source_offset = 1234; blit.target_width = 320; blit.target_height = 240;
    blit.cursor_x = -4;
    assert(pxa_game_render_blit_indexed_frame(77, &blit) == 64);
    assert(captured_operation == 0x108);
    assert(pxa_game_render_blit_indexed_rect(77, &blit) == 64);
    assert(captured_operation == 0x109);
    assert(pxa_game_render_blit_decode(captured, 64, &blit_read));
    assert(blit_read.source_offset == 1234 && blit_read.cursor_x == -4);
    short_reply = 1;
    assert(pxa_game_render_blit_indexed_frame(77, &blit) == PXA_STATUS_PROTOCOL_ERROR);
    assert(pxa_game_render_blit_indexed_rect(77, &blit) == PXA_STATUS_PROTOCOL_ERROR);
    short_reply = 0;
    failure = PXA_STATUS_WOULD_BLOCK;
    assert(pxa_game_render_blit_indexed_rect(77, &blit) == failure);
    failure=0;
    uint8_t tile_storage[1024], pixels[64], palette[32];
    memset(pixels,1,sizeof(pixels));memset(palette,7,sizeof(palette));
    pxa_game_render_tile_batch_t tiles;
    pxa_game_render_tile_target_t target={4096,8192,1024,32};
    pxa_game_render_tile_batch_begin(&tiles,tile_storage,sizeof(tile_storage),target);
    assert(pxa_game_render_execute_priority_tiles(77,&tiles)==0);
    pxa_game_render_tile_wire_t tile={0}, decoded_tile;
    tile.tile_offset=pxa_game_render_tile_copy_source(&tiles,pixels,64);
    tile.palette_offset=pxa_game_render_tile_copy_source(&tiles,palette,32);
    tile.palette_entries=16;tile.width=tile.height=8;tile.compare_depth=9;tile.write_depth=3;
    assert(tile.tile_offset && tile.palette_offset && pxa_game_render_tile_append(&tiles,&tile));
    memset(pixels,0,sizeof(pixels));memset(palette,0,sizeof(palette));
    assert(pxa_game_render_execute_priority_tiles(77,&tiles)==1024);
    assert(captured_operation==0x10a && pxa_load_u32(captured)==PXA_GAME_RENDER_TILES_MAGIC);
    assert(pxa_load_u32(captured+12)==56 && captured[tile.tile_offset]==1 && captured[tile.palette_offset]==7);
    assert(pxa_game_render_tile_decode(captured+32,24,&decoded_tile) && decoded_tile.compare_depth==9);
    short_reply=1;assert(pxa_game_render_execute_priority_tiles(77,&tiles)==PXA_STATUS_PROTOCOL_ERROR);
    short_reply=0;failure=PXA_STATUS_UNSUPPORTED;
    assert(pxa_game_render_execute_priority_tiles(77,&tiles)==failure);failure=0;
    assert(!pxa_game_render_tile_reserve_source(&tiles,1024));
    assert(pxa_game_render_execute_priority_tiles(77,&tiles)==PXA_STATUS_LIMIT_EXCEEDED);
    pxa_game_render_tile_batch_begin(&tiles,tile_storage+1,sizeof(tile_storage)-1,target);
    uint32_t tile_at=pxa_game_render_tile_reserve_source(&tiles,64);
    assert(tile_at && !((uintptr_t)(tiles.bytes+tile_at)&3u));
    return 0;
}

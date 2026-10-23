#include "../../testing/pxa_control_capture.h"
#include "pxa_game_render_spans.h"
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
    if (op == PXA_GAME_RENDER_IO_EXECUTE_SPANS || op == PXA_GAME_RENDER_IO_EXECUTE_PROJECTED_SPANS || op == PXA_GAME_RENDER_IO_EXECUTE_FRAME_SPANS || op == PXA_GAME_RENDER_IO_BLIT_INDEXED_FRAME || op == PXA_GAME_RENDER_IO_BLIT_INDEXED_RECT || op == PXA_GAME_RENDER_IO_EXECUTE_PRIORITY_TILES) {
        assert(n <= sizeof(captured)); memcpy(captured, p, n);
    } else {
        assert(op == PXA_GAME_RENDER_IO_SPAN_TELEMETRY && n == 64);
        memset(p, 0, n); pxa_store_u64(p, UINT64_C(0x123456789));
        pxa_store_u64(p+24, 64000); pxa_store_u32(p+56, 256);
        pxa_store_u32(p+60, reserved);
    }
    return (int32_t)n-short_reply;
}
int main(void) {
    uint8_t bytes[120];
    pxa_game_render_span_batch_t batch;
    pxa_game_render_spans_header_wire_t header = {0};
    pxa_game_render_span_wire_t span = {0, 8, -65536, 0, 65536, 0}, decoded;
    header.target_offset = 4096; header.target_bytes = 32;
    pxa_game_render_span_batch_begin(&batch, bytes, sizeof(bytes), &header);
    assert(pxa_game_render_execute_spans(77, &batch) == 0);
    assert(pxa_game_render_span_append(&batch, &span));
    assert(pxa_game_render_execute_spans(77, &batch) == 96);
    assert(pxa_load_u32(captured+8) == 1);
    assert(pxa_game_render_span_decode(captured+72, 24, &decoded));
    assert(decoded.s == span.s && decoded.ds == span.ds);
    assert(pxa_game_render_span_append(&batch, &span));
    failure = PXA_STATUS_WOULD_BLOCK;
    assert(pxa_game_render_execute_spans(77, &batch) == failure);
    failure = 0;
    assert(!pxa_game_render_span_append(&batch, &span));
    assert(pxa_game_render_execute_spans(77, &batch) == PXA_STATUS_LIMIT_EXCEEDED);
    uint8_t projected_bytes[168];
    pxa_game_render_projection_wire_t projection = {0};
    pxa_game_render_projected_batch_t projected;
    pxa_game_render_row_wire_t row = {3, 4, 8, 0}, decoded_row;
    pxa_game_render_projected_batch_begin(&projected, projected_bytes, sizeof(projected_bytes), &header, &projection);
    assert(pxa_game_render_execute_projected_spans(77, &projected) == 0);
    assert(pxa_game_render_projected_append(&projected, &row));
    assert(pxa_game_render_execute_projected_spans(77, &projected) == 160);
    assert(pxa_load_u32(captured) == PXA_GAME_RENDER_PROJECTED_MAGIC);
    assert(pxa_game_render_row_decode(captured + 152, 8, &decoded_row));
    assert(decoded_row.x == 3 && decoded_row.y == 4 && decoded_row.count == 8);
    short_reply = 1;
    assert(pxa_game_render_execute_projected_spans(77, &projected) == PXA_STATUS_PROTOCOL_ERROR);
    short_reply = 0; failure = PXA_STATUS_WOULD_BLOCK;
    assert(pxa_game_render_execute_projected_spans(77, &projected) == failure);
    failure = 0;
    assert(pxa_game_render_projected_append(&projected, &row));
    assert(!pxa_game_render_projected_append(&projected, &row));
    assert(pxa_game_render_execute_projected_spans(77, &projected) == PXA_STATUS_LIMIT_EXCEEDED);
    pxa_game_render_span_telemetry_t stats;
    assert(pxa_game_render_query_span_telemetry(77, &stats) == 0);
    assert(stats.batches == UINT64_C(0x123456789) && stats.pixels == 64000 && stats.max_spans == 256);
    short_reply = 1;
    assert(pxa_game_render_query_span_telemetry(77, &stats) == PXA_STATUS_PROTOCOL_ERROR);
    assert(!stats.batches && !stats.max_spans);
    short_reply = 0; reserved = 1;
    assert(pxa_game_render_query_span_telemetry(77, &stats) == PXA_STATUS_PROTOCOL_ERROR);
    reserved = 0;
    uint8_t frame_bytes[512] = {0}, source[16]; memset(source, 37, sizeof(source));
    pxa_game_render_frame_batch_t frame;
    pxa_game_render_frame_begin(&frame, frame_bytes, sizeof(frame_bytes));
    assert(pxa_game_render_execute_frame_spans(77, &frame) == 0);
    uint32_t offset = pxa_game_render_frame_copy_source(&frame, source, sizeof(source));
    assert(offset && !((uintptr_t)(frame_bytes + offset) & 3u));
    assert(!memcmp(source, frame_bytes + offset, sizeof(source)));
    header.texture_offset = offset;
    assert(pxa_game_render_frame_append(&frame, &header, &projection, &row, 1));
    assert(pxa_game_render_execute_frame_spans(77, &frame) == 512);
    pxa_game_render_frame_header_wire_t frame_header;
    assert(pxa_game_render_frame_header_decode(captured, sizeof(captured), &frame_header));
    assert(frame_header.magic == PXA_GAME_RENDER_FRAME_MAGIC && frame_header.count == 1 && frame_header.command_bytes == 176);
    assert(pxa_load_u32(captured + 16) == PXA_GAME_RENDER_FRAME_MATERIAL_MAGIC);
    short_reply = 1;
    assert(pxa_game_render_execute_frame_spans(77, &frame) == PXA_STATUS_PROTOCOL_ERROR);
    short_reply = 0;
    assert(pxa_game_render_frame_append(&frame, &header, &projection, &row, 1));
    assert(pxa_game_render_frame_append(&frame, &header, &projection, &row, 1));
    assert(!pxa_game_render_frame_append(&frame, &header, &projection, &row, 1));
    assert(pxa_game_render_execute_frame_spans(77, &frame) == PXA_STATUS_LIMIT_EXCEEDED);
    pxa_game_render_frame_begin(&frame, frame_bytes + 1, sizeof(frame_bytes) - 1);
    offset = pxa_game_render_frame_copy_source(&frame, source, 3);
    assert(offset && !((uintptr_t)(frame.bytes + offset) & 3u));
    offset = pxa_game_render_frame_reserve_source(&frame, 4);
    assert(offset && !((uintptr_t)(frame.bytes + offset) & 3u));
    memset(frame.bytes + offset, 73, 4);
    assert(!pxa_game_render_frame_reserve_source(&frame, 512));
    assert(frame.status == PXA_STATUS_LIMIT_EXCEEDED && frame.bytes[offset] == 73);
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

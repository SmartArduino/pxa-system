#include "../../testing/pxa_control_capture.h"
#include <pxa/game_service.hpp>
#include <cassert>
#include <cstring>
static std::array<std::byte, 1024> captured;
static std::uint32_t captured_size;
static std::uint32_t captured_operation;
static int32_t io_error;
static bool short_reply;
extern "C" int32_t pxa_test_control(const uint8_t*, uint32_t) { return 0; }
extern "C" int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t* data, uint32_t size) {
    assert(handle == 77);
    captured_operation = operation;
    if (io_error) return io_error;
    if (operation == 0x108 || operation == 0x109 || operation == 0x10a) {
        assert(size <= captured.size());
        std::memcpy(captured.data(), data, size); captured_size = size;
    } else {
        assert(operation == 0x10c && size == 64);
        std::memset(data, 0, size); data[0] = 3; data[24] = 48; data[56] = 2;
    }
    return size - short_reply;
}
int main() {
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::game::RasterContext context(transport, 77, 2);
    auto stats=context.telemetry();assert(stats && stats->batches==3 && stats->max_commands==2);
    pxa::game::IndexedBlit blit{};
    blit.source_offset = 1234; blit.target_width = 320; blit.target_height = 240; blit.cursor_x = -4;
    assert(context.blit(blit) && captured_size == 64);
    assert(captured_operation == 0x108);
    assert(context.blit_rect(blit) && captured_size == 64);
    assert(captured_operation == 0x109);
    pxa::game::IndexedBlit read_blit{};
    assert(pxa::game::wire_compute::pxa_game_render_blit_decode(captured.data(), 64, &read_blit));
    assert(read_blit.source_offset == 1234 && read_blit.cursor_x == -4);
    short_reply = true;
    assert(context.blit(blit).error() == pxa::Error::protocol_error);
    assert(context.blit_rect(blit).error() == pxa::Error::protocol_error);
    short_reply = false;
    io_error = -7;
    assert(context.blit_rect(blit).error() == pxa::Error::would_block);
    io_error=0;
    std::array<std::byte,1024> tile_storage{};
    std::array<std::byte,64> pixels;pixels.fill(std::byte{1});
    std::array<std::byte,32> palette;palette.fill(std::byte{7});
    pxa::game::PriorityTileBatch tiles(tile_storage,{4096,8192,1024,32});
    assert(context.execute(tiles));
    pxa::game::PriorityTile tile{};
    auto tile_at=tiles.copy_source(pixels), palette_at=tiles.copy_source(palette);
    assert(tile_at && palette_at);tile.tile_offset=*tile_at;tile.palette_offset=*palette_at;
    tile.palette_entries=16;tile.width=tile.height=8;tile.compare_depth=9;tile.write_depth=3;
    assert(tiles.append(tile));pixels.fill(std::byte{});palette.fill(std::byte{});
    assert(context.execute(tiles));
    assert(captured_operation==0x10a && captured_size==1024);
    assert(pxa::wire::get32(captured.data())==PXA_GAME_RENDER_TILES_MAGIC);
    assert(pxa::wire::get32(captured.data()+12)==56);
    assert(captured[*tile_at]==std::byte{1} && captured[*palette_at]==std::byte{7});
    short_reply=true;assert(context.execute(tiles).error()==pxa::Error::protocol_error);short_reply=false;
    io_error=static_cast<int32_t>(pxa::Error::unsupported);
    assert(context.execute(tiles).error()==pxa::Error::unsupported);io_error=0;
    assert(tiles.append(tile) && tiles.append(tile));
    assert(context.execute(tiles).error()==pxa::Error::limit_exceeded);
    assert(tiles.reserve_source(1024).error()==pxa::Error::limit_exceeded);
    pxa::game::PriorityTileBatch odd_tiles(std::span(tile_storage).subspan(1),{4096,8192,1024,32});
    auto aligned=odd_tiles.reserve_source(64);
    assert(aligned && !((reinterpret_cast<std::uintptr_t>(tile_storage.data()+1)+*aligned)&3u));
    return 0;
}

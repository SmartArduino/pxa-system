#include "../../testing/pxa_control_capture.h"
#include <pxa/game_service.hpp>
#include <cassert>
#include <cstring>
static std::array<std::byte, 1024> captured;
static std::uint32_t captured_size;
static int32_t io_error;
static bool short_reply;
extern "C" int32_t pxa_test_control(const uint8_t*, uint32_t) { return 0; }
extern "C" int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t* data, uint32_t size) {
    assert(handle == 77);
    if (io_error) return io_error;
    if (operation == 0x104 || operation == 0x106 || operation == 0x107 || operation == 0x108) {
        assert(size <= captured.size());
        std::memcpy(captured.data(), data, size); captured_size = size;
    } else {
        assert(operation == 0x105 && size == 64);
        std::memset(data, 0, size); data[0] = 3; data[24] = 48; data[56] = 2;
    }
    return size - short_reply;
}
int main() {
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::game::RasterContext context(transport, 77, 2);
    std::array<std::byte, 120> bytes{};
    pxa::game::IndexedSpanHeader header{};
    header.target_offset = 4096; header.target_bytes = 32;
    pxa::game::SpanBatch batch(bytes, header);
    assert(batch.append({.destination = 0, .count = 8, .s = -65536, .ds = 65536}));
    assert(context.execute(batch));
    assert(captured_size == 96 && pxa::wire::get32(captured.data() + 8) == 1);
    pxa::game::IndexedSpan decoded{};
    assert(pxa::game::wire_spans::pxa_game_render_span_decode(captured.data() + 72, 24, &decoded));
    assert(decoded.s == -65536 && decoded.ds == 65536 && decoded.count == 8);
    assert(batch.append({.destination = 8, .count = 8}));
    assert(context.execute(batch) && captured_size == 120);
    auto stats = context.telemetry();
    assert(stats && stats->batches == 3 && stats->pixels == 48 && stats->max_spans == 2);
    io_error = -7;
    assert(context.execute(batch).error() == pxa::Error::would_block);
    io_error = 0;
    assert(!batch.append({.count = 1}));
    assert(context.execute(batch).error() == pxa::Error::limit_exceeded);
    std::array<std::byte, 168> projected_bytes{};
    pxa::game::SpanProjection projection{};
    pxa::game::ProjectedBatch projected(projected_bytes, header, projection);
    assert(context.execute(projected));
    assert(projected.append({.x = 3, .y = 4, .count = 8}));
    assert(context.execute(projected) && captured_size == 160);
    assert(pxa::wire::get32(captured.data()) == PXA_GAME_RENDER_PROJECTED_MAGIC);
    pxa::game::ProjectedRow row{};
    assert(pxa::game::wire_spans::pxa_game_render_row_decode(captured.data() + 152, 8, &row));
    assert(row.x == 3 && row.y == 4 && row.count == 8);
    short_reply = true;
    assert(context.execute(projected).error() == pxa::Error::protocol_error);
    short_reply = false;
    io_error = -7;
    assert(context.execute(projected).error() == pxa::Error::would_block);
    io_error = 0;
    assert(projected.append({.count = 1}));
    assert(!projected.append({.count = 1}));
    assert(context.execute(projected).error() == pxa::Error::limit_exceeded);
    std::array<std::byte, 512> frame_bytes{};
    std::array<std::byte, 16> source{}; source.fill(std::byte{37});
    pxa::game::FrameBatch frame(frame_bytes);
    assert(context.execute(frame));
    auto offset = frame.copy_source(source);
    assert(offset && !((reinterpret_cast<std::uintptr_t>(frame_bytes.data()) + *offset) & 3u));
    assert(!std::memcmp(frame_bytes.data() + *offset, source.data(), source.size()));
    header.texture_offset = *offset;
    const std::array<pxa::game::ProjectedRow, 1> rows{{{0, 0, 8, 0}}};
    assert(frame.append(header, projection, rows));
    assert(context.execute(frame));
    assert(captured_size == 512 && pxa::wire::get32(captured.data()) == PXA_GAME_RENDER_FRAME_MAGIC);
    assert(pxa::wire::get32(captured.data() + 12) == 176);
    short_reply = true;
    assert(context.execute(frame).error() == pxa::Error::protocol_error);
    short_reply = false;
    assert(frame.append(header, projection, rows));
    assert(frame.append(header, projection, rows));
    assert(!frame.append(header, projection, rows));
    assert(context.execute(frame).error() == pxa::Error::limit_exceeded);
    pxa::game::FrameBatch reserved_frame(std::span(frame_bytes).subspan(1));
    auto reserved_offset = reserved_frame.reserve_source(4);
    assert(reserved_offset && !((reinterpret_cast<std::uintptr_t>(frame_bytes.data() + 1) + *reserved_offset) & 3u));
    std::memset(frame_bytes.data() + 1 + *reserved_offset, 73, 4);
    assert(reserved_frame.reserve_source(512).error() == pxa::Error::limit_exceeded);
    assert(frame_bytes[1 + *reserved_offset] == std::byte{73});
    pxa::game::IndexedBlit blit{};
    blit.source_offset = 1234; blit.target_width = 320; blit.target_height = 240; blit.cursor_x = -4;
    assert(context.blit(blit) && captured_size == 64);
    pxa::game::IndexedBlit read_blit{};
    assert(pxa::game::wire_spans::pxa_game_render_blit_decode(captured.data(), 64, &read_blit));
    assert(read_blit.source_offset == 1234 && read_blit.cursor_x == -4);
    short_reply = true;
    assert(context.blit(blit).error() == pxa::Error::protocol_error);
    return 0;
}

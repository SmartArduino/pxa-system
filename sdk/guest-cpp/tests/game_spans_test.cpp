#include "../../testing/pxa_control_capture.h"
#include <pxa/game_service.hpp>
#include <cassert>
#include <cstring>
static std::array<std::byte, 168> captured;
static std::uint32_t captured_size;
static int32_t io_error;
static bool short_reply;
extern "C" int32_t pxa_test_control(const uint8_t*, uint32_t) { return 0; }
extern "C" int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t* data, uint32_t size) {
    assert(handle == 77);
    if (io_error) return io_error;
    if (operation == 0x104 || operation == 0x106) {
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
    return 0;
}

#pragma once

#include "core.hpp"
#include <bit>

namespace pxa::ui {

inline constexpr std::uint8_t pointer_phase_down = 0;
inline constexpr std::uint8_t pointer_phase_move = 1;
inline constexpr std::uint8_t pointer_phase_up = 2;
inline constexpr std::uint8_t pointer_phase_cancel = 3;

struct CanvasPointer {
    std::uint32_t surface = 0;
    std::uint32_t node = 0;
    std::uint32_t generation = 0;
    std::uint64_t timestamp_us = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint16_t flags = 0;
    std::uint16_t buttons = 0;
    std::uint8_t pointer_id = 0;
    std::uint8_t phase = 0;
};

// The result owns its scalars. Raw users must check the page generation;
// Canvas::on_pointer does that through Page's ordinary event routing.
inline Result<CanvasPointer> decode_pointer(const Event& event) noexcept {
    if (event.service != 3 || event.opcode != 0x8001 || event.token ||
        event.payload.size() != 36 || wire::get16(event.payload.data() + 12) != 7)
        return std::unexpected(Error::protocol_error);
    const auto* data = event.payload.data();
    CanvasPointer pointer;
    pointer.surface = wire::get32(data);
    pointer.node = wire::get32(data + 4);
    pointer.generation = wire::get32(data + 8);
    pointer.flags = wire::get16(data + 14);
    pointer.timestamp_us = wire::get64(data + 16);
    pointer.pointer_id = std::to_integer<std::uint8_t>(data[24]);
    pointer.phase = std::to_integer<std::uint8_t>(data[25]);
    pointer.buttons = wire::get16(data + 26);
    pointer.x = std::bit_cast<std::int32_t>(wire::get32(data + 28));
    pointer.y = std::bit_cast<std::int32_t>(wire::get32(data + 32));
    if (!pointer.surface || !pointer.node || !pointer.generation ||
        pointer.phase > pointer_phase_cancel)
        return std::unexpected(Error::protocol_error);
    return pointer;
}

} // namespace pxa::ui

#pragma once

#include "core.hpp"
#include <bit>

namespace pxa::ui {

enum class VolumeKey : std::uint32_t { up = 1, down = 2, up_released = 3, down_released = 4 };
struct VolumeKeyEvent {
    std::uint32_t surface, node, generation;
    std::uint64_t timestamp_us;
    VolumeKey key;
};

inline Result<VolumeKeyEvent> decode_volume_key(const Event& event) noexcept {
    if (event.service != 3 || event.opcode != 0x8001 || event.token ||
        event.payload.size() != 28 || wire::get16(event.payload.data() + 12) != 5)
        return std::unexpected(Error::protocol_error);
    const auto* data = event.payload.data();
    VolumeKeyEvent key{wire::get32(data), wire::get32(data + 4), wire::get32(data + 8),
                       wire::get64(data + 16), VolumeKey(wire::get32(data + 24))};
    if (!key.surface || !key.node || !key.generation ||
        key.key < VolumeKey::up || key.key > VolumeKey::down_released)
        return std::unexpected(Error::protocol_error);
    return key;
}

inline constexpr std::uint8_t pointer_phase_down = 0;
inline constexpr std::uint8_t pointer_phase_move = 1;
inline constexpr std::uint8_t pointer_phase_up = 2;
inline constexpr std::uint8_t pointer_phase_cancel = 3;

enum class PointerPhase : std::uint8_t { down, move, up, cancel };

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

    constexpr PointerPhase action() const noexcept {
        return static_cast<PointerPhase>(phase);
    }
    constexpr bool ended() const noexcept {
        return action() == PointerPhase::up || action() == PointerPhase::cancel;
    }
};

// One gesture owner, scoped to the subscribing surface/node/page generation.
// Compose independent captures for walking, looking, buttons, etc. No event
// storage or allocation: applications retain only the positions they need.
// Reset on background/unmount, where a final pointer event is not guaranteed.
class PointerCapture {
    friend class PressGesture;
public:
    [[nodiscard]] constexpr bool try_capture(const CanvasPointer& event) noexcept {
        if (active() || event.action() != PointerPhase::down ||
            !event.surface || !event.node || !event.generation) return false;
        surface_ = event.surface;
        node_ = event.node;
        generation_ = event.generation;
        pointer_id_ = event.pointer_id;
        return true;
    }

    [[nodiscard]] constexpr bool owns(const CanvasPointer& event) const noexcept {
        return active() && event.phase <= pointer_phase_cancel &&
            surface_ == event.surface && node_ == event.node &&
            generation_ == event.generation && pointer_id_ == event.pointer_id;
    }

    // Both UP and CANCEL release ownership. Inspect action() first when only
    // a completed UP should activate an action; cancellation is never a tap.
    constexpr bool release(const CanvasPointer& event) noexcept {
        if (!event.ended() || !owns(event)) return false;
        reset();
        return true;
    }

    [[nodiscard]] constexpr bool active() const noexcept { return generation_ != 0; }
    constexpr void reset() noexcept { generation_ = 0; }

private:
    std::uint32_t surface_ = 0;
    std::uint32_t node_ = 0;
    std::uint32_t generation_ = 0;
    std::uint8_t pointer_id_ = 0;
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

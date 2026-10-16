#pragma once

#include "core.hpp"
#include <bit>

namespace pxa::ui { class PressGesture; }

namespace pxa::input {

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

struct Pointer {
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
    friend class pxa::ui::PressGesture;
public:
    [[nodiscard]] constexpr bool try_capture(const Pointer& event) noexcept {
        if (active() || event.action() != PointerPhase::down ||
            !event.surface || !event.node || !event.generation) return false;
        surface_ = event.surface;
        node_ = event.node;
        generation_ = event.generation;
        pointer_id_ = event.pointer_id;
        return true;
    }

    [[nodiscard]] constexpr bool owns(const Pointer& event) const noexcept {
        return active() && event.phase <= pointer_phase_cancel &&
            surface_ == event.surface && node_ == event.node &&
            generation_ == event.generation && pointer_id_ == event.pointer_id;
    }

    // Both UP and CANCEL release ownership. Inspect action() first when only
    // a completed UP should activate an action; cancellation is never a tap.
    constexpr bool release(const Pointer& event) noexcept {
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
// AppRuntime and UI Page routing check this before invoking callbacks.
inline Result<Pointer> decode_pointer(const Event& event) noexcept {
    if (event.service != 3 || event.opcode != 0x8001 || event.token ||
        event.payload.size() != 36 || wire::get16(event.payload.data() + 12) != 7)
        return std::unexpected(Error::protocol_error);
    const auto* data = event.payload.data();
    Pointer pointer;
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

inline constexpr std::uint32_t controller_up = 1u << 0, controller_down = 1u << 1,
    controller_left = 1u << 2, controller_right = 1u << 3,
    controller_a = 1u << 4, controller_b = 1u << 5,
    controller_start = 1u << 6, controller_select = 1u << 7,
    controller_x = 1u << 8, controller_y = 1u << 9,
    controller_l = 1u << 10, controller_r = 1u << 11;
inline constexpr std::uint32_t controller_button_mask = 0xfff;

struct ControllerState {
    std::uint32_t surface{}, node{}, generation{}, buttons{};
    std::uint64_t timestamp_us{};
    std::uint8_t controller{};
    bool connected{};
};

// Optional, allocation-free state for one controller. Edges describe the last
// accepted update, so consume them in the event callback, not every frame.
class Controller {
public:
    explicit constexpr Controller(std::uint8_t id = 0) noexcept : id_(id) {}

    [[nodiscard]] constexpr bool update(const ControllerState& state) noexcept {
        if (state.controller != id_) return false;
        previous_ = buttons_;
        buttons_ = state.connected ? state.buttons & controller_button_mask : 0;
        connected_ = state.connected;
        return true;
    }

    // Clear held keys and edges when the app backgrounds or its page unmounts.
    constexpr void reset() noexcept {
        buttons_ = previous_ = 0;
        connected_ = false;
    }

    constexpr bool connected() const noexcept { return connected_; }
    constexpr std::uint32_t buttons() const noexcept { return buttons_; }
    // A mask matches if any of its buttons match.
    constexpr bool down(std::uint32_t mask) const noexcept { return (buttons_ & mask) != 0; }
    constexpr bool pressed(std::uint32_t mask) const noexcept {
        return (buttons_ & ~previous_ & mask) != 0;
    }
    constexpr bool released(std::uint32_t mask) const noexcept {
        return (previous_ & ~buttons_ & mask) != 0;
    }
    constexpr int horizontal() const noexcept {
        return int(down(controller_right)) - int(down(controller_left));
    }
    constexpr int vertical() const noexcept {
        return int(down(controller_down)) - int(down(controller_up));
    }
private:
    std::uint32_t buttons_{}, previous_{};
    std::uint8_t id_;
    bool connected_{};
};

inline Result<ControllerState> decode_controller(const Event& event) noexcept {
    if (event.service != 3 || event.opcode != 0x8001 || event.token || event.payload.size() != 32)
        return std::unexpected(Error::protocol_error);
    const auto* p = event.payload.data();
    // Offset 14 is the common event flags, not the controller data length.
    // The 32-byte envelope already establishes an eight-byte controller body.
    if (wire::get16(p + 12) != 10 || p[25] > std::byte{1} ||
        p[26] != std::byte{} || p[27] != std::byte{})
        return std::unexpected(Error::protocol_error);
    ControllerState out{wire::get32(p), wire::get32(p + 4), wire::get32(p + 8),
        wire::get32(p + 28), wire::get64(p + 16), std::to_integer<std::uint8_t>(p[24]),
        p[25] != std::byte{}};
    if (!out.surface || !out.node || !out.generation || (out.buttons & ~controller_button_mask) ||
        (!out.connected && out.buttons))
        return std::unexpected(Error::protocol_error);
    return out;
}

} // namespace pxa::input

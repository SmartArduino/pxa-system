#pragma once
#include "ui_input.hpp"
namespace pxa::ui {
inline constexpr std::uint32_t controller_up=1,controller_down=2,
    controller_left=4,controller_right=8,controller_a=16,controller_b=32,
    controller_start=64,controller_select=128;
struct ControllerState {
    std::uint32_t surface,node,generation,buttons;
    std::uint64_t timestamp_us;
    std::uint8_t controller;
    bool connected;
};
inline Result<ControllerState> decode_controller(const Event& event) noexcept {
    if(event.service!=3||event.opcode!=0x8001||event.token||event.payload.size()!=32)
        return std::unexpected(Error::protocol_error);
    auto* p=event.payload.data();
    // Offset 14 is the common event flags, not the controller data length.
    // The 32-byte envelope already establishes an eight-byte controller body.
    if(wire::get16(p+12)!=10||p[25]>std::byte{1}||p[26]!=std::byte{}||p[27]!=std::byte{})
        return std::unexpected(Error::protocol_error);
    ControllerState out{wire::get32(p),wire::get32(p+4),wire::get32(p+8),
        wire::get32(p+28),wire::get64(p+16),std::to_integer<std::uint8_t>(p[24]),p[25]!=std::byte{}};
    if(!out.surface||!out.node||!out.generation||(out.buttons&~255u)||(!out.connected&&out.buttons))
        return std::unexpected(Error::protocol_error);
    return out;
}
} // namespace pxa::ui

#pragma once
#include "events.hpp"
namespace pxa::ui {
enum class ResourcePressure : std::uint8_t { normal, constrained, critical };
}
namespace pxa {
template<> struct EventTraits<ui::ResourcePressure> {
    static constexpr std::uint16_t service = 3, opcode = 0x8003;
};
}
namespace pxa::ui {
// Optional wrapper for the existing notification: no Context state or new ABI.
inline Result<ResourcePressure> decode_resource_pressure(const Event& event) noexcept {
    if (!event.is<ResourcePressure>() || event.token || event.payload.size() != 4 ||
        std::to_integer<unsigned>(event.payload[0]) > 2 || event.payload[1] != std::byte{} ||
        event.payload[2] != std::byte{} || event.payload[3] != std::byte{})
        return std::unexpected(Error::protocol_error);
    return static_cast<ResourcePressure>(std::to_integer<std::uint8_t>(event.payload[0]));
}
}

#pragma once
#include "ui_display.hpp"
#include "task.hpp"
#include "service_wire.hpp"

namespace pxa::ui {
// Optional snapshot: applications that only need geometry pay no storage cost.
struct UiAppearance {
    bool dark = false;
    std::uint32_t generation = 0;
    std::array<std::uint32_t, 10> colors{}; // Same roles as ui::Color.
    std::array<std::uint16_t, 6> typography{};
    constexpr bool operator==(const UiAppearance&) const = default;
};
inline Result<UiAppearance> decode_theme_snapshot(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() != 60) return std::unexpected(Error::protocol_error);
    auto p = bytes.data();
    if (!wire::get32(p) || std::to_integer<unsigned>(p[4]) > 1 ||
        p[5] != std::byte{0} || p[6] != std::byte{0} || p[7] != std::byte{0})
        return std::unexpected(Error::protocol_error);
    UiAppearance out; out.dark = p[4] == std::byte{1}; out.generation = wire::get32(p);
    for (unsigned i = 0; i < out.colors.size(); ++i) out.colors[i] = wire::get32(p + 8 + 4 * i);
    for (unsigned i = 0; i < out.typography.size(); ++i) {
        out.typography[i] = wire::get16(p + 48 + 2 * i);
        if (!out.typography[i]) return std::unexpected(Error::protocol_error);
    }
    return out;
}
inline Result<UiAppearance> decode_start_appearance(std::span<const std::byte> config) noexcept {
    auto environment = pxa::detail::configuration_record(config, 8);
    if (!environment) return std::unexpected(environment.error());
    auto geometry = decode_display_metrics(*environment);
    if (!geometry) return std::unexpected(geometry.error());
    auto scheme = pxa::detail::configuration_record(*environment, 7);
    if (!scheme || scheme->size() != 1 || std::to_integer<unsigned>((*scheme)[0]) > 1)
        return std::unexpected(Error::protocol_error);
    UiAppearance out; out.dark = (*scheme)[0] == std::byte{1};
    return out; // Palette is retrieved with ThemeService::get, not invented.
}
inline Result<UiAppearance> decode_appearance(const Event& event) noexcept {
    if (event.service != 3 || event.opcode != 0x8006 || event.token)
        return std::unexpected(Error::protocol_error);
    return decode_theme_snapshot(event.payload);
}
class ThemeService {
public:
    ThemeService(Transport& transport, RequestTable& requests) noexcept : transport_(transport), requests_(requests) {}
    Task<UiAppearance> get(this ThemeService self) {
        auto& [transport, requests] = self;
        auto event = co_await Response(transport, requests, 3, 11, std::span<const std::byte>{});
        if (!event) co_return std::unexpected(event.error());
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        co_return decode_theme_snapshot(*body);
    }
private:
    Transport& transport_;
    RequestTable& requests_;
};
} // namespace pxa::ui

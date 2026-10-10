#pragma once
#include "ui.hpp"
#include "ui_theme.hpp"

namespace pxa::ui {
namespace detail {
template<class T> concept EnvironmentValue = std::same_as<T, DisplayMetrics> ||
    std::same_as<T, UiCapabilities> || std::same_as<T, UiAppearance>;
template<class T> struct EnvironmentSlot { State<T> value{T{}}; };
template<class... T> inline constexpr bool unique_environment_types = true;
template<class T, class... Rest> inline constexpr bool unique_environment_types<T, Rest...> =
    ((!std::same_as<T, Rest>) && ...) && unique_environment_types<Rest...>;
template<class T> Result<T> start_environment(std::span<const std::byte> config) {
    if constexpr (std::same_as<T, DisplayMetrics>) return decode_start_display(config);
    else if constexpr (std::same_as<T, UiCapabilities>) return decode_start_ui_capabilities(config);
    else return decode_start_appearance(config);
}
}

// Only requested snapshots are stored; neither Context nor a default application
// acquires caches/subscriptions. Keep this model alive longer than its UI Page.
template<detail::EnvironmentValue Primary = DisplayMetrics, detail::EnvironmentValue... Extra>
    requires detail::unique_environment_types<Primary, Extra...>
class Environment : private detail::EnvironmentSlot<Primary>, private detail::EnvironmentSlot<Extra>... {
    template<class T> static constexpr bool contains = std::same_as<T, Primary> || (std::same_as<T, Extra> || ...);
public:
    template<class T> requires contains<T>
    State<T>& state() noexcept { return static_cast<detail::EnvironmentSlot<T>&>(*this).value; }
    State<DisplayMetrics>& display() noexcept requires contains<DisplayMetrics> { return state<DisplayMetrics>(); }
    State<UiAppearance>& appearance() noexcept requires contains<UiAppearance> { return state<UiAppearance>(); }
    State<UiCapabilities>& capabilities() noexcept requires contains<UiCapabilities> { return state<UiCapabilities>(); }

    Result<void> initialize(std::span<const std::byte> config) {
        // Validate all selected fields before publishing any State change.
        auto values = std::tuple(detail::start_environment<Primary>(config),
                                 detail::start_environment<Extra>(config)...);
        std::optional<Error> failure;
        auto check = [&](const auto& value) { if (!value && !failure) failure = value.error(); };
        std::apply([&](const auto&... value) { (check(value), ...); }, values);
        if (failure) return std::unexpected(*failure);
        std::apply([&](auto&... value) { (state<typename std::remove_reference_t<decltype(value)>::value_type>().set(*value), ...); }, values);
        return {};
    }
    Result<bool> update(const Event& event) {
        if (event.service != protocol::service) return false;
        if (event.opcode == 0x8002) {
            if constexpr (contains<DisplayMetrics> || contains<UiCapabilities>) {
                auto metrics = decode_display_metrics(event);
                if (!metrics) return std::unexpected(metrics.error());
                if constexpr (contains<UiCapabilities>) {
                    auto capabilities = decode_ui_capabilities(event);
                    if (!capabilities) return std::unexpected(capabilities.error());
                    state<UiCapabilities>().set(*capabilities);
                }
                if constexpr (contains<DisplayMetrics>) state<DisplayMetrics>().set(*metrics);
                return true;
            }
        } else if (event.opcode == 0x8006) {
            if constexpr (contains<UiAppearance>) {
                auto value = decode_appearance(event);
                if (!value) return std::unexpected(value.error());
                state<UiAppearance>().set(*value);
                return true;
            }
        }
        return false;
    }
};
} // namespace pxa::ui

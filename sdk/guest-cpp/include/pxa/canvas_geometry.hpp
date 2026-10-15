#pragma once

#include "canvas.hpp"
#include "ui_geometry.hpp"
#include <limits>

namespace pxa::ui {
// Both insets are absolute distances from the panel edge. Backgrounds may
// fill the panel; interactive controls must also avoid system input strips.
inline PixelRect interactive_rectangle(const DisplayMetrics &metrics, Insets occupied) noexcept {
    auto display = metrics;
    display.safe.left = std::max(display.safe.left, occupied.left);
    display.safe.top = std::max(display.safe.top, occupied.top);
    display.safe.right = std::max(display.safe.right, occupied.right);
    display.safe.bottom = std::max(display.safe.bottom, occupied.bottom);
    return safe_rectangle(display);
}
namespace detail {
inline std::int32_t canvas_coordinate(std::int64_t pixels, std::uint32_t density,
                                      bool upper) noexcept {
    const std::int64_t divisor = density ? density : 65536;
    const auto scaled = pixels * 65536;
    auto coordinate = scaled / divisor;
    const auto remainder = scaled % divisor;
    if (upper && remainder > 0)
        ++coordinate;
    if (!upper && remainder < 0)
        --coordinate;
    return static_cast<std::int32_t>(
        std::clamp(coordinate, std::int64_t(std::numeric_limits<std::int32_t>::min()),
                   std::int64_t(std::numeric_limits<std::int32_t>::max())));
}
} // namespace detail

// Canvas command coordinates are logical pixels; DisplayMetrics/safe_rectangle
// and explicit font sizes are Surface pixels. Convert edges separately so
// fractional DPI never removes a pixel from a clip or background rectangle.
inline CanvasRegion surface_to_canvas_region(PixelRect pixels,
                                             const DisplayMetrics &metrics) noexcept {
    const auto x = detail::canvas_coordinate(pixels.x, metrics.density_q16, false);
    const auto y = detail::canvas_coordinate(pixels.y, metrics.density_q16, false);
    const auto right = detail::canvas_coordinate(std::int64_t(pixels.x) + std::max(0, pixels.width),
                                                 metrics.density_q16, true);
    const auto bottom = detail::canvas_coordinate(
        std::int64_t(pixels.y) + std::max(0, pixels.height), metrics.density_q16, true);
    return {x, y,
            pixels.width <= 0 ? 0
                              : static_cast<std::int32_t>(std::min<std::int64_t>(
                                    std::max<std::int64_t>(0, std::int64_t(right) - x), INT32_MAX)),
            pixels.height <= 0
                ? 0
                : static_cast<std::int32_t>(std::min<std::int64_t>(
                      std::max<std::int64_t>(0, std::int64_t(bottom) - y), INT32_MAX))};
}

inline std::int32_t surface_to_canvas_coordinate(std::int32_t pixels,
                                                 const DisplayMetrics &metrics) noexcept {
    return detail::canvas_coordinate(pixels, metrics.density_q16, false);
}

// Positive widths/radii must round up independently of their position. At
// fractional DPI, rounding a one-pixel stroke down can make it disappear when
// the Host scales logical coordinates back to Surface pixels.
inline std::int32_t surface_to_canvas_length(std::int32_t pixels,
                                             const DisplayMetrics &metrics) noexcept {
    return detail::canvas_coordinate(std::max(0, pixels), metrics.density_q16, true);
}
} // namespace pxa::ui

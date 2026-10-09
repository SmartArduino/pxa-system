#pragma once
#include "game.hpp"

namespace pxa::game {
// Optional pre-build backpressure. Count rendering and pending lists together;
// completed and replaced lists are retired. No cache or additional queue.
// Two outstanding lists preserve one-frame overlap with the rasterizer.
inline bool can_build_frame(const Telemetry& telemetry,
                            std::uint64_t maximum_outstanding = 2) noexcept {
    if (!maximum_outstanding) return false;
    if (telemetry.submitted_frames <= telemetry.rendered_frames) return true;
    const auto outstanding = telemetry.submitted_frames - telemetry.rendered_frames;
    if (outstanding <= telemetry.dropped_frames) return true;
    return outstanding - telemetry.dropped_frames < maximum_outstanding;
}
} // namespace pxa::game

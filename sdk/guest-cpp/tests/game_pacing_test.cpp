#include <pxa/game_pacing.hpp>
#include <cassert>
#include <limits>
extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { return 0; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return 0; }

int main() {
    pxa::game::Telemetry t;
    assert(pxa::game::can_build_frame(t));
    t.submitted_frames = 1;
    assert(pxa::game::can_build_frame(t));
    t.submitted_frames = 2;
    assert(!pxa::game::can_build_frame(t));
    t.rendered_frames = 1;
    assert(pxa::game::can_build_frame(t));
    t.submitted_frames = 100;
    t.rendered_frames = 40;
    t.dropped_frames = 59;
    assert(pxa::game::can_build_frame(t));
    t.dropped_frames = 58;
    assert(!pxa::game::can_build_frame(t));
    // Avoid overflow when reading cumulative counters close to their limit.
    t.submitted_frames = std::numeric_limits<std::uint64_t>::max();
    t.rendered_frames = t.submitted_frames - 1;
    t.dropped_frames = 0;
    assert(pxa::game::can_build_frame(t));
    assert(!pxa::game::can_build_frame(t, 1));
    assert(!pxa::game::can_build_frame(t, 0));
    t.rendered_frames = t.submitted_frames;
    assert(pxa::game::can_build_frame(t));
}

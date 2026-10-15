#include "../../testing/pxa_control_capture.h"
#include <pxa/ui_gesture.hpp>
#include <cassert>
extern "C" std::int32_t pxa_test_control(const std::uint8_t*, std::uint32_t) { return 0; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }
using namespace pxa::ui;
using namespace std::chrono_literals;
constexpr bool check() {
    PressGesture press;
    CanvasPointer e{.surface=1,.node=2,.generation=3,.timestamp_us=100,
                    .x=10,.y=20,.pointer_id=17,.phase=pointer_phase_down};
    if (press.update(e)!=PressResult::none || !press.active() || press.deadline()!=550100) return false;
    auto foreign=e; ++foreign.pointer_id;
    (void)press.update(foreign);
    foreign.phase=pointer_phase_cancel;
    (void)press.update(foreign);
    if (!press.active() || press.advance(550099) || !press.advance(550100) || press.advance(900000)) return false;
    e.phase=pointer_phase_up;e.timestamp_us=900000;
    if (press.update(e)!=PressResult::none || press.active()) return false;
    e.phase=pointer_phase_down;e.timestamp_us=1000000;
    (void)press.update(e);
    e.phase=pointer_phase_move;e.timestamp_us+=10;e.x+=9;
    (void)press.update(e);
    e.x-=9;(void)press.update(e);
    if (!press.dragged() || press.deadline() || press.advance(2000000)) return false;
    e.phase=pointer_phase_up;e.timestamp_us=2000000;
    if (press.update(e)!=PressResult::none) return false;
    e.phase=pointer_phase_down;(void)press.update(e);
    e.phase=pointer_phase_up;e.timestamp_us+=1;
    if (press.update(e)!=PressResult::tap) return false;
    e.phase=pointer_phase_down;(void)press.update(e);
    e.phase=pointer_phase_up;e.timestamp_us+=550000;
    if (press.update(e)!=PressResult::long_press) return false; // delayed clock
    e.phase=pointer_phase_down;(void)press.update(e,{.long_press=0us});
    if (press.deadline({.long_press=0us}) || press.advance(UINT64_MAX,{.long_press=0us})) return false;
    e.phase=pointer_phase_up;e.timestamp_us+=9000000;
    if (press.update(e,{.long_press=0us})!=PressResult::tap) return false;
    e.phase=pointer_phase_down;e.x=INT32_MIN;(void)press.update(e);
    e.phase=pointer_phase_move;e.x=INT32_MAX;(void)press.update(e);
    if (!press.dragged()) return false;
    press.reset();e.phase=pointer_phase_down;e.timestamp_us=UINT64_MAX-100;
    (void)press.update(e);
    if (press.deadline()!=UINT64_MAX || press.advance(UINT64_MAX)) return false;
    press.rebase(4);e.generation=4;
    if (!press.owns(e)) return false;
    e.phase=pointer_phase_cancel;(void)press.update(e);
    if (press.active()) return false;
    e.phase=pointer_phase_down;e.timestamp_us=100;
    (void)press.update(e);
    e.phase=pointer_phase_up;e.timestamp_us=99;
    if (press.update(e)!=PressResult::none || !press.active()) return false;
    (void)press.update(e,{.slop=-1});
    return !press.active() && !press.advance(UINT64_MAX,{.long_press=-1us});
}
static_assert(check());
int main() { assert(check()); }

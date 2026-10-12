#include "../../testing/pxa_control_capture.h"
#include <pxa/ui.hpp>

#include <array>
#include <cassert>

extern "C" std::int32_t pxa_test_control(const std::uint8_t*, std::uint32_t) { return 0; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }

constexpr bool capture_two_fingers() {
    using namespace pxa::ui;
    PointerCapture walk, look;
    CanvasPointer first{.surface = 1, .node = 2, .generation = 3,
                        .pointer_id = 17, .phase = pointer_phase_down};
    auto second = first;
    second.pointer_id = 255;
    if (!walk.try_capture(first) || !look.try_capture(second)) return false;
    if (walk.try_capture(second) || walk.owns(second) || look.owns(first)) return false;
    first.phase = pointer_phase_move;
    first.x = 250; // Crossing a control boundary does not change ownership.
    if (!walk.owns(first) || walk.release(first)) return false;
    auto unrelated = first;
    ++unrelated.node;
    if (walk.owns(unrelated)) return false;
    unrelated = first; ++unrelated.surface;
    if (walk.owns(unrelated)) return false;
    unrelated = first; ++unrelated.generation;
    if (walk.owns(unrelated)) return false;
    unrelated = first; unrelated.phase = 4;
    if (walk.owns(unrelated)) return false;
    first.phase = pointer_phase_up;
    if (!first.ended() || !walk.release(first) || walk.release(first) ||
        walk.active() || !look.owns(second)) return false;
    second.phase = pointer_phase_cancel;
    if (!second.ended() || !look.release(second) || look.active()) return false;
    if (look.try_capture(second)) return false;
    second.phase = pointer_phase_down;
    if (!walk.try_capture(second)) return false; // ID reuse after release.
    walk.reset(); // Background/unmount without an UP.
    if (walk.active() || walk.owns(second)) return false;
    second.generation = 0;
    return !walk.try_capture(second);
}
static_assert(sizeof(pxa::ui::PointerCapture) == 16);
static_assert(capture_two_fingers());

int main() {
    using namespace pxa::ui;
    pxa::Transport transport;
    transport.phase(pxa::Phase::start);
    unsigned calls = 0;
    CanvasPointer saved;
    auto page = Page(transport, Canvas().on_pointer(
        [counter = &calls, result = &saved](const CanvasPointer& pointer) {
            ++*counter;
            *result = pointer;
        }));
    static_assert(decltype(page)::binding_capacity == 0);
    static_assert(decltype(page)::handler_capacity == 1);
    assert(page.mount());
    std::array<std::byte, 36> payload{};
    pxa::wire::put32(payload.data(), 1);
    pxa::wire::put32(payload.data() + 4, 2);
    pxa::wire::put32(payload.data() + 8, page.generation());
    pxa::wire::put16(payload.data() + 12, 7);
    pxa::wire::put16(payload.data() + 14, 5);
    pxa::wire::put64(payload.data() + 16, 123456789);
    payload[24] = std::byte{17};
    payload[25] = std::byte{pointer_phase_move};
    pxa::wire::put16(payload.data() + 26, 3);
    pxa::wire::put32(payload.data() + 28, static_cast<std::uint32_t>(-12));
    pxa::wire::put32(payload.data() + 32, 240);
    pxa::Event event{3, 0x8001, 0, payload};
    assert(page.handle(event) && calls == 1);
    assert(saved.x == -12 && saved.y == 240 && saved.pointer_id == 17);
    assert(saved.flags == 5 && saved.buttons == 3 && saved.timestamp_us == 123456789);
    for (unsigned phase = 0; phase <= pointer_phase_cancel; ++phase) {
        payload[25] = std::byte(phase);
        assert(page.handle(event));
    }
    const auto before = calls;
    payload[25] = std::byte{4};
    assert(!page.handle(event));
    payload[25] = std::byte{pointer_phase_up};
    auto truncated = event;
    truncated.payload = std::span{payload}.first(35);
    assert(!page.handle(truncated));
    auto wrong = event;
    wrong.service = 4;
    assert(!page.handle(wrong));
    wrong = event; wrong.token = 1;
    assert(!page.handle(wrong));
    pxa::wire::put32(payload.data() + 8, page.generation() + 1);
    assert(!page.handle(event));
    pxa::wire::put32(payload.data() + 8, page.generation());
    pxa::wire::put32(payload.data() + 4, 99);
    assert(!page.handle(event) && calls == before);
    payload.fill(std::byte{});
    assert(saved.x == -12 && saved.y == 240); // No borrowed event storage.
}

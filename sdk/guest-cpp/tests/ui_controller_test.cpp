#include "../../testing/pxa_control_capture.h"
#include <pxa/ui_controller.hpp>
#include <pxa/ui.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>

using namespace pxa;
using namespace pxa::ui;

static unsigned allocations;
void* operator new(std::size_t size) { ++allocations; return std::malloc(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

extern "C" std::int32_t pxa_test_control(const std::uint8_t*, std::uint32_t) { assert(false); return -1; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }

static_assert(sizeof(Controller) == 12 && std::is_trivially_copyable_v<Controller>);
static_assert(sizeof(CanvasView<>) == 2 * sizeof(std::int32_t));
static_assert([] {
    Controller pad{2};
    ControllerState state{.buttons = controller_a | controller_left, .controller = 2, .connected = true};
    if (!pad.update(state) || !pad.connected() || !pad.pressed(controller_a | controller_b) ||
        pad.down(0) || pad.pressed(0) || pad.released(0) ||
        pad.horizontal() != -1 || pad.vertical() != 0) return false;
    if (!pad.update(state) || pad.pressed(controller_a)) return false;
    state.buttons = controller_right | controller_down;
    if (!pad.update(state) || !pad.released(controller_a) || !pad.pressed(controller_right) ||
        pad.horizontal() != 1 || pad.vertical() != 1) return false;
    state.controller = 1;
    state.buttons = 0;
    if (pad.update(state) || !pad.down(controller_right)) return false;
    state.controller = 2;
    state.buttons = controller_up | controller_down | controller_left | controller_right;
    if (!pad.update(state) || pad.horizontal() || pad.vertical()) return false;
    state.connected = false;
    if (!pad.update(state) || pad.connected() || pad.buttons() ||
        !pad.released(controller_down)) return false;
    if (!pad.update(state) || pad.released(controller_down)) return false;
    state.connected = true;
    state.buttons = controller_x;
    if (!pad.update(state) || !pad.pressed(controller_x)) return false;
    pad.reset();
    return !pad.connected() && !pad.buttons() && !pad.pressed(controller_x) &&
        !pad.released(controller_x) && pad.update(state) && pad.pressed(controller_x);
}());

struct ControllerEvent {
    std::array<std::byte, 32> bytes{};
    ControllerEvent(std::uint32_t node = 2, std::uint32_t generation = 1) {
        wire::put32(bytes.data(), 1);
        wire::put32(bytes.data() + 4, node);
        wire::put32(bytes.data() + 8, generation);
        wire::put16(bytes.data() + 12, protocol::event_controller_kind);
        wire::put16(bytes.data() + 14, 1); // Flags are not a payload length.
        wire::put64(bytes.data() + 16, 1234567);
        bytes[25] = std::byte{1};
    }
    Event event() const { return {3, 0x8001, 0, bytes}; }
};

static void decoding() {
    ControllerEvent sample;
    const std::array buttons{controller_up, controller_down, controller_left, controller_right,
        controller_a, controller_b, controller_start, controller_select,
        controller_x, controller_y, controller_l, controller_r};
    for (unsigned i = 0; i < buttons.size(); ++i) {
        assert(buttons[i] == (1u << i));
        wire::put32(sample.bytes.data() + 28, buttons[i]);
        auto state = decode_controller(sample.event());
        assert(state && state->buttons == buttons[i] && state->surface == 1 &&
            state->node == 2 && state->generation == 1 && state->connected &&
            state->controller == 0 && state->timestamp_us == 1234567);
    }
    wire::put32(sample.bytes.data() + 28, controller_button_mask);
    assert(decode_controller(sample.event()));
    wire::put32(sample.bytes.data() + 28, 1u << 12);
    assert(!decode_controller(sample.event()));
    wire::put32(sample.bytes.data() + 28, controller_a);
    sample.bytes[25] = std::byte{};
    assert(!decode_controller(sample.event())); // Disconnected with held keys.
    wire::put32(sample.bytes.data() + 28, 0);
    auto disconnected = decode_controller(sample.event());
    assert(disconnected && !disconnected->connected);
    sample.bytes[25] = std::byte{2};
    assert(!decode_controller(sample.event()));
    sample.bytes[25] = std::byte{1};
    for (unsigned offset : {26u, 27u}) {
        sample.bytes[offset] = std::byte{1};
        assert(!decode_controller(sample.event()));
        sample.bytes[offset] = std::byte{};
    }
    for (unsigned offset : {0u, 4u, 8u, 12u}) {
        auto invalid = sample;
        wire::put32(invalid.bytes.data() + offset, 0);
        assert(!decode_controller(invalid.event()));
    }
    auto invalid = sample.event(); invalid.service = 4; assert(!decode_controller(invalid));
    invalid = sample.event(); invalid.opcode = 1; assert(!decode_controller(invalid));
    invalid = sample.event(); invalid.token = 1; assert(!decode_controller(invalid));
    for (std::size_t length = 0; length < 32; ++length) {
        invalid = sample.event(); invalid.payload = invalid.payload.first(length);
        assert(!decode_controller(invalid));
    }
    std::array<std::byte, 33> oversized{};
    invalid = sample.event(); invalid.payload = oversized; assert(!decode_controller(invalid));
}

int main() {
    const auto before = allocations;
    decoding();
    assert(allocations == before);
    std::puts("Controller codec/state: 12 keys, edges, disconnect, zero allocation OK");
}

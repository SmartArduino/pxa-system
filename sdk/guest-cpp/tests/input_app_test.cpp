#include "../../testing/pxa_control_capture.h"
#include <pxa/input.hpp>
#include <pxa/app.hpp>
#include <pxa/ui_volume_keys.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

using namespace pxa;
static unsigned allocations, nodes, canvases, controller_calls, pointer_calls, volume_calls, frames;
static unsigned button_calls, ui_volume_calls, draws;
static std::uint64_t root_mask;
static std::uint32_t generation, button_node;
static bool fail_commit;
void* operator new(std::size_t n) { ++allocations; return std::malloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

extern "C" std::int32_t pxa_test_control(const std::uint8_t* data, std::uint32_t size) {
    const auto* p = reinterpret_cast<const std::byte*>(data);
    const auto service = wire::get16(p), opcode = wire::get16(p + 2);
    if (service == 3 && opcode == 1) root_mask = 0;
    if (service == 3 && opcode == 2) {
        for (std::size_t i = 24; i < size;) {
            const auto length = wire::get16(p + i + 2);
            assert(i + 4 + length <= size);
            if (p[i] == std::byte{ui::protocol::create}) {
                ++nodes;
                if (p[i + 16] == std::byte{ui::protocol::canvas}) ++canvases;
                if (p[i + 16] == std::byte{ui::protocol::control}) button_node = wire::get32(p + i + 4);
            }
            if (p[i] == std::byte{ui::protocol::set_property} &&
                wire::get32(p + i + 4) == 1 && wire::get16(p + i + 8) == ui::protocol::event_mask)
                root_mask = wire::get64(p + i + 10);
            i += length + 4;
        }
    }
    if (service == 3 && opcode == 3) {
        if (fail_commit) return std::int32_t(Error::resource_limit);
        generation = wire::get32(p + 20);
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t size) {
    ++draws;
    return std::int32_t(size);
}

using Dispatch = std::int32_t (*)(const std::uint8_t*, std::uint32_t);
static std::int32_t dispatch(Dispatch target, std::uint16_t kind, std::uint32_t value = 0,
                             std::uint32_t node = 1, std::uint32_t gen = 0) {
    std::array<std::byte, 56> bytes{};
    wire::put16(bytes.data(), 3); wire::put16(bytes.data() + 2, 0x8001);
    const auto length = kind == 7 ? 36u : kind == 10 ? 32u : 28u;
    wire::put32(bytes.data() + 12, length);
    auto* p = bytes.data() + 20;
    wire::put32(p, 1); wire::put32(p + 4, node);
    wire::put32(p + 8, gen ? gen : generation); wire::put16(p + 12, kind);
    wire::put64(p + 16, 123456);
    if (kind == 10) { p[25] = std::byte{1}; wire::put32(p + 28, value); }
    else if (kind == 7) { wire::put32(p + 28, 40); wire::put32(p + 32, 50); }
    else wire::put32(p + 24, value);
    return target(reinterpret_cast<const std::uint8_t*>(bytes.data()), 20 + length);
}
static void foreground(Dispatch target, bool enabled) {
    std::array<std::byte, 21> bytes{};
    wire::put16(bytes.data(), 17); wire::put16(bytes.data() + 2, 0x8005);
    wire::put32(bytes.data() + 12, 1); bytes[20] = std::byte(enabled);
    assert(target(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()) == 1);
}

struct Game {
    input::Controller controller;
    std::optional<game::Renderer> renderer;
    game::DrawBuffer<128> commands;
    Result<void> on_start(Context& ctx) {
        renderer.emplace(ctx.transport(), 77, 0);
        return {};
    }
    void on_controller(Context& ctx, const input::ControllerState& state) {
        assert(ctx.foreground() && state.node == 1 && state.timestamp_us == 123456);
        if (controller.update(state) && controller.pressed(input::controller_a)) ++controller_calls;
    }
    void on_pointer(Context&, const input::Pointer& pointer) {
        assert(pointer.x == 40 && pointer.y == 50); ++pointer_calls;
    }
    void on_volume_key(Context&, const input::VolumeKeyEvent& key) {
        assert(key.key == input::VolumeKey::up); ++volume_calls;
    }
    void on_background(Context&) { controller.reset(); }
    void on_frame(Context&, game::FrameTick) {
        auto frame = renderer->frame(commands);
        frame.clear({0x07e0}); assert(frame.submit()); ++frames;
    }
};
template<class T> concept HasView = requires(T& app) { app.view(); };
static_assert(!HasView<Game>);
static_assert(sizeof(input::Controller) == 12);
static_assert(sizeof(UiSlot<Game>) == sizeof(std::uint32_t));

struct UiApp {
    ui::State<bool> volume{true};
    static inline UiApp* current;
    UiApp() { current = this; }
    auto view() {
        return ui::CaptureVolumeKeys(ui::Button("Go").on_click([] { ++button_calls; }), volume,
            [](const input::VolumeKeyEvent&) { ++ui_volume_calls; });
    }
    void on_controller(Context&, const input::ControllerState&) { ++controller_calls; }
    void on_pointer(Context&, const input::Pointer&) { ++pointer_calls; }
};
struct Screen { auto view() { return ui::Text("Route"); } };
struct NavigationApp {
    ui::Navigator<4> navigator;
    Result<void> on_start(Context&) { return navigator.push<Screen>(); }
    auto& navigation() { return navigator; }
    void on_controller(Context&, const input::ControllerState&) {
        ++controller_calls; assert(navigator.replace<Screen>());
    }
};
struct PlainApp {};
static_assert(detail::AppInput<PlainApp>::mask == 0);
static_assert(std::is_empty_v<UiSlot<PlainApp>>);

int main() {
    const auto before = allocations;
    using Runtime = AppRuntime<Game>;
    assert(Runtime::start(nullptr, 0) == 0);
    assert(nodes == 1 && canvases == 0 && root_mask == ((1u << 9) | (1u << 6) | (1u << 4)));
    assert(dispatch(Runtime::event, 10, input::controller_a) == 1 && controller_calls == 1);
    assert(dispatch(Runtime::event, 10, input::controller_a) == 1 && controller_calls == 1);
    assert(dispatch(Runtime::event, 7) == 1 && pointer_calls == 1);
    assert(dispatch(Runtime::event, 5, 1) == 1 && volume_calls == 1);
    assert(dispatch(Runtime::event, 10, 1u << 12) == std::int32_t(Error::protocol_error));
    assert(dispatch(Runtime::event, 10, 0, 2) == 0);
    assert(dispatch(Runtime::event, 10, 0, 1, generation + 1) == 0);
    foreground(Runtime::event, false);
    assert(dispatch(Runtime::event, 10, input::controller_a) == 1 && controller_calls == 1);
    assert(dispatch(Runtime::event, 7) == 1 && pointer_calls == 1);
    foreground(Runtime::event, true);
    assert(dispatch(Runtime::event, 10, input::controller_a) == 1 && controller_calls == 2);
    std::array<std::byte, 28> tick{};
    wire::put16(tick.data(), 4); wire::put16(tick.data() + 2, 0x8001);
    wire::put32(tick.data() + 12, 8); wire::put64(tick.data() + 20, 1000000);
    assert(Runtime::event(reinterpret_cast<const std::uint8_t*>(tick.data()), tick.size()) == 1);
    assert(frames == 1 && draws == 1);
    Runtime::stop(0);
    assert(dispatch(Runtime::event, 10) == std::int32_t(Error::bad_state));
    fail_commit = true;
    assert(Runtime::start(nullptr, 0) == std::int32_t(Error::resource_limit));
    fail_commit = false;
    assert(Runtime::start(nullptr, 0) == 0);
    assert(dispatch(Runtime::event, 10, input::controller_a) == 1 && controller_calls == 3);
    Runtime::stop(0);

    using UiRuntime = AppRuntime<UiApp>;
    assert(UiRuntime::start(nullptr, 0) == 0);
    assert(root_mask == ((1u << 9) | (1u << 6) | (1u << 4)));
    assert(dispatch(UiRuntime::event, 5, 1) == 1 && ui_volume_calls == 1);
    assert(dispatch(UiRuntime::event, 1, 0, button_node) == 1 && button_calls == 1);
    UiApp::current->volume.set(false);
    assert(dispatch(UiRuntime::event, 10) == 1 && controller_calls == 4);
    assert(root_mask == ((1u << 9) | (1u << 6))); // Reactive volume mask preserves app input.
    assert(dispatch(UiRuntime::event, 7) == 1 && pointer_calls == 2);
    UiRuntime::stop(0);

    using NavRuntime = AppRuntime<NavigationApp>;
    assert(NavRuntime::start(nullptr, 0) == 0 && root_mask == (1u << 9));
    auto old = generation;
    assert(dispatch(NavRuntime::event, 10) == 1 && controller_calls == 5);
    assert(generation > old && root_mask == (1u << 9));
    assert(dispatch(NavRuntime::event, 10, 0, 1, old) == 0 && controller_calls == 5);
    assert(dispatch(NavRuntime::event, 10) == 1 && controller_calls == 6);
    NavRuntime::stop(0);
    auto old_nodes = nodes;
    assert(AppRuntime<PlainApp>::start(nullptr, 0) == 0 && nodes == old_nodes);
    AppRuntime<PlainApp>::stop(0);
    assert(allocations == before && canvases == 0);
    std::puts("Application input: GameRender without view/Canvas, controller/pointer/volume, UI/navigation masks, lifecycle, zero heap OK");
}

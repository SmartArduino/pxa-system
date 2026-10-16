#include <pxa/app.hpp>
#include <algorithm>
#include <optional>

// Pure GameRender: there is no view(), Canvas, or UI callback wiring.
struct Game {
    pxa::input::Controller controller;
    std::optional<pxa::game::Renderer> renderer;
    pxa::game::DrawBuffer<1024> commands;
    int x = 72, y = 52;
    bool alternate = false, loading = false;

    pxa::Task<void> initialize(pxa::Context& context) {
        pxa::game::RenderOptions options;
        options.width = 160;
        options.height = 120;
        options.direct_scanout = true;
        auto created = co_await context.game().create(options);
        loading = false;
        if (!created) co_return std::unexpected(created.error());
        renderer.emplace(std::move(*created));
        co_return pxa::Result<void>{};
    }
    void on_foreground(pxa::Context& context) {
        if (renderer || loading) return;
        loading = true;
        auto started = context.foreground_tasks().start(initialize(context));
        if (!started) loading = false;
    }
    void on_background(pxa::Context&) {
        controller.reset();
        loading = false;
    }
    void on_controller(pxa::Context&, const pxa::input::ControllerState& state) {
        if (!controller.update(state)) return;
        if (controller.pressed(pxa::input::controller_a)) alternate = !alternate;
        if (controller.pressed(pxa::input::controller_b)) { x = 72; y = 52; }
    }
    void on_pointer(pxa::Context&, const pxa::input::Pointer& pointer) {
        if (pointer.action() == pxa::input::PointerPhase::down) alternate = !alternate;
    }
    void on_update(pxa::Context&, std::uint32_t) {
        x = std::clamp(x + 2 * controller.horizontal(), 0, 144);
        y = std::clamp(y + 2 * controller.vertical(), 16, 104);
    }
    void on_frame(pxa::Context&, pxa::game::FrameTick) {
        if (!renderer) return;
        auto frame = renderer->frame(commands);
        frame.clear({0x0841});
        auto rectangle = [&](int left, int top, int width, int height, std::uint16_t color) {
            const auto l = std::int16_t(left * 16), r = std::int16_t((left + width) * 16);
            const auto t = std::int16_t(top * 16), b = std::int16_t((top + height) * 16);
            frame.quad({l, t, r, t, r, b, l, b}, {color});
        };
        rectangle(x, y, 16, 16, alternate ? 0xffe0 : 0x07e0);
        for (unsigned i = 0; i < 12; ++i)
            rectangle(2 + int(i) * 13, 2, 10, 8, controller.down(1u << i) ? 0xffff : 0x4208);
        (void)frame.submit();
    }
};

PXA_GAME(Game)

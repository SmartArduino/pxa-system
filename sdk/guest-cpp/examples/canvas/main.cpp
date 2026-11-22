#include <pxa/app.hpp>
#include <cstdio>

struct CanvasApp {
    pxa::ui::CanvasRef canvas;
    pxa::ui::CanvasCommands<480> commands;
    pxa::input::Controller controller;
    int taps = 0;
    auto view() {
        return pxa::ui::Canvas(canvas)
            .on_pointer([this](const pxa::ui::CanvasPointer& pointer) {
                if (pointer.phase == pxa::ui::pointer_phase_down) { ++taps; draw(); }
            });
    }
    void on_controller(pxa::Context&, const pxa::input::ControllerState& state) {
        if (controller.update(state) && controller.pressed(pxa::input::controller_a)) {
            ++taps;
            draw();
        }
    }
    void draw() {
        char label[48];
        const auto length = std::snprintf(label, sizeof(label), "Touch / A: %d", taps);
        commands.reset();
        commands.rect(0, 0, 296, 240, 0x102030ff)
                .rect(30, 70, 100, 80, 0x36bdeaff, 12, 2, 0xffffffff)
                .circle(200, 110, 32, 0xffcc40ff, 2, 0xffffffff)
                .ellipse(164, 150, 72, 16, 0x36bdeaff)
                .line(30, 180, 260, 180, 0xffcc40ff, 3)
                .text_box({20, 12, 256, 40}, 0xffffffff,
                          {label, static_cast<std::size_t>(length)}, 1, 1, 1);
        (void)canvas.present(commands);
    }
    void on_foreground(pxa::Context&) { draw(); }
    void on_background(pxa::Context&) { controller.reset(); }
};
PXA_APPLICATION(CanvasApp)

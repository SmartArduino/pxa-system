#include <pxa/app.hpp>
#include <cstdio>

struct CanvasApp {
    pxa::ui::CanvasRef canvas;
    pxa::ui::CanvasCommands<480> commands;
    int taps = 0;
    auto view() {
        return pxa::ui::Canvas(canvas).on_pointer([this](const pxa::ui::CanvasPointer& pointer) {
            if (pointer.phase == pxa::ui::pointer_phase_down) { ++taps; draw(); }
        });
    }
    void draw() {
        char label[48];
        const auto length = std::snprintf(label, sizeof(label), "Canvas taps: %d", taps);
        commands.reset();
        commands.rect(0, 0, 296, 240, 0x102030ff)
                .rect(30, 70, 100, 80, 0x36bdeaff, 12)
                .line(30, 180, 260, 180, 0xffcc40ff, 3)
                .text(20, 20, 270, 0xffffffff, {label, static_cast<std::size_t>(length)});
        (void)canvas.present(commands);
    }
    void on_foreground(pxa::Context&) { draw(); }
};
PXA_APPLICATION(CanvasApp)

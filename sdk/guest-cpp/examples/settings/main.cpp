#include <pxa/app.hpp>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct Settings {
    State<std::string> status{std::string("Active")};
    Ref<std::string> name{std::string("Player")};
    Ref<bool> sound{true};
    State<int> brightness{60};

    auto view() {
        return Column(
            Text("Settings").font(Font::title),
            Text(status),
            Toggle("Sound", sound),
            Text("Brightness"),
            Slider(brightness, 0, 100, 5),
            Progress(brightness),
            Text("Name"),
            TextInput(name)
        ).gap(6_dp).padding(12_dp).fill();
    }

    void on_background(pxa::Context&) { status.set("Paused"); }
    void on_foreground(pxa::Context&) { status.set("Active"); }
};

PXA_APPLICATION(Settings)

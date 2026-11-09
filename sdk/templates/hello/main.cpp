#include <pxa/app.hpp>
#include <pxa/ui_component.hpp>
#include <pxa/ui_environment.hpp>
#include <pxa/ui_layout.hpp>

using namespace pxa;
using namespace pxa::ui;
using namespace pxa::ui::literals;

struct Hello {
    Environment<> environment;
    State<int> count{0};

    Result<void> on_start(Context& ctx, std::span<const std::byte> configuration) {
        auto result = environment.initialize(configuration);
        if (!result) return result;
        WindowConfig window;
        window.edge_to_edge = true;
        return ctx.window().configure(window);
    }
    Result<bool> on_event(Context&, const Event& event) {
        return environment.update(event);
    }
    auto view() {
        return SafeArea(environment.display(), Column(
            Text("Hello PXA").font(Font::title),
            Text(count).font(Font::headline),
            Button("+1").on_click([this] {
                count.update([](int value) { return value + 1; });
            }).height(44_dp).fill().justify(Justify::center)
        ).gap(12_dp).fill()).padding(12_dp).background(Color::background);
    }
};

PXA_APPLICATION(Hello)

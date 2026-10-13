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

    Result<void> on_start(Context&, std::span<const std::byte> configuration) {
        return environment.initialize(configuration);
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
            }).height(44_dp).fill()
        ).gap(12_dp).fill()).padding(12_dp);
    }
};

PXA_APPLICATION(Hello)

#include <pxa/app.hpp>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct ConditionalDemo {
    State<bool> expanded{false};
    State<int> count{0};

    auto view() {
        return Column(
            Text<"Conditional UI">().font(Font::title),
            Toggle("Show counter", expanded),
            When(expanded,
                [this] {
                    return Column(
                        Text(count).font(Font::headline),
                        Button("Add one").on_click([this] {
                            count.update([](int value) { return value + 1; });
                        })
                    ).gap(8_dp);
                },
                [] { return Text("Counter hidden"); })
        ).gap(12_dp).padding(16_dp);
    }
};

PXA_APPLICATION(ConditionalDemo)

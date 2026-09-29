#include <pxa/app.hpp>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct Counter {
    State<int> count{0};

    auto view() {
        return Column(
            Text("Counter").font(Font::title),
            Text(count).font(Font::headline),
            Button("Add one").on_click([this] {
                count.update([](int value) { return value + 1; });
            })
        ).gap(12_dp).padding(16_dp);
    }
};

PXA_APPLICATION(Counter)

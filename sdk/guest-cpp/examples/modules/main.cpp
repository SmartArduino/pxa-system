#include <pxa/app.hpp>
#include "model.hpp"

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct App {
    Model model;
    auto view() {
        return Column(
            Text<"Modules">().font(Font::title),
            Text(model.count),
            Button("Add").on_click([this] { (void)model.add(1); })
        ).gap(12_dp).padding(16_dp);
    }
};
PXA_APPLICATION(App)

#include <pxa/app.hpp>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct Model {
    State<int> count{0};
    Navigator<> routes;
};

struct Details {
    Model& model;
    explicit Details(Model& value) : model(value) {}
    auto view() {
        return Column(
            Text("Details").font(Font::title),
            Text(model.count).font(Font::headline),
            Button("Add one").on_click([this] {
                model.count.update([](int n) { return n + 1; });
            }),
            Button("Back").on_click([this] { (void)model.routes.pop(); })
        ).gap(12_dp).padding(16_dp);
    }
};

struct Home {
    Model& model;
    explicit Home(Model& value) : model(value) {}
    auto view() {
        return Column(
            Text("Home").font(Font::title),
            Text(model.count).font(Font::headline),
            Button("Details").on_click([this] {
                (void)model.routes.push<Details>(std::ref(model));
            })
        ).gap(12_dp).padding(16_dp);
    }
};

struct NavigationApp {
    Model model;
    Navigator<>& navigation() noexcept { return model.routes; }
    pxa::Result<void> on_start(pxa::Context&) {
        return model.routes.push<Home>(std::ref(model));
    }
    pxa::BackAction on_back() {
        if (!model.routes.can_pop()) return pxa::BackAction::close;
        (void)model.routes.pop();
        return pxa::BackAction::stay;
    }
};

PXA_APPLICATION(NavigationApp)

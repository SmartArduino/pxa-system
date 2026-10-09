#include <pxa/app.hpp>

#include <array>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct StorageApp {
    State<int> count{0};
    State<std::string> status{"Loading"};
    pxa::Context* context = nullptr;

    pxa::Task<void> load() {
        auto result = co_await context->storage().get_value<std::int32_t>("count");
        if (result) {
            count.set(*result);
            status.set("Loaded");
        } else if (!result && result.error() == pxa::Error::not_found) {
            status.set("New save");
        } else {
            status.set("Load failed");
        }
        co_return pxa::Result<void>{};
    }

    pxa::Task<void> save() {
        auto result = co_await context->storage().set_value("count", std::int32_t(count.get()));
        status.set(result ? "Saved" : "Save failed");
        co_return pxa::Result<void>{};
    }

    pxa::Result<void> on_start(pxa::Context& ctx) {
        context = &ctx;
        return ctx.tasks().start(load());
    }

    auto view() {
        return Column(
            Text("Storage").font(Font::title),
            Text(count).font(Font::headline),
            Button("Add and save").on_click([this] {
                count.update([](int current) { return current + 1; });
                status.set("Saving");
                auto started = context->tasks().start(save());
                if (!started) status.set("Save failed");
            }),
            Text(status)
        ).gap(12_dp).padding(16_dp);
    }
};

PXA_APPLICATION(StorageApp)

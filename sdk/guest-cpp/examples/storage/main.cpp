#include <pxa/app.hpp>

#include <array>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct StorageApp {
    State<int> count{0};
    State<std::string> status{"Loading"};
    pxa::Context* context = nullptr;

    pxa::Task<void> load() {
        std::array<std::byte, 4> data{};
        auto result = co_await context->storage().get("count", data);
        if (result && *result == data.size()) {
            count.set(static_cast<int>(pxa::wire::get32(data.data())));
            status.set("Loaded");
        } else if (!result && result.error() == pxa::Error::not_found) {
            status.set("New save");
        } else {
            status.set("Load failed");
        }
        co_return pxa::Result<void>{};
    }

    pxa::Task<void> save() {
        std::array<std::byte, 4> data{};
        pxa::wire::put32(data.data(), static_cast<std::uint32_t>(count.get()));
        auto result = co_await context->storage().set("count", data);
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

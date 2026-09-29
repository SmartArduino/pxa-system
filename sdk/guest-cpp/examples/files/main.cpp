#include <pxa/app.hpp>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct FilesApp {
    State<int> count{0};
    State<std::string> status{"Loading"};
    pxa::Context* context = nullptr;
    bool busy = true;

    pxa::Task<void> load() {
        auto file = co_await context->fs().open("count.bin");
        if (!file) {
            status.set(file.error() == pxa::Error::not_found ? "New file" : "Open failed");
        } else {
            std::array<std::byte, 4> bytes{};
            auto read = file->read(bytes);
            if (read && *read == bytes.size()) {
                count.set(static_cast<int>(pxa::wire::get32(bytes.data())));
                status.set("Loaded");
            } else {
                status.set("Read failed");
            }
        }
        busy = false;
        co_return pxa::Result<void>{};
    }

    pxa::Task<void> save() {
        auto file = co_await context->fs().open("count.bin",
            pxa::OpenMode::write | pxa::OpenMode::create | pxa::OpenMode::truncate);
        if (!file) {
            status.set("Open failed");
        } else {
            std::array<std::byte, 4> bytes{};
            pxa::wire::put32(bytes.data(), static_cast<std::uint32_t>(count.get()));
            auto written = file->write(bytes);
            status.set(written && *written == bytes.size() ? "Saved" : "Write failed");
        }
        busy = false;
        co_return pxa::Result<void>{};
    }

    pxa::Result<void> on_start(pxa::Context& ctx) {
        context = &ctx;
        return ctx.tasks().start(load());
    }
    auto view() {
        return Column(
            Text("Files").font(Font::title),
            Text(count).font(Font::headline),
            Button("Add and save").on_click([this] {
                if (busy) return;
                busy = true;
                count.update([](int value) { return value + 1; });
                status.set("Saving");
                auto started = context->tasks().start(save());
                if (!started) { busy = false; status.set("Save failed"); }
            }),
            Text(status)
        ).gap(12_dp).padding(16_dp);
    }
};

PXA_APPLICATION(FilesApp)

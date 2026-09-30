#include <pxa/app.hpp>

#include <stats_generated.hpp>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct StatsApp {
    State<int> count{0};
    State<std::string> status{std::string("Ready")};
    pxa::Context* context = nullptr;
    pxa::IpcCallBuffers<stats::Get> buffers;
    bool busy = false;

    pxa::Result<void> on_start(pxa::Context& ctx) {
        context = &ctx;
        return {};
    }

    pxa::Task<void> fetch(pxa::Context& ctx, stats::Get::Request request) {
        auto result = co_await ctx.ipc().call<stats::Get>(request, buffers);
        if (result && result->valid) {
            count.set(static_cast<int>(result->next));
            status.set(std::string(result->label.view()));
        } else {
            status.set("Unavailable");
        }
        busy = false;
        co_return pxa::Result<void>{};
    }

    void request() {
        if (busy || !context) return;
        stats::Get::Request input{};
        input.seed = static_cast<std::uint32_t>(count.get());
        if (!input.label.set("From service")) return;
        busy = true;
        status.set("Loading");
        auto started = context->tasks().start(fetch(*context, std::move(input)));
        if (!started) {
            busy = false;
            status.set("Unavailable");
        }
    }

    auto view() {
        return Column(
            Text<"IPC Stats">().font(Font::title),
            Text(count).font(Font::headline),
            Text(status),
            Button("Request").on_click([this] { request(); })
        ).gap(12_dp).padding(16_dp);
    }
};

PXA_APPLICATION(StatsApp)

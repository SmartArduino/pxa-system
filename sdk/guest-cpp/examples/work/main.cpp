#include <pxa/app.hpp>

#include <array>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct WorkApp {
    State<std::string> status{"Ready"};
    pxa::Context* context = nullptr;
    std::uint32_t last_id = 0;
    bool busy = false;

    pxa::Task<void> schedule() {
        constexpr std::array<std::byte, 1> input{std::byte{1}};
        auto queued = co_await context->work().enqueue({
            .worker = "sync.job", .input = input,
            .retry_delay_ms = 1000, .max_attempts = 3});
        if (queued) {
            last_id = queued->id;
            status.set("Queued");
        } else status.set("Queue failed");
        busy = false;
        co_return pxa::Result<void>{};
    }

    pxa::Task<void> cancel() {
        auto result = co_await context->work().cancel(last_id);
        if (result) { last_id = 0; status.set("Cancelled"); }
        else status.set("Cancel failed");
        busy = false;
        co_return pxa::Result<void>{};
    }

    void queue() {
        if (busy) return;
        busy = true;
        status.set("Queueing");
        if (!context->tasks().start(schedule())) {
            busy = false;
            status.set("Task capacity exceeded");
        }
    }

    void cancel_queued() {
        if (busy || !last_id) return;
        busy = true;
        if (!context->tasks().start(cancel())) {
            busy = false;
            status.set("Task capacity exceeded");
        }
    }

    pxa::Result<void> on_start(pxa::Context& ctx) {
        context = &ctx;
        return {};
    }

    auto view() {
        return Column(
            Text<"Deferred work">().font(Font::title),
            Text(status),
            Row(
                Button("Queue").on_click([this] { queue(); }),
                Button("Cancel").on_click([this] { cancel_queued(); })
            ).gap(8_dp)
        ).gap(12_dp).padding(16_dp);
    }
};

PXA_APPLICATION(WorkApp)

#include <pxa/app.hpp>

#include <array>

#include "work_key.hpp"

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct WorkApp {
    State<std::string> status{"Ready"};
    State<std::string> completion{"Checking job"};
    pxa::Context* context = nullptr;
    std::uint32_t last_id = 0;
    bool busy = false;

    pxa::Task<void> schedule(std::uint32_t delay_ms) {
        auto previous = co_await context->storage().get_value<std::uint32_t>("sync.action.next");
        if ((!previous && previous.error() != pxa::Error::not_found) ||
            (previous && *previous == UINT32_MAX)) {
            status.set("Action sequence failed");
            busy = false;
            co_return pxa::Result<void>{};
        }
        const std::uint32_t action = previous ? *previous + 1 : 1;
        // Reserve the action before enqueue. A failed enqueue may leave a gap,
        // but a completed action is never mistaken for a later request.
        auto reserved = co_await context->storage().set_value("sync.action.next", action);
        if (!reserved) {
            status.set("Action sequence failed");
            busy = false;
            co_return pxa::Result<void>{};
        }
        std::array<std::byte, 5> input{std::byte{1}};
        (void)pxa::binary::write(input, action, 1);
        auto queued = co_await context->work().enqueue({
            .worker = "sync.job", .initial_delay_ms = delay_ms,
            .input = input,
            .retry_delay_ms = 1000, .max_attempts = 3});
        if (queued) {
            last_id = queued->id;
            std::array<std::byte, 8> id_bytes{};
            pxa::binary::Writer tracked(id_bytes);
            (void)tracked.write(last_id);
            (void)tracked.write(action);
            auto saved = co_await context->storage().set("sync.latest.action", id_bytes);
            status.set(saved ? "Queued" : "Queued, tracking failed");
            completion.set("Pending");
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

    pxa::Task<void> check() {
        std::array<std::byte, 8> id_bytes{};
        auto latest = co_await context->storage().get("sync.latest.action", id_bytes);
        if (!latest || *latest != id_bytes.size()) {
            completion.set(!latest && latest.error() == pxa::Error::not_found
                               ? "No tracked job" : "Check failed");
            co_return pxa::Result<void>{};
        }
        pxa::binary::Reader tracked(id_bytes);
        last_id = *tracked.read<std::uint32_t>();
        std::array<char, 32> key_storage{};
        const auto key = work_completion_key(*tracked.read<std::uint32_t>(),
                                             key_storage);
        if (key.empty())
            co_return std::unexpected(pxa::Error::resource_limit);
        std::array<std::byte, 1> marker{};
        auto result = co_await context->storage().get(key, marker);
        completion.set(result && *result == marker.size() &&
                               marker[0] == std::byte{1}
                           ? "Completed"
                           : !result && result.error() == pxa::Error::not_found
                               ? "Pending" : "Check failed");
        co_return pxa::Result<void>{};
    }

    void queue(std::uint32_t delay_ms = 0) {
        if (busy) return;
        busy = true;
        status.set("Queueing");
        if (!context->tasks().start(schedule(delay_ms))) {
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
        return ctx.tasks().start(check());
    }

    auto view() {
        return Column(
            Text<"Deferred work">().font(Font::title),
            Text(status),
            Row(
                Button("Queue").on_click([this] { queue(); }),
                Button("Later").on_click([this] { queue(15000); }),
                Button("Cancel").on_click([this] { cancel_queued(); })
            ).gap(8_dp),
            Text(completion),
            Button("Check").on_click([this] {
                auto started = context->tasks().start(check());
                if (!started) completion.set("Task capacity exceeded");
            })
        ).gap(12_dp).padding(16_dp);
    }
};

PXA_APPLICATION(WorkApp)

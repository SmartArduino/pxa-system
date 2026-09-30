#include <pxa/app.hpp>

#include <array>

#include "work_key.hpp"

struct SyncJob {
    pxa::Context* context = nullptr;
    pxa::WorkStart start;
    std::array<std::byte, 1> marker{};

    pxa::Task<void> run() {
        if (start.input_view().size() != 1 ||
            start.input_view()[0] != std::byte{1}) {
            auto rejected = co_await context->work().complete(
                start.id, pxa::WorkResult::failure);
            co_return rejected;
        }
        std::array<char, 32> key_storage{};
        const auto key = work_completion_key(start.id, key_storage);
        if (key.empty())
            co_return std::unexpected(pxa::Error::resource_limit);
        auto found = co_await context->storage().get(key, marker);
        pxa::WorkResult outcome = pxa::WorkResult::success;
        if (!found && found.error() == pxa::Error::not_found) {
            constexpr std::array<std::byte, 1> value{std::byte{1}};
            auto saved = co_await context->storage().set(key, value);
            if (!saved) outcome = start.attempt < 3
                ? pxa::WorkResult::retry : pxa::WorkResult::failure;
        } else if (!found) {
            outcome = start.attempt < 3
                ? pxa::WorkResult::retry : pxa::WorkResult::failure;
        }
        auto completed = co_await context->work().complete(start.id, outcome);
        co_return completed;
    }

    pxa::Result<void> on_start(pxa::Context& ctx,
                                std::span<const std::byte> config) {
        auto decoded = pxa::decode_work_start(config);
        if (!decoded) return std::unexpected(decoded.error());
        start = *decoded;
        context = &ctx;
        return ctx.tasks().start(run());
    }

    pxa::Result<bool> on_event(pxa::Context& ctx, const pxa::Event& event) {
        if (event.service != 13 || event.opcode != 0x8001) return false;
        auto stop = pxa::decode_work_stop_requested(event);
        if (!stop) return std::unexpected(stop.error());
        if (stop->id != start.id)
            return std::unexpected(pxa::Error::protocol_error);
        ctx.tasks().cancel();
        auto started = ctx.tasks().start(
            ctx.work().complete(start.id, pxa::WorkResult::retry));
        if (!started) return std::unexpected(started.error());
        return true;
    }
};

PXA_JOB(SyncJob)

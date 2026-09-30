#include <pxa/app.hpp>

#include <stats_generated.hpp>

struct StatsProvider {
    pxa::Task<void> respond(pxa::Context& ctx, std::uint32_t call_id,
                            stats::Get::Response response) {
        pxa::IpcReplyBuffer<stats::Get> buffer;
        auto sent = co_await ctx.ipc().reply<stats::Get>(call_id, response, buffer);
        if (!sent) co_return std::unexpected(sent.error());
        co_return pxa::Result<void>{};
    }

    pxa::Result<bool> on_event(pxa::Context& ctx, const pxa::Event& event) {
        if (event.service != 7 || event.opcode != 0x8001) return false;
        auto request = pxa::decode_ipc_request<stats::Get>(event);
        if (!request) {
            if (request.error() == pxa::Error::not_found) return false;
            return std::unexpected(request.error());
        }
        stats::Get::Response reply{};
        reply.next = request->value.seed + 1;
        reply.valid = true;
        reply.cached = false;
        if (!reply.note.emplace().set("remote"))
            return std::unexpected(pxa::Error::invalid_argument);
        if (!reply.label.set(request->value.label.view()))
            return std::unexpected(pxa::Error::invalid_argument);
        auto started = ctx.tasks().start(respond(ctx, request->call_id,
                                                 std::move(reply)));
        if (!started) return std::unexpected(started.error());
        return true;
    }
};

PXA_SERVICE(StatsProvider)

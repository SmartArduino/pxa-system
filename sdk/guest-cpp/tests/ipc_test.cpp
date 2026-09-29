#include <pxa/app.hpp>

#include <array>
#include <algorithm>
#include <cassert>
#include <string>

static std::uint64_t call_token;
static std::uint64_t other_token;
static std::uint64_t reply_token;
static unsigned calls;
static bool call_done;
static bool other_done;
static bool cancellation_mode;
static std::array<std::byte, 96> submitted_call{};
static std::size_t submitted_call_size;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                     std::uint32_t length) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    const auto service = pxa::wire::get16(bytes);
    if (service == 7) {
        if (pxa::wire::get16(bytes + 2) == 1) {
            ++calls;
            call_token = pxa::wire::get64(bytes + 4);
            assert(length <= submitted_call.size());
            submitted_call_size = length;
            std::copy(bytes, bytes + length, submitted_call.begin());
        } else {
            assert(pxa::wire::get16(bytes + 2) == 2);
            reply_token = pxa::wire::get64(bytes + 4);
            pxa::wire::Records records({bytes + 20, length - 20});
            auto id = records.take(1, 4);
            auto status = records.take(2, 4);
            auto payload = records.take(3, 2);
            assert(id && status && payload && records.empty());
            assert(pxa::wire::get32(id->data()) == 2);
            assert(pxa::wire::get32(status->data()) == 0);
            assert((*payload)[0] == std::byte{'o'});
        }
    } else if (service == 3) {
        other_token = pxa::wire::get64(bytes + 4);
    } else if (service == 1) {
        assert(pxa::wire::get16(bytes + 2) == 1);
        assert(pxa::wire::get64(bytes + 20) == call_token);
    } else assert(false);
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                 std::uint8_t*, std::uint32_t) {
    assert(false);
    return -1;
}

struct IpcApp {
    std::array<std::byte, 128> packet{};
    std::array<std::byte, 48> reply_packet{};
    std::array<std::byte, 16> output{};

    pxa::Task<void> invoke(pxa::Context& ctx) {
        auto pending = [&] {
            std::string endpoint = "Stats.get";
            std::array<std::byte, 2> input{std::byte{'a'}, std::byte{'b'}};
            return ctx.ipc().call(endpoint, input, packet, output);
        }();
        auto result = co_await std::move(pending);
        assert(result && result->call_id == 2 && result->size == 3);
        assert(output[0] == std::byte{'x'} && output[2] == std::byte{'z'});
        call_done = true;
        co_return pxa::Result<void>{};
    }

    pxa::Task<void> other(pxa::Context& ctx) {
        auto result = co_await ctx.request(3, 1);
        assert(result && result->payload.size() == 4);
        other_done = true;
        co_return pxa::Result<void>{};
    }

    pxa::Result<void> on_start(pxa::Context& ctx) {
        if (cancellation_mode) {
            return ctx.foreground_tasks().start(
                ctx.ipc().call("Stats.get", {}, packet, output));
        }
        auto started = ctx.tasks().start(invoke(ctx));
        if (!started) return started;
        return ctx.tasks().start(other(ctx));
    }

    pxa::Result<bool> on_event(pxa::Context& ctx, const pxa::Event& event) {
        if (event.service != 7 || event.opcode != 0x8001) return false;
        auto request = pxa::decode_ipc_request(event);
        if (!request) return std::unexpected(request.error());
        constexpr std::array<std::byte, 2> payload{std::byte{'o'}, std::byte{'k'}};
        auto started = ctx.tasks().start(ctx.ipc().reply(
            request->call_id, 0, payload, reply_packet));
        if (!started) return std::unexpected(started.error());
        return true;
    }
};

PXA_APPLICATION(IpcApp)

static int deliver(std::uint16_t service, std::uint16_t opcode,
                   std::uint64_t token,
                   std::span<const std::byte> payload) {
    std::array<std::byte, 80> packet{};
    pxa::wire::put16(packet.data(), service);
    pxa::wire::put16(packet.data() + 2, opcode);
    pxa::wire::put64(packet.data() + 4, token);
    pxa::wire::put32(packet.data() + 12, payload.size());
    std::copy(payload.begin(), payload.end(), packet.begin() + 20);
    return pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(packet.data()),
                            20 + payload.size());
}

int main() {
    assert(pxa_app_start(nullptr, 0) == 0);
    assert(calls == 1 && call_token == 1 && other_token == 2);
    pxa::Event request{7, 0x8001, 2,
        std::span{submitted_call}.subspan(20, submitted_call_size - 20)};
    auto decoded = pxa::decode_ipc_request(request);
    assert(decoded && decoded->call_id == 2 && decoded->endpoint == "Stats.get");
    assert(decoded->payload.size() == 2 && decoded->payload[0] == std::byte{'a'});
    auto optional_endpoint = submitted_call;
    pxa::wire::put16(optional_endpoint.data() + 20, 0x8001);
    pxa::Event invalid_request{7, 0x8001, 2,
        std::span{optional_endpoint}.subspan(20, submitted_call_size - 20)};
    auto invalid_decoded = pxa::decode_ipc_request(invalid_request);
    assert(!invalid_decoded &&
           invalid_decoded.error() == pxa::Error::protocol_error);

    std::array<std::byte, 8> accepted{};
    pxa::wire::put32(accepted.data() + 4, 2);
    assert(deliver(7, 1, call_token, accepted) == 1);
    assert(!call_done);
    assert(deliver(7, 0x8001, 2, request.payload) == 1);
    assert(reply_token == 3);
    std::array<std::byte, 4> success{};
    assert(deliver(7, 2, reply_token, success) == 1);
    std::array<std::byte, 11> result{};
    pxa::wire::put16(result.data() + 4, 3);
    pxa::wire::put16(result.data() + 6, 3);
    result[8] = std::byte{'x'};
    result[9] = std::byte{'y'};
    result[10] = std::byte{'z'};
    assert(deliver(7, 0x8002, 2, result) == 1);
    assert(call_done && !other_done);
    assert(deliver(3, 1, other_token, success) == 1);
    assert(other_done);
    pxa::wire::put16(result.data() + 4, 0x8003);
    auto invalid_result = pxa::decode_ipc_result(
        pxa::Event{7, 0x8002, 2, result}, std::span<std::byte>{});
    assert(!invalid_result &&
           invalid_result.error() == pxa::Error::protocol_error);
    pxa::wire::put16(result.data() + 4, 3);
    pxa_app_stop(0);

    cancellation_mode = true;
    assert(pxa_app_start(nullptr, 0) == 0 && calls == 2);
    assert(deliver(7, 1, call_token, accepted) == 1);
    std::array<std::byte, 1> background{std::byte{0}};
    assert(deliver(17, 0x8005, 0, background) == 1);
    assert(deliver(7, 0x8002, 2, result) == 0);
    pxa_app_stop(0);

    std::array<std::byte, 48> packet{};
    auto invalid = pxa::encode_ipc_call("1bad", {}, packet);
    assert(!invalid && invalid.error() == pxa::Error::invalid_argument);
    auto too_small = pxa::encode_ipc_call("Stats.get", {},
                                           std::span{packet}.first(24));
    assert(!too_small && too_small.error() == pxa::Error::resource_limit);
    auto encoded = pxa::encode_ipc_reply(2, 0,
        std::as_bytes(std::span{"ok", 2}), packet);
    assert(encoded && *encoded == 42);
    auto error_reply = pxa::encode_ipc_reply(2, -17, {}, packet);
    assert(!error_reply && error_reply.error() == pxa::Error::invalid_argument);
}

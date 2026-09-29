#include <pxa/app.hpp>

#include <array>
#include <algorithm>
#include <cassert>
#include <string>

static std::uint64_t token;
static std::uint16_t opcode;
static unsigned requests;
static bool cancelled;
static bool completed;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                     std::uint32_t length) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    assert(pxa::wire::get16(bytes) == 13 && length >= 20);
    token = pxa::wire::get64(bytes + 4);
    opcode = pxa::wire::get16(bytes + 2);
    ++requests;
    pxa::wire::Records records({bytes + 20, length - 20});
    if (opcode == 1) {
        auto worker = records.take(1);
        auto delay = records.take(2, 4);
        auto hint = records.take(3, 4);
        auto input = records.take(5, 3);
        auto retry = records.take(6, 4);
        auto attempts = records.take(7, 1);
        assert(worker && delay && hint && input && retry && attempts && records.empty());
        assert(std::string_view(reinterpret_cast<const char*>(worker->data()),
                                worker->size()) == "sync.job");
        assert(pxa::wire::get32(delay->data()) == 100);
        assert(pxa::wire::get32(hint->data()) == 5000);
        assert((*input)[0] == std::byte{'a'});
        assert(pxa::wire::get32(retry->data()) == 1000);
        assert((*attempts)[0] == std::byte{3});
    } else {
        auto id = records.take(4, 4);
        assert(id && pxa::wire::get32(id->data()) == 7);
        if (opcode == 3) {
            auto result = records.take(9, 1);
            assert(result && (*result)[0] == std::byte{2});
        } else assert(opcode == 2);
        assert(records.empty());
    }
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                 std::uint8_t*, std::uint32_t) {
    assert(false);
    return -1;
}

struct WorkApp {
    pxa::Task<void> run(pxa::Context& ctx) {
        auto pending = [&] {
            std::string worker = "sync.job";
            std::array<std::byte, 3> input{
                std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
            return ctx.work().enqueue({.worker = worker,
                .initial_delay_ms = 100, .execution_hint_ms = 5000,
                .input = input, .retry_delay_ms = 1000, .max_attempts = 3});
        }();
        auto schedule = co_await std::move(pending);
        assert(schedule && schedule->id == 7 &&
               schedule->granted_execution_ms == 5000);
        auto result = co_await ctx.work().cancel(schedule->id);
        assert(result);
        cancelled = true;
        co_return pxa::Result<void>{};
    }

    pxa::Task<void> retry(pxa::Context& ctx, std::uint32_t id) {
        auto result = co_await ctx.work().complete(id, pxa::WorkResult::retry);
        assert(result);
        completed = true;
        co_return pxa::Result<void>{};
    }

    pxa::Result<void> on_start(pxa::Context& ctx) {
        return ctx.tasks().start(run(ctx));
    }

    pxa::Result<bool> on_event(pxa::Context& ctx, const pxa::Event& event) {
        if (event.service != 13 || event.opcode != 0x8001) return false;
        auto stop = pxa::decode_work_stop_requested(event);
        if (!stop) return std::unexpected(stop.error());
        auto started = ctx.tasks().start(retry(ctx, stop->id));
        if (!started) return std::unexpected(started.error());
        return true;
    }
};

PXA_APPLICATION(WorkApp)

static void deliver(std::uint16_t service, std::uint16_t event_opcode,
                    std::uint64_t event_token,
                    std::span<const std::byte> payload) {
    std::array<std::byte, 80> packet{};
    pxa::wire::put16(packet.data(), service);
    pxa::wire::put16(packet.data() + 2, event_opcode);
    pxa::wire::put64(packet.data() + 4, event_token);
    pxa::wire::put32(packet.data() + 12, payload.size());
    std::copy(payload.begin(), payload.end(), packet.begin() + 20);
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(packet.data()),
                            20 + payload.size()) == 1);
}

int main() {
    assert(pxa_app_start(nullptr, 0) == 0 && requests == 1 && opcode == 1);
    std::array<std::byte, 20> accepted{};
    pxa::wire::put16(accepted.data() + 4, 4);
    pxa::wire::put16(accepted.data() + 6, 4);
    pxa::wire::put32(accepted.data() + 8, 7);
    pxa::wire::put16(accepted.data() + 12, 8);
    pxa::wire::put16(accepted.data() + 14, 4);
    pxa::wire::put32(accepted.data() + 16, 5000);
    deliver(13, 1, token, accepted);
    assert(requests == 2 && opcode == 2);
    std::array<std::byte, 4> success{};
    deliver(13, 2, token, success);
    assert(cancelled);

    std::array<std::byte, 12> stop{};
    pxa::wire::put32(stop.data(), 7);
    pxa::wire::put64(stop.data() + 4, 9000);
    deliver(13, 0x8001, 0, stop);
    assert(requests == 3 && opcode == 3);
    deliver(13, 3, token, success);
    assert(completed);
    pxa_app_stop(0);

    std::array<std::byte, 48> config{};
    pxa::wire::Writer writer(config);
    std::array<std::byte, 8> scalar{};
    pxa::wire::put32(scalar.data(), 7);
    assert(pxa::wire::record(writer, 7, std::span{scalar}.first(4)));
    scalar[0] = std::byte{2};
    assert(pxa::wire::record(writer, 9, std::span{scalar}.first(1)));
    pxa::wire::put64(scalar.data(), 9000);
    assert(pxa::wire::record(writer, 10, scalar));
    constexpr std::array<std::byte, 3> input{
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    assert(pxa::wire::record(writer, 11, input));
    auto started = pxa::decode_work_start(std::span{config}.first(writer.size()));
    assert(started && started->id == 7 && started->attempt == 2 &&
           started->deadline_ms == 9000 &&
           std::ranges::equal(started->input_view(), input));
    config[8] = std::byte{0};
    auto bad = pxa::decode_work_start(std::span{config}.first(writer.size()));
    assert(!bad && bad.error() == pxa::Error::protocol_error);

    std::array<std::byte, 145> packet{};
    pxa::WorkRequest invalid{.worker = "Sync.job", .max_attempts = 1};
    auto encoded = pxa::encode_work_enqueue(invalid, packet);
    assert(!encoded && encoded.error() == pxa::Error::invalid_argument);
    invalid.worker = "sync.job";
    invalid.max_attempts = 2;
    encoded = pxa::encode_work_enqueue(invalid, packet);
    assert(!encoded && encoded.error() == pxa::Error::invalid_argument);
    invalid.retry_delay_ms = 1000;
    encoded = pxa::encode_work_enqueue(invalid, std::span{packet}.first(24));
    assert(!encoded && encoded.error() == pxa::Error::resource_limit);
}

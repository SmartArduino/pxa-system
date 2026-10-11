#include <pxa/app.hpp>
#include <pxa/events.hpp>
#include <charconv>
#include <ctime>

struct AbiControlBench {
    static constexpr unsigned iterations = 1000;
    unsigned sample = 0;

    static std::uint64_t nanoseconds() noexcept {
        timespec time{};
        if (clock_gettime(CLOCK_MONOTONIC, &time)) return 0;
        return std::uint64_t(time.tv_sec) * 1000000000 + time.tv_nsec;
    }
    pxa::Result<void> on_start(pxa::Context& context) {
        sample = 0;
        return context.clock().set_period(16);
    }
    void on_background(pxa::Context& context) noexcept {
        (void)context.clock().set_period(0);
    }
    void on_foreground(pxa::Context& context) noexcept {
        sample = 0;
        (void)context.clock().set_period(16);
    }
    pxa::Result<bool> on_event(pxa::Context& context, const pxa::Event& event) {
        if (!event.is<pxa::ClockTick>()) return false;
        std::array<std::byte, 8> payload{};
        // Unknown cancellation is a valid, side-effect-free Core operation.
        pxa::wire::put64(payload.data(), UINT64_C(0xffeeddcc12345678));
        const auto start = nanoseconds();
        unsigned errors = 0;
        for (unsigned i = 0; i < iterations; ++i)
            errors += !context.transport().send(1, 1, 0, payload);
        const auto elapsed = nanoseconds() - start;
        constexpr auto path = "call";
        std::array<char, 192> line{};
        char* cursor = line.data();
        auto text = [&](std::string_view value) {
            for (char byte : value) *cursor++ = byte;
        };
        auto number = [&](std::uint64_t value) {
            cursor = std::to_chars(cursor, line.data() + line.size(), value).ptr;
        };
        // The bounded labels plus six uint64 decimal fields fit in 192 bytes.
        text("ABI_BENCH path="); text(path);
        text(" sample="); number(sample);
        text(" iterations="); number(iterations);
        text(" elapsed_ns="); number(elapsed);
        text(" errors="); number(errors);
        text(" transport_bytes="); number(sizeof(pxa::Transport));
        auto logged = pxa::LogService(context.transport()).write(pxa::LogLevel::info,
            {line.data(), std::size_t(cursor - line.data())});
        if (!logged) return std::unexpected(logged.error());
        if (++sample == 32) {
            auto stopped = context.clock().set_period(0);
            if (!stopped) return std::unexpected(stopped.error());
        }
        return true;
    }
};
PXA_APPLICATION(AbiControlBench)

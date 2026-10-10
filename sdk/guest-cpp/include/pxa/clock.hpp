#pragma once

#include "task.hpp"

#include <array>

namespace pxa {

struct ClockTick { std::uint64_t timestamp_us = 0; };

inline Result<ClockTick> decode_clock_tick(const Event& event) noexcept {
    if (event.service != 4 || event.opcode != 0x8001 || event.token != 0 ||
        event.payload.size() != 8)
        return std::unexpected(Error::protocol_error);
    return ClockTick{wire::get64(event.payload.data())};
}

class ClockService {
public:
    ClockService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Result<void> set_period(std::uint16_t period_ms) noexcept {
        if (period_ms != 0 && (period_ms < 16 || period_ms > 1000))
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 2> payload{};
        wire::put16(payload.data(), period_ms);
        return transport_.send(4, 1, 0, payload);
    }

    // A cooperative round trip without a nested Task/frame-pool slot. Useful
    // between synchronous Flash/stream blocks; this is not a timed delay.
    class Yield {
    public:
        Yield(Transport& transport, RequestTable& requests) noexcept
            : response_(transport, requests, 4, 2, std::span<const std::byte>{}) {}
        bool await_ready() const noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> handle) noexcept { return response_.await_suspend(handle); }
        Result<void> await_resume() const noexcept {
            auto event = response_.await_resume();
            if (!event) return std::unexpected(event.error());
            if (event->payload.size() < 4) return std::unexpected(Error::protocol_error);
            const auto status = static_cast<std::int32_t>(wire::get32(event->payload.data()));
            if (status) return event->payload.size() == 4 ? Result<void>(std::unexpected(static_cast<Error>(status)))
                                                        : Result<void>(std::unexpected(Error::protocol_error));
            if (event->payload.size() != 12) return std::unexpected(Error::protocol_error);
            return {};
        }
    private:
        Response response_;
    };
    Yield yield() noexcept { return Yield(transport_, requests_); }

    Task<std::uint64_t> now(this ClockService self) {
        auto& [transport_, requests_] = self;
        auto event = co_await Response(transport_, requests_, 4, 2, {});
        if (!event) co_return std::unexpected(event.error());
        if (event->payload.size() < 4)
            co_return std::unexpected(Error::protocol_error);
        auto status = static_cast<std::int32_t>(
            wire::get32(event->payload.data()));
        if (status != 0) {
            if (event->payload.size() != 4)
                co_return std::unexpected(Error::protocol_error);
            co_return std::unexpected(static_cast<Error>(status));
        }
        if (event->payload.size() != 12)
            co_return std::unexpected(Error::protocol_error);
        co_return wire::get64(event->payload.data() + 4);
    }

private:
    Transport& transport_;
    RequestTable& requests_;
};

struct FixedSteps {
    std::uint32_t step_us = 0;
    std::uint32_t frame_delta_us = 0;
    std::uint8_t count = 0;
};

class FixedStepper {
public:
    explicit FixedStepper(std::uint32_t step_us = 16000,
                          std::uint8_t maximum_steps = 4) noexcept
        : step_us_(step_us), maximum_steps_(maximum_steps) {}
    void reset() noexcept {
        previous_us_ = 0;
        remainder_us_ = 0;
    }
    FixedSteps advance(std::uint64_t timestamp_us) noexcept {
        if (!timestamp_us || !step_us_ || !maximum_steps_)
            return {};
        if (!previous_us_) {
            previous_us_ = timestamp_us;
            return {step_us_, step_us_, 1};
        }
        if (timestamp_us <= previous_us_) return {};
        auto elapsed = timestamp_us - previous_us_;
        previous_us_ = timestamp_us;
        // Report wall time even when simulation catch-up is capped. Using the
        // capped interval as frame time makes slow games report the target FPS.
        const auto frame_elapsed = static_cast<std::uint32_t>(
            elapsed > UINT32_MAX ? UINT32_MAX : elapsed);
        const auto cap = std::uint64_t(step_us_) * maximum_steps_;
        if (elapsed > cap) elapsed = cap;
        auto accumulated = remainder_us_ + elapsed;
        auto count = accumulated / step_us_;
        if (count > maximum_steps_) count = maximum_steps_;
        remainder_us_ = accumulated - count * step_us_;
        return {step_us_, frame_elapsed,
                static_cast<std::uint8_t>(count)};
    }
private:
    std::uint32_t step_us_;
    std::uint8_t maximum_steps_;
    std::uint64_t previous_us_ = 0;
    std::uint64_t remainder_us_ = 0;
};

} // namespace pxa

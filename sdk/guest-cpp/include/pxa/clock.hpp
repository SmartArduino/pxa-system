#pragma once

#include "task.hpp"

#include <array>

namespace pxa {

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

    Task<std::uint64_t> now() {
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
        const auto cap = std::uint64_t(step_us_) * maximum_steps_;
        if (elapsed > cap) elapsed = cap;
        auto accumulated = remainder_us_ + elapsed;
        auto count = accumulated / step_us_;
        if (count > maximum_steps_) count = maximum_steps_;
        remainder_us_ = accumulated - count * step_us_;
        return {step_us_, static_cast<std::uint32_t>(elapsed),
                static_cast<std::uint8_t>(count)};
    }
private:
    std::uint32_t step_us_;
    std::uint8_t maximum_steps_;
    std::uint64_t previous_us_ = 0;
    std::uint64_t remainder_us_ = 0;
};

} // namespace pxa

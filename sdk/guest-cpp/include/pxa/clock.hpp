#pragma once

#include "task.hpp"
#include "service_wire.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <limits>
#include <type_traits>

namespace pxa {

struct ClockLimits {
    std::uint32_t min_period_us=0, max_period_us=0, period_step_us=0;
    std::uint32_t dispatch_quantum_us=0, flags=0;
    bool valid() const noexcept {
        return min_period_us && max_period_us>=min_period_us &&
            period_step_us && dispatch_quantum_us;
    }
    bool supports(std::uint32_t period_us) const noexcept {
        return valid() && (!period_us || (period_us>=min_period_us &&
            period_us<=max_period_us && period_us%period_step_us==0));
    }
};

struct ClockTick { std::uint64_t timestamp_us = 0; };

// The effective zone at the sampled instant, including any DST adjustment.
// This is not a rule database for converting arbitrary past/future instants.
struct TimeZone {
    std::chrono::seconds utc_offset{};
    bool daylight_saving=false;
};

struct WallTime {
    std::chrono::sys_time<std::chrono::microseconds> utc{};
    std::uint64_t monotonic_us=0;
    TimeZone zone{};
    std::chrono::local_time<std::chrono::microseconds> local() const noexcept {
        return std::chrono::local_time<std::chrono::microseconds>{utc.time_since_epoch()+zone.utc_offset};
    }
};

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

private:
    template<bool Configure>
    class [[nodiscard("Clock timing request must be co_awaited")]] TimingQuery {
    public:
        using Value=std::conditional_t<Configure,std::uint32_t,ClockLimits>;
        TimingQuery(Transport& transport,RequestTable& requests,std::uint32_t period_us=0) noexcept
            : response_(transport,requests,4,Configure?5:4,
                std::span<const std::byte>{payload_}.first(Configure?4:0)), requested_(period_us) {
            wire::put32(payload_.data(),period_us);
        }
        TimingQuery(const TimingQuery&)=delete;
        TimingQuery(TimingQuery&&)=delete;
        bool await_ready() const noexcept {return false;}
        bool await_suspend(std::coroutine_handle<> handle) noexcept {return response_.await_suspend(handle);}
        Result<Value> await_resume() const noexcept {
            auto event=response_.await_resume();
            if(!event)return std::unexpected(event.error());
            auto body=wire::result_body(event->payload);
            if(!body)return std::unexpected(body.error());
            if(body->size()!=(Configure?4u:20u))return std::unexpected(Error::protocol_error);
            const auto* p=body->data();
            if constexpr(Configure){
                auto accepted=wire::get32(p);
                if(accepted!=requested_)return std::unexpected(Error::protocol_error);
                return accepted;
            }else{
                ClockLimits limits{wire::get32(p),wire::get32(p+4),wire::get32(p+8),
                    wire::get32(p+12),wire::get32(p+16)};
                if(!limits.valid())return std::unexpected(Error::protocol_error);
                return limits;
            }
        }
    private:
        std::array<std::byte,4> payload_{};
        Response response_;
        std::uint32_t requested_;
    };
public:
    auto limits() noexcept {return TimingQuery<false>(transport_,requests_);}
    auto configure(std::uint32_t period_us) noexcept {
        return TimingQuery<true>(transport_,requests_,period_us);
    }

    // A cooperative round trip without a nested Task/frame-pool slot. Useful
    // between synchronous Flash/stream blocks; this is not a timed delay.
    class [[nodiscard("Clock yield must be co_awaited")]] Yield {
    public:
        Yield(Transport& transport, RequestTable& requests) noexcept
            : response_(transport, requests, 4, 2, std::span<const std::byte>{}) {}
        bool await_ready() const noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> handle) noexcept { return response_.await_suspend(handle); }
        Result<void> await_resume() const noexcept {
            auto event = response_.await_resume();
            if (!event) return std::unexpected(event.error());
            auto body = wire::result_body(event->payload);
            if (!body) return std::unexpected(body.error());
            if (body->size() != 8) return std::unexpected(Error::protocol_error);
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
        auto body = wire::result_body(event->payload);
        if (!body) co_return std::unexpected(body.error());
        if (body->size() != 8) co_return std::unexpected(Error::protocol_error);
        co_return wire::get64(event->payload.data() + 4);
    }

private:
    // A fixed 24-byte snapshot and no nested coroutine/frame-pool allocation.
    // UTC, monotonic anchor and local offset come from the same Host sample.
    template<bool ZoneOnly>
    class [[nodiscard("Clock query must be co_awaited")]] WallTimeQuery {
    public:
        using Value=std::conditional_t<ZoneOnly,TimeZone,WallTime>;
        WallTimeQuery(Transport& transport,RequestTable& requests) noexcept
            : response_(transport,requests,4,3,std::span<const std::byte>{}) {}
        bool await_ready() const noexcept {return false;}
        bool await_suspend(std::coroutine_handle<> handle) noexcept {return response_.await_suspend(handle);}
        Result<Value> await_resume() const noexcept {
            auto event=response_.await_resume();
            if(!event)return std::unexpected(event.error());
            auto body=wire::result_body(event->payload);
            if(!body)return std::unexpected(body.error());
            if(body->size()!=24)return std::unexpected(Error::protocol_error);
            const auto* bytes=body->data();
            const auto unix_us=std::bit_cast<std::int64_t>(wire::get64(bytes));
            const auto offset=std::bit_cast<std::int32_t>(wire::get32(bytes+16));
            const auto flags=wire::get32(bytes+20);
            const auto delta=std::int64_t(offset)*1000000;
            if(offset < -86400 || offset > 86400 || (flags&~1u) ||
               (delta>0 && unix_us>std::numeric_limits<std::int64_t>::max()-delta) ||
               (delta<0 && unix_us<std::numeric_limits<std::int64_t>::min()-delta))
                return std::unexpected(Error::protocol_error);
            TimeZone zone{std::chrono::seconds{offset},bool(flags&1u)};
            if constexpr(ZoneOnly)return zone;
            else return WallTime{std::chrono::sys_time<std::chrono::microseconds>{
                std::chrono::microseconds{unix_us}},wire::get64(bytes+8),zone};
        }
    private:
        Response response_;
    };
public:
    auto wall_time() noexcept {return WallTimeQuery<false>(transport_,requests_);}
    auto time_zone() noexcept {return WallTimeQuery<true>(transport_,requests_);}

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

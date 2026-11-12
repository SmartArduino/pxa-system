#include "../../sdk/testing/pxa_control_capture.h"
#include <pxa/app.hpp>
#include <cassert>
#include <cstdio>

#undef PXA_APPLICATION
#define PXA_APPLICATION(App)
#include "recipe_timer.hpp"

static std::uint64_t clock_token, storage_token;
static unsigned running_periods, writes;
static std::uint64_t saved_value;
extern "C" std::int32_t pxa_test_control(const std::uint8_t* data, std::uint32_t size) {
    const auto* p = reinterpret_cast<const std::byte*>(data);
    const auto service = wire::get16(p), opcode = wire::get16(p + 2);
    if (service == 4) {
        if (opcode == 1) {
            assert(size == 22);
            const auto period = wire::get16(p + 20);
            assert(period == 0 || period == 1000);
            running_periods += period != 0;
        } else {
            assert(opcode == 2);
            clock_token = wire::get64(p + 4);
        }
    } else if (service == 6) {
        assert(opcode == 2);
        storage_token = wire::get64(p + 4);
        const auto key_size = wire::get16(p + 22);
        assert(size == 20 + 8 + key_size + 8);
        saved_value = wire::get64(p + 28 + key_size);
        ++writes;
    } else assert(service == 3 || service == 1);
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) {
    return -3;
}

struct Probe : Timer {
    static inline Probe* live;
    Result<void> on_start(Context& ctx) {
        live = this; context = &ctx;
        remaining = 4000000; phase = Phase::paused; ready.set(true);
        return {};
    }
    auto view() { return Text("Recipe lifecycle"); }
    Result<bool> on_event(Context& ctx, const Event& event) {
        if (event.service == 0x8001) {
            if (event.opcode == 2) checkpoint();
            else request(phase != Phase::running);
            return true;
        }
        return Timer::on_event(ctx, event);
    }
};
using Runtime = AppRuntime<Probe>;
static void deliver(std::uint16_t service, std::uint16_t opcode, std::uint64_t token,
                    std::span<const std::byte> body = {}) {
    std::array<std::byte, 64> packet{};
    wire::put16(packet.data(), service); wire::put16(packet.data() + 2, opcode);
    wire::put64(packet.data() + 4, token); wire::put32(packet.data() + 12, body.size());
    std::copy(body.begin(), body.end(), packet.begin() + 20);
    assert(Runtime::event(reinterpret_cast<const std::uint8_t*>(packet.data()), 20 + body.size()) == 1);
}
static void foreground(bool value) {
    const std::array payload{std::byte(value)};
    deliver(17, 0x8005, 0, payload);
}
static void clock_now(std::uint64_t value) {
    std::array<std::byte, 12> payload{}; wire::put64(payload.data() + 4, value);
    deliver(4, 2, clock_token, payload);
}
int main() {
    assert(Runtime::start(nullptr, 0) == 0);
    auto& timer = *Probe::live;
    // A start waiting for Clock.now must not start after a background/resume.
    deliver(0x8001, 1, 0); assert(clock_token && !timer.ready.get());
    foreground(false); foreground(true); clock_now(2000000);
    assert(timer.phase == Timer::Phase::paused && timer.ready.get() && !running_periods);
    deliver(0x8001, 1, 0); clock_now(2000000);
    assert(timer.phase == Timer::Phase::running && running_periods == 1);
    assert(writes == 1 && saved_value == 4000000);
    std::array<std::byte, 8> tick{}; wire::put64(tick.data(), 3000000);
    deliver(4, 0x8001, 0, tick); assert(timer.remaining == 3000000);
    // Pause while Clock and Storage replies are delayed. Background must freeze
    // synchronously; neither an old reply nor a late tick may consume that time.
    deliver(0x8001, 1, 0); assert(!timer.ready.get());
    foreground(false);
    assert(timer.phase == Timer::Phase::paused && timer.remaining == 3000000 && timer.ready.get());
    foreground(true); wire::put64(tick.data(), 100000000);
    deliver(4, 0x8001, 0, tick); clock_now(100000000);
    assert(timer.phase == Timer::Phase::paused && timer.remaining == 3000000);
    const std::array<std::byte, 4> success{};
    deliver(6, 2, storage_token, success);
    assert(writes == 2 && saved_value == 3000000);
    deliver(6, 2, storage_token, success);
    assert(timer.storage_status.get() == "已保存");
    assert(!timer.storage_notice.get());
    // Routine autosave stays quiet; a failed write remains visible until a
    // later confirmed save recovers, without changing the running game state.
    deliver(0x8001, 2, 0);
    std::array<std::byte, 4> failure{};
    wire::put32(failure.data(), static_cast<std::uint32_t>(Error::not_found));
    deliver(6, 2, storage_token, failure);
    assert(timer.storage_notice.get() && timer.storage_status.get() == "保存失败");
    assert(timer.phase == Timer::Phase::paused && timer.remaining == 3000000);
    deliver(0x8001, 2, 0);
    deliver(6, 2, storage_token, success);
    assert(!timer.storage_notice.get() && timer.storage_status.get() == "已保存");
    Runtime::stop(0);
    assert(task_pool_stats().active_slots == 0);
    std::puts("Timer recipe: immediate background pause, stale Clock replies, coalesced checkpoint passed");
}

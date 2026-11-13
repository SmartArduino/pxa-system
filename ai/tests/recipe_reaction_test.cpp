#include "../../sdk/testing/pxa_control_capture.h"
#include <pxa/app.hpp>
#include <cassert>
#include <cstdio>

#undef PXA_APPLICATION
#define PXA_APPLICATION(App)
#include "recipe_reaction.hpp"

static std::uint64_t clock_token;
static unsigned running_periods;
extern "C" std::int32_t pxa_test_control(const std::uint8_t* data, std::uint32_t size) {
    const auto* p = reinterpret_cast<const std::byte*>(data);
    const auto service = wire::get16(p), opcode = wire::get16(p + 2);
    if (service == 4) {
        if (opcode == 1) {
            assert(size == 22);
            const auto period = wire::get16(p + 20);
            assert(period == 0 || period == 100);
            running_periods += period != 0;
        } else {
            assert(opcode == 2);
            clock_token = wire::get64(p + 4);
        }
    } else assert(service == 3 || service == 1); // No filesystem writes, audio or networking.
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }
struct Probe : Reaction {
    static inline Probe* live;
    Result<void> on_start(Context& ctx) { live = this; context = &ctx; return {}; }
    auto view() { return Text("Reaction lifecycle"); }
    Result<bool> on_event(Context& ctx, const Event& event) {
        if (event.service == 0x8001) {
            if (event.opcode == 1) toggle();
            else if (event.opcode == 2) choose(int(wire::get32(event.payload.data())));
            else if (event.opcode == 3) reset();
            else if (event.opcode == 4) {
                input::ControllerState state; state.connected = true;
                state.buttons = wire::get32(event.payload.data()); on_controller(ctx, state);
            }
            return true;
        }
        return Reaction::on_event(ctx, event);
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
static void foreground(bool value) { const std::array payload{std::byte(value)}; deliver(17, 0x8005, 0, payload); }
static void action(std::uint16_t opcode, std::uint32_t value = 0) {
    std::array<std::byte, 4> payload{}; wire::put32(payload.data(), value); deliver(0x8001, opcode, 0, payload);
}
static void clock_now(std::uint64_t value) {
    std::array<std::byte, 12> payload{}; wire::put64(payload.data() + 4, value); deliver(4, 2, clock_token, payload);
}
static void button(std::uint32_t buttons) { action(4, buttons); action(4); }
int main() {
    assert(Runtime::start(nullptr, 0) == 0);
    auto& game = *Probe::live;
    // An in-flight start cannot resume a round after Home/background.
    action(1); assert(clock_token && !game.ready.get());
    foreground(false); foreground(true); clock_now(1000000);
    assert(game.phase == Reaction::Phase::idle && game.ready.get() && running_periods == 0);
    // A wrong target ends the game without awarding a point.
    action(1); clock_now(1000000);
    assert(game.phase == Reaction::Phase::running);
    action(2, (game.target + 1) % 4); clock_now(1100000);
    assert(game.phase == Reaction::Phase::lost && game.score == 0 && game.status.get() == "点错了");
    // Ten actual hits complete a round; adjacent targets always differ.
    action(3); action(1); clock_now(2000000);
    for (int score = 1; score <= 10; ++score) {
        const auto previous = game.target;
        action(2, previous); clock_now(2000000 + score * 20000);
        assert(game.score == score);
        if (score != 10) assert(game.target != previous && game.phase == Reaction::Phase::running);
    }
    assert(game.phase == Reaction::Phase::won && game.status.get() == "完成" && !game.playing.get());
    // In-flight input is invalidated on pause. Resuming preserves score/time.
    action(3); button(input::controller_a); clock_now(3000000);
    const auto frozen = game.remaining;
    action(2, game.target); foreground(false); foreground(true); clock_now(3001000);
    assert(game.phase == Reaction::Phase::paused && game.score == 0 && game.remaining == frozen);
    button(input::controller_start); clock_now(100000000);
    assert(game.phase == Reaction::Phase::running && game.deadline == 100000000 + frozen);
    // Input must check monotonic time itself, even before a timer tick arrives.
    action(2, game.target); clock_now(game.deadline);
    assert(game.phase == Reaction::Phase::lost && game.score == 0 && game.status.get() == "时间到");
    // Controller navigation selects a real grid cell; X cancels pending input.
    button(input::controller_x); button(input::controller_a); clock_now(200000000);
    const auto target = game.target;
    if (target & 1) button(input::controller_right);
    if (target & 2) button(input::controller_down);
    assert(game.focus.get() == target);
    button(input::controller_a); clock_now(200010000);
    assert(game.score == 1 && game.phase == Reaction::Phase::running);
    button(input::controller_a); button(input::controller_x); clock_now(200020000);
    assert(game.phase == Reaction::Phase::idle && game.score == 0 && game.ready.get());
    Runtime::stop(0); assert(task_pool_stats().active_slots == 0);
    std::puts("Reaction recipe: win/loss, deadline, controller and stale background replies passed");
}

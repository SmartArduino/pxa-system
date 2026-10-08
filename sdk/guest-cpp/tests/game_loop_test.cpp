#include <pxa/app.hpp>

#include <array>
#include <cassert>

static int updates;
static int frames;
static int period_changes;
static std::uint16_t last_period;
static pxa::game::FrameTick last_tick;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t length) {
    auto* bytes = reinterpret_cast<const std::byte*>(data);
    assert(length == 22);
    assert(pxa::wire::get16(bytes) == 4);
    assert(pxa::wire::get16(bytes + 2) == 1);
    last_period = pxa::wire::get16(bytes + 20);
    ++period_changes;
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    return -3;
}

struct Game {
    void on_update(pxa::Context&, std::uint32_t step_us) {
        assert(step_us == 16000);
        ++updates;
    }
    void on_frame(pxa::Context& context, pxa::game::FrameTick tick) {
        assert(context.foreground());
        ++frames;
        last_tick = tick;
    }
};

PXA_GAME(Game)

static void tick(std::uint64_t timestamp) {
    std::array<std::byte, 28> packet{};
    pxa::wire::put16(packet.data(), 4);
    pxa::wire::put16(packet.data() + 2, 0x8001);
    pxa::wire::put32(packet.data() + 12, 8);
    pxa::wire::put64(packet.data() + 20, timestamp);
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(packet.data()),
               packet.size()) == 1);
}

static void lifecycle(std::uint8_t state) {
    std::array<std::byte, 21> packet{};
    pxa::wire::put16(packet.data(), 17);
    pxa::wire::put16(packet.data() + 2, 0x8005);
    pxa::wire::put32(packet.data() + 12, 1);
    packet[20] = std::byte(state);
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(packet.data()),
               packet.size()) == 1);
}

int main() {
    assert(pxa_app_start(nullptr, 0) == 0);
    assert(last_period == 16 && period_changes == 1);
    tick(1000000);
    assert(updates == 1 && frames == 1 && last_tick.simulation_steps == 1);
    tick(1016000);
    assert(updates == 2 && frames == 2 && last_tick.frame_delta_us == 16000);
    tick(2016000);
    assert(updates == 6 && frames == 3 && last_tick.simulation_steps == 4);
    assert(last_tick.frame_delta_us == 1000000);
    lifecycle(0);
    assert(last_period == 0 && period_changes == 2);
    tick(3016000);
    assert(updates == 6 && frames == 3);
    lifecycle(1);
    assert(last_period == 16 && period_changes == 3);
    tick(4016000);
    assert(updates == 7 && frames == 4 && last_tick.simulation_steps == 1);
    pxa_app_stop(0);
}

#include <pxa/app.hpp>

#include <array>
#include <cassert>

static std::array<std::uint8_t, 64> last_packet{};
static std::uint32_t last_packet_size = 0;
static int starts = 0;
static int stops = 0;
static int foregrounds = 0;
static int backgrounds = 0;
static int metric_changes = 0;
static int back_requests = 0;
static bool stop_rejected = false;

extern "C" std::int32_t pxa_submit(const std::uint8_t* bytes,
                                      std::uint32_t size) {
    assert(size <= last_packet.size());
    for (std::uint32_t i = 0; i < size; ++i) last_packet[i] = bytes[i];
    last_packet_size = size;
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    return 0;
}

struct TestApp {
    pxa::Transport* transport = nullptr;

    pxa::Result<void> on_start(pxa::Context& context) {
        ++starts;
        transport = &context.transport();
        std::array<std::byte, 3> payload{std::byte{1}, std::byte{2},
                                          std::byte{3}};
        return transport->send(7, 2, UINT64_C(0x1122334455667788), payload);
    }

    void on_background(pxa::Context& context) {
        assert(!context.foreground());
        ++backgrounds;
    }

    void on_foreground(pxa::Context& context) {
        assert(context.foreground());
        ++foregrounds;
    }

    void on_window_changed(const pxa::WindowMetrics& metrics) {
        assert(metrics.revision == 1 && metrics.logical_width == 320);
        assert(metrics.pixel_height == 480 && metrics.focused);
        ++metric_changes;
    }

    pxa::BackAction on_back() {
        ++back_requests;
        return pxa::BackAction::stay;
    }

    pxa::Result<bool> on_event(pxa::Context&, const pxa::Event& event) {
        return event.service == 7 && event.opcode == 2;
    }

    void on_stop(pxa::StopReason) noexcept {
        stop_rejected = !transport->send(7, 2, 1);
        ++stops;
    }
};

PXA_APPLICATION(TestApp)

int main() {
    // Existing fullscreen callers retain transient bars. Games can hide only
    // the status bar while preserving system navigation; invalid modes must
    // not publish a partially encoded configuration.
    {
        pxa::Transport transport;
        pxa::RequestTable requests;
        std::array<std::byte,64> scratch{};
        assert(transport.scratch(scratch));
        transport.phase(pxa::Phase::start);
        pxa::WindowService window(transport,requests);
        assert(window.fullscreen());
        assert(last_packet_size==35&&last_packet[24]==1&&last_packet[29]==2&&last_packet[34]==2);
        assert(window.fullscreen(pxa::WindowBarMode::hidden));
        assert(last_packet[29]==1&&last_packet[34]==2);
        auto previous=last_packet;
        assert(!window.fullscreen(static_cast<pxa::WindowBarMode>(3))&&last_packet==previous);
    }
    assert(pxa_app_start(nullptr, 0) == 0);
    assert(starts == 1 && last_packet_size == 23);
    const std::array<std::uint8_t, 23> golden{
        7, 0, 2, 0, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        3, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3};
    for (std::size_t i = 0; i < golden.size(); ++i)
        assert(last_packet[i] == golden[i]);
    assert(pxa_app_start(nullptr, 0) ==
           static_cast<int>(pxa::Error::bad_state));

    auto event = golden;
    event[12] = 0;
    event[20] = event[21] = event[22] = 0;
    assert(pxa_app_on_event(event.data(), 20) == 1);
    event[12] = 1;
    assert(pxa_app_on_event(event.data(), 20) ==
           static_cast<int>(pxa::Error::protocol_error));

    std::array<std::uint8_t, 21> lifecycle{};
    lifecycle[0] = 17;
    lifecycle[2] = 5;
    lifecycle[3] = 0x80;
    lifecycle[12] = 1;
    assert(pxa_app_on_event(lifecycle.data(), lifecycle.size()) == 1);
    assert(backgrounds == 1);
    lifecycle[20] = 1;
    assert(pxa_app_on_event(lifecycle.data(), lifecycle.size()) == 1);
    assert(foregrounds == 1);

    std::array<std::byte, 118> metrics{};
    pxa::wire::put16(metrics.data(), 2);
    pxa::wire::put16(metrics.data() + 2, 0x8001);
    pxa::wire::put32(metrics.data() + 12, 98);
    auto* body = metrics.data() + 20;
    const std::array<std::uint16_t, 8> offsets{0, 12, 24, 36,
                                               48, 68, 88, 93};
    const std::array<std::uint16_t, 8> lengths{8, 8, 8, 8,
                                               16, 16, 1, 1};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        pxa::wire::put16(body + offsets[i], i + 1);
        pxa::wire::put16(body + offsets[i] + 2, lengths[i]);
    }
    pxa::wire::put64(body + 4, 1);
    pxa::wire::put32(body + 16, 320);
    pxa::wire::put32(body + 20, 240);
    pxa::wire::put32(body + 28, 640);
    pxa::wire::put32(body + 32, 480);
    pxa::wire::put32(body + 40, 2);
    pxa::wire::put32(body + 44, 1);
    body[97] = std::byte{1};
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(metrics.data()),
               metrics.size()) == 1);
    assert(metric_changes == 1);
    body[97] = std::byte{2};
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(metrics.data()),
               metrics.size()) ==
           static_cast<int>(pxa::Error::protocol_error));
    assert(metric_changes == 1);

    std::array<std::byte, 20> back{};
    pxa::wire::put16(back.data(), 2);
    pxa::wire::put16(back.data() + 2, 0x8002);
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(back.data()),
               back.size()) == static_cast<int>(pxa::BackAction::stay));
    assert(back_requests == 1);

    pxa_app_stop(0);
    assert(stops == 1 && stop_rejected);
    assert(pxa_app_on_event(event.data(), 20) ==
           static_cast<int>(pxa::Error::bad_state));
    assert(pxa_app_start(nullptr, 0) == 0);
    assert(starts == 2);
    pxa_app_stop(0);
    assert(stops == 2);
}

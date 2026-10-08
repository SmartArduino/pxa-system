#include <pxa/ui.hpp>

#include <array>
#include <cassert>

extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { return 0; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }

int main() {
    using namespace pxa::ui;
    pxa::Transport transport;
    transport.phase(pxa::Phase::start);
    unsigned calls = 0;
    CanvasPointer saved;
    auto page = Page(transport, Canvas().on_pointer(
        [counter = &calls, result = &saved](const CanvasPointer& pointer) {
            ++*counter;
            *result = pointer;
        }));
    static_assert(decltype(page)::binding_capacity == 0);
    static_assert(decltype(page)::handler_capacity == 1);
    assert(page.mount());
    std::array<std::byte, 36> payload{};
    pxa::wire::put32(payload.data(), 1);
    pxa::wire::put32(payload.data() + 4, 2);
    pxa::wire::put32(payload.data() + 8, page.generation());
    pxa::wire::put16(payload.data() + 12, 7);
    pxa::wire::put16(payload.data() + 14, 5);
    pxa::wire::put64(payload.data() + 16, 123456789);
    payload[24] = std::byte{17};
    payload[25] = std::byte{pointer_phase_move};
    pxa::wire::put16(payload.data() + 26, 3);
    pxa::wire::put32(payload.data() + 28, static_cast<std::uint32_t>(-12));
    pxa::wire::put32(payload.data() + 32, 240);
    pxa::Event event{3, 0x8001, 0, payload};
    assert(page.handle(event) && calls == 1);
    assert(saved.x == -12 && saved.y == 240 && saved.pointer_id == 17);
    assert(saved.flags == 5 && saved.buttons == 3 && saved.timestamp_us == 123456789);
    for (unsigned phase = 0; phase <= pointer_phase_cancel; ++phase) {
        payload[25] = std::byte(phase);
        assert(page.handle(event));
    }
    const auto before = calls;
    payload[25] = std::byte{4};
    assert(!page.handle(event));
    payload[25] = std::byte{pointer_phase_up};
    auto truncated = event;
    truncated.payload = std::span{payload}.first(35);
    assert(!page.handle(truncated));
    auto wrong = event;
    wrong.service = 4;
    assert(!page.handle(wrong));
    wrong = event; wrong.token = 1;
    assert(!page.handle(wrong));
    pxa::wire::put32(payload.data() + 8, page.generation() + 1);
    assert(!page.handle(event));
    pxa::wire::put32(payload.data() + 8, page.generation());
    pxa::wire::put32(payload.data() + 4, 99);
    assert(!page.handle(event) && calls == before);
    payload.fill(std::byte{});
    assert(saved.x == -12 && saved.y == 240); // No borrowed event storage.
}

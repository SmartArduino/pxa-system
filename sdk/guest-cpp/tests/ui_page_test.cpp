#include <pxa/ui.hpp>

#include <cassert>
#include <vector>

static std::vector<std::vector<std::byte>> packets;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t length) {
    auto bytes = reinterpret_cast<const std::byte*>(data);
    packets.emplace_back(bytes, bytes + length);
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    return -3;
}

int main() {
    using namespace pxa::ui;
    using namespace pxa::ui::literals;

    pxa::Transport transport;
    transport.phase(pxa::Phase::start);
    State<int> count{0};
    auto page = Page(transport, Column(
        Text("Counter").font(Font::title), Text(count),
        Button("Increment").on_click([&] { count.set(1); count.set(2); })
    ).gap(8_dp).padding(16_dp));

    assert(page.mount());
    assert(page.generation() == 1);
    std::size_t initial_packets = packets.size();
    assert(initial_packets >= 3);
    assert(pxa::wire::get16(packets.back().data() + 2) == 3);

    std::array<std::byte, 24> event_bytes{};
    pxa::wire::put32(event_bytes.data(), 1);
    pxa::wire::put32(event_bytes.data() + 4, 5);
    pxa::wire::put32(event_bytes.data() + 8, 1);
    pxa::wire::put16(event_bytes.data() + 12, 1);
    pxa::Event click{3, 0x8001, 0, event_bytes};
    assert(page.handle(click));
    assert(count.get() == 2 && page.dirty());
    assert(page.flush());
    assert(!page.dirty() && page.generation() == 2);
    assert(packets.size() == initial_packets + 3);
    assert(pxa::wire::get16(packets[initial_packets + 1].data() + 2) == 2);
    assert(!page.handle(click));
    assert(page.flush());
    assert(packets.size() == initial_packets + 3);
}

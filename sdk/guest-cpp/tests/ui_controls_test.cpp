#include <pxa/ui.hpp>

#include <array>
#include <cassert>
#include <vector>

static std::vector<std::vector<std::byte>> packets;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t length) {
    auto* bytes = reinterpret_cast<const std::byte*>(data);
    packets.emplace_back(bytes, bytes + length);
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    return -3;
}

static pxa::Event event(std::uint32_t node, std::uint32_t generation,
                        std::uint16_t kind,
                        std::span<const std::byte> value) {
    static std::array<std::byte, 24> header{};
    pxa::wire::put32(header.data(), 1);
    pxa::wire::put32(header.data() + 4, node);
    pxa::wire::put32(header.data() + 8, generation);
    pxa::wire::put16(header.data() + 12, kind);
    static std::array<std::byte, 88> payload{};
    for (std::size_t i = 0; i < header.size(); ++i)
        payload[i] = header[i];
    for (std::size_t i = 0; i < value.size(); ++i)
        payload[24 + i] = value[i];
    return {3, 0x8001, 0, {payload.data(), 24 + value.size()}};
}

int main() {
    using namespace pxa::ui;
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    State<bool> enabled{false};
    State<int> level{3};
    State<std::string> name{std::string("Ada")};
    auto page = Page(transport, Column(
        Toggle("Enabled", enabled), Slider(level, 0, 10),
        Progress(level, 0, 10), TextInput(name),
        Image("assets/picture.png"), Scroll(Text("End"))));
    assert(page.mount());
    const auto initial_count = packets.size();
    assert(initial_count >= 3);

    std::array<std::byte, 4> value{};
    pxa::wire::put32(value.data(), 7);
    assert(page.handle(event(6, page.generation(), 2, value)));
    assert(level.get() == 7 && page.dirty());
    assert(page.flush());
    assert(packets.size() == initial_count + 3);
    const auto& writes = packets[initial_count + 1];
    assert(pxa::wire::get16(writes.data() + 2) == 2);
    assert(writes.size() == 20 + 4 + 14 * 2);

    pxa::wire::put32(value.data(), 1);
    assert(page.handle(event(5, page.generation(), 2, value)));
    assert(enabled.get());
    assert(page.flush());

    const std::array<std::byte, 3> text{
        std::byte{'B'}, std::byte{'o'}, std::byte{'b'}};
    assert(page.handle(event(8, page.generation(), 6, text)));
    assert(name.get() == "Bob");
    assert(page.flush());
    assert(!page.handle(event(8, page.generation() - 1, 6, text)));

    State<int> invalid{20};
    auto rejected = Page(transport, Slider(invalid, 0, 10));
    auto result = rejected.mount();
    assert(!result && result.error() == pxa::Error::invalid_argument);
}

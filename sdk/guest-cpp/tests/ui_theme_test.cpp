#include <pxa/ui_theme.hpp>
#include <pxa/events.hpp>
#include <cassert>
extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { return -1; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -1; }
int main() {
    std::array<std::byte, 60> bytes{};
    pxa::wire::put32(bytes.data(), 7); bytes[4] = std::byte{1};
    for (unsigned i = 0; i < 6; ++i) pxa::wire::put16(bytes.data()+48+2*i, 16);
    pxa::wire::put32(bytes.data()+16, 0xfa608aff);
    pxa::Event event{3, 0x8006, 0, bytes};
    assert(event.is<pxa::ui::UiAppearance>());
    auto theme = pxa::ui::decode_appearance(event);
    assert(theme && theme->dark && theme->generation == 7 && theme->colors[2] == 0xfa608aff && theme->typography[0] == 16);
    bytes[4] = std::byte{2}; assert(!pxa::ui::decode_appearance(event));
    bytes[4] = std::byte{0}; bytes[5] = std::byte{1}; assert(!pxa::ui::decode_appearance(event));
    bytes[5] = std::byte{0}; bytes[48] = std::byte{0}; assert(!pxa::ui::decode_appearance(event));
    assert(!pxa::ui::decode_appearance({3, 0x8006, 0, std::span{bytes}.first(59)}));
    assert(!pxa::ui::decode_appearance({3, 0x8002, 0, bytes}));
}

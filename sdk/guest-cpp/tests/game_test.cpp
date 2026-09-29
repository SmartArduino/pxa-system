#include <pxa/game.hpp>

#include <array>
#include <cassert>

static std::array<std::byte, 512> last_draw{};
static std::uint32_t last_draw_size = 0;
static int submits = 0;
static int binds = 0;

extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) {
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t handle, std::uint32_t operation,
                                  std::uint8_t* data, std::uint32_t size) {
    assert(handle == 77);
    if (operation == 0x103) {
        ++binds;
        assert(size == 28);
        assert(pxa::wire::get16(reinterpret_cast<std::byte*>(data)) == 2);
        return size;
    }
    assert(operation == 0x101);
    ++submits;
    last_draw_size = size;
    assert(size <= last_draw.size());
    for (std::uint32_t i = 0; i < size; ++i)
        last_draw[i] = std::byte(data[i]);
    return size;
}

int main() {
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::game::Renderer renderer(transport, 77, 1 | 8);
    std::array<std::uint64_t, 1> textures{11};
    assert(renderer.bind_assets(textures, 12));
    assert(binds == 1);

    pxa::game::DrawBuffer<512> buffer;
    std::array<pxa::game::Sprite, 1> sprites{{
        {.x = 10, .y = 20, .width = 16, .height = 16,
         .source_width = 16, .source_height = 16}}};
    auto frame = renderer.frame(buffer);
    frame.clear(pxa::game::Color565::black());
    frame.quad({0, 0, 16, 0, 16, 16, 0, 16}, {0xffff});
    frame.sprites({0}, sprites);
    assert(frame.submit());
    assert(submits == 1 && last_draw_size == 32 + 8 + 24 + 28);
    assert(pxa::wire::get32(last_draw.data()) == 0x4c525850);
    assert(pxa::wire::get32(last_draw.data() + 12) == 9);
    assert(pxa::wire::get32(last_draw.data() + 16) == 3);
    assert(pxa::wire::get64(last_draw.data() + 20) == 1);
    auto next = renderer.frame(buffer);
    next.clear(pxa::game::Color565::black());
    assert(next.submit());
    assert(pxa::wire::get64(last_draw.data() + 20) == 2);
}

#include <pxa/game.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <new>

static std::array<std::byte, 512> last_draw{};
static std::uint32_t last_draw_size = 0;
static int submits = 0;
static int binds = 0;
static int allocations = 0;

void* operator new(std::size_t size) {
    ++allocations;
    if (void* pointer = std::malloc(size)) return pointer;
    std::abort();
}
void* operator new[](std::size_t size) {
    ++allocations;
    if (void* pointer = std::malloc(size)) return pointer;
    std::abort();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

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
    pxa::game::Renderer renderer(transport, 77, 1 | 2 | 8 | 16 | 32);
    std::array<std::uint64_t, 1> textures{11};
    assert(renderer.bind_assets(textures, 12));
    assert(binds == 1);

    pxa::game::DrawBuffer<512> buffer;
    std::array<pxa::game::Sprite, 1> sprites{{
        {.x = 10, .y = 20, .width = 16, .height = 16,
         .source_width = 16, .source_height = 16}}};
    std::array<pxa::game::Vertex, 4> quad_vertices{{
        {.x_q4 = 0, .y_q4 = 0},
        {.x_q4 = 160, .y_q4 = 0, .u_q4 = 16},
        {.x_q4 = 160, .y_q4 = 160, .u_q4 = 16, .v_q4 = 16},
        {.x_q4 = 0, .y_q4 = 160, .v_q4 = 16}}};
    std::array<pxa::game::Vertex, 3> triangle_vertices{{
        {.x_q4 = 0, .y_q4 = 0},
        {.x_q4 = 160, .y_q4 = 0},
        {.x_q4 = 0, .y_q4 = 160}}};
    auto frame = renderer.frame(buffer);
    frame.clear(pxa::game::Color565::black());
    frame.quad({0, 0, 16, 0, 16, 16, 0, 16}, {0xffff});
    frame.sprites({0}, sprites);
    frame.textured_quad({0}, quad_vertices, {.affine_uv = true});
    frame.triangles({0}, triangle_vertices);
    frame.solid_triangles(triangle_vertices, {0xf800});
    assert(frame.submit());
    assert(submits == 1 && last_draw_size == 32 + 8 + 24 + 28 + 56 + 48 + 48);
    assert(pxa::wire::get32(last_draw.data()) == 0x4c525850);
    assert(pxa::wire::get32(last_draw.data() + 12) == 59);
    assert(pxa::wire::get32(last_draw.data() + 16) == 6);
    assert(pxa::wire::get64(last_draw.data() + 20) == 1);
    assert(last_draw[92] == std::byte{3} && last_draw[93] == std::byte{2});
    assert(pxa::wire::get16(last_draw.data() + 94) == 56);
    assert(pxa::wire::get16(last_draw.data() + 100 + 12) == 160);
    assert(pxa::wire::get16(last_draw.data() + 100 + 12 + 4) == 16);
    assert(pxa::wire::get16(last_draw.data() + 100 + 10) == 256);
    assert(last_draw[148] == std::byte{6});
    assert(pxa::wire::get16(last_draw.data() + 156) == 1);
    assert(last_draw[196] == std::byte{6} && last_draw[197] == std::byte{1});
    assert(pxa::wire::get16(last_draw.data() + 202) == 0xf800);
    auto next = renderer.frame(buffer);
    next.clear(pxa::game::Color565::black());
    assert(next.submit());
    assert(pxa::wire::get64(last_draw.data() + 20) == 2);

    pxa::game::Renderer limited(transport, 77, 2 | 16);
    auto unsupported = limited.frame(buffer);
    unsupported.textured_quad({0}, quad_vertices, {.affine_uv = true});
    auto unsupported_result = unsupported.submit();
    assert(!unsupported_result &&
           unsupported_result.error() == pxa::Error::unsupported);

    auto bad_options = renderer.frame(buffer);
    bad_options.textured_quad({0}, quad_vertices,
                              {.coverage_mask = true});
    auto invalid = bad_options.submit();
    assert(!invalid && invalid.error() == pxa::Error::invalid_argument);

    auto bad_batch = renderer.frame(buffer);
    bad_batch.triangles({0}, std::span{triangle_vertices}.first(2));
    invalid = bad_batch.submit();
    assert(!invalid && invalid.error() == pxa::Error::invalid_argument);

    auto missing_sprite_feature = renderer.frame(buffer);
    missing_sprite_feature.sprites(
        {0}, sprites, {.solid_color = true, .additive = true,
                       .color = {0xffff}});
    auto missing = missing_sprite_feature.submit();
    assert(!missing && missing.error() == pxa::Error::unsupported);

    auto missing_slots = renderer.frame(buffer);
    missing_slots.sprites({16}, sprites);
    missing = missing_slots.submit();
    assert(!missing && missing.error() == pxa::Error::unsupported);

    pxa::game::DrawBuffer<64> small_buffer;
    auto full = renderer.frame(small_buffer);
    full.clear(pxa::game::Color565::black());
    full.solid_triangles(triangle_vertices, {0xffff});
    auto overflow = full.submit();
    assert(!overflow && overflow.error() == pxa::Error::limit_exceeded);
    assert(submits == 2);

    const auto before = allocations;
    for (int i = 0; i < 16; ++i) {
        auto steady = renderer.frame(buffer);
        steady.clear({0}).sprites({0}, sprites)
              .solid_triangles(triangle_vertices, {0xf800});
        assert(steady.submit());
    }
    assert(submits == 18 && allocations == before);
}

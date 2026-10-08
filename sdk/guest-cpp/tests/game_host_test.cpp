#include <pxa/game.hpp>
#include <pxa/raster.h>

#include <array>
#include <cassert>
#include <cstring>

static std::array<std::uint8_t, 512> draw_list{};
static std::size_t draw_size;

extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) {
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t handle, std::uint32_t operation,
                                 std::uint8_t* bytes, std::uint32_t size) {
    assert(handle == 77 && operation == 0x101);
    assert(size <= draw_list.size());
    std::memcpy(draw_list.data(), bytes, size);
    draw_size = size;
    return size;
}

int main() {
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::game::Renderer renderer(
        transport, 77, PXA_RASTER_CAP_TEXTURED_QUAD |
                           PXA_RASTER_CAP_TRIANGLE_BATCH |
                           PXA_RASTER_CAP_AFFINE_UV);
    pxa::game::DrawBuffer<512> buffer;
    for (auto& byte : buffer.bytes()) byte = std::byte{0xaa};
    const std::array<pxa::game::Vertex, 4> quad{{
        {.x_q4 = 0, .y_q4 = 0, .light = 255},
        {.x_q4 = 112, .y_q4 = 0, .u_q4 = 16, .light = 255},
        {.x_q4 = 112, .y_q4 = 112, .u_q4 = 16, .v_q4 = 16, .light = 255},
        {.x_q4 = 0, .y_q4 = 112, .v_q4 = 16, .light = 255}}};
    const std::array<pxa::game::Vertex, 3> triangle{{
        {.x_q4 = 0, .y_q4 = 64, .light = 255},
        {.x_q4 = 64, .y_q4 = 64, .light = 255},
        {.x_q4 = 64, .y_q4 = 0, .light = 255}}};
    auto frame = renderer.frame(buffer);
    frame.clear(pxa::game::Color565::black())
         .textured_quad({0}, quad, {.affine_uv = true})
         .solid_triangles(triangle, {0xf800});
    assert(frame.submit());

    std::array<std::uint16_t, 256> palette{};
    palette[1] = 0x07e0;
    const std::array<std::uint8_t, 4> texture{1, 1, 1, 1};
    std::array<std::uint16_t, 64> pixels{};
    std::array<std::uint16_t, 64> depth{};
    depth.fill(0xffff);
    pxa_raster_resources_t resources{};
    resources.capabilities = renderer.capabilities();
    resources.palette = palette.data();
    resources.palette_light_levels = 1;
    resources.textures[0] = {texture.data(), 2, 2};
    pxa_raster_target_t target{};
    target.pixels = pixels.data();
    target.depth_pixels = depth.data();
    target.width = 8;
    target.height = 8;
    target.stride_pixels = 8;
    target.depth_stride_pixels = 8;
    target.scratch_mode = PXA_RASTER_SCRATCH_DEPTH16;
    pxa_raster_draw_list_view_t view{};
    assert(pxa_raster_validate_draw_list(draw_list.data(), draw_size, &target,
                                         &resources, &view) == PXA_STATUS_OK);
    assert(view.command_count == 3);
    assert(view.uses_depth);
    pxa_raster_execute_draw_list(draw_list.data(), &view, &target,
                                 &resources, nullptr);
    assert(pixels[1 * 8 + 1] == 0x07e0);
    assert(pixels[2 * 8 + 3] == 0xf800);
    assert(depth[2 * 8 + 3] != 0xffff);

    // Cutout leaves and opaque trunks share depth across painter quads and
    // clipped triangle batches. Transparent texels must not occlude a trunk.
    pxa::game::Renderer forest(transport, 77, PXA_RASTER_CAP_KNOWN_MASK);
    resources.capabilities = forest.capabilities();
    std::array<std::uint16_t, 16 * 256> lit_palette{};
    for (int level = 0; level < 16; ++level) lit_palette[level * 256 + 1] = 0x07e0;
    resources.palette = lit_palette.data();
    resources.palette_light_levels = 16;
    const std::array<std::uint8_t, 4> leaves_texture{1, 0, 0, 1};
    resources.textures[0] = {leaves_texture.data(), 2, 2};
    auto crown = quad;
    for (auto& vertex : crown) {vertex.depth_q8 = 512; vertex.light = 0;}
    crown[1].u_q4 = crown[2].u_q4 = 32;
    crown[2].v_q4 = crown[3].v_q4 = 32;
    const std::array<pxa::game::Vertex, 6> trunk{{
        {.x_q4=0,.y_q4=0,.depth_q8=1024},
        {.x_q4=112,.y_q4=0,.depth_q8=1024},
        {.x_q4=112,.y_q4=112,.depth_q8=1024},
        {.x_q4=0,.y_q4=0,.depth_q8=1024},
        {.x_q4=112,.y_q4=112,.depth_q8=1024},
        {.x_q4=0,.y_q4=112,.depth_q8=1024}}};
    for (bool leaves_first : {true, false}) {
        auto tree = forest.frame(buffer);
        tree.clear({0x001f});
        if (!leaves_first) tree.solid_triangles(trunk, {0xf800});
        tree.textured_quad({0}, crown, {.painter=true,.transparent_index0=true,.lit_palette=true});
        if (leaves_first) tree.solid_triangles(trunk, {0xf800});
        assert(tree.submit());
        assert(pxa_raster_validate_draw_list(draw_list.data(), draw_size, &target,
                                              &resources, &view) == PXA_STATUS_OK);
        pxa_raster_execute_draw_list(draw_list.data(), &view, &target, &resources, nullptr);
        assert(pixels[1 * 8 + 1] == 0x07e0);
        assert(pixels[1 * 8 + 5] == 0xf800);
        assert(pixels[5 * 8 + 1] == 0xf800);
        assert(pixels[5 * 8 + 5] == 0x07e0);
    }
}

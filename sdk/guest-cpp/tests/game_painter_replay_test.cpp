// Replay test: build a painter draw list through the C++ SDK exactly the way
// the voxel terrain does, then feed the emitted bytes straight into the host
// rasteriser. This is the shortest path from "the device shows a black frame"
// to a reproducible host-side failure, without a Guest in the loop.
#include <pxa/game.hpp>

extern "C" {
#include "pxa/raster.h"
}

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<std::byte> captured;

extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) {
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t operation,
                               std::uint8_t* data, std::uint32_t size) {
    if (operation == 0x101) {
        auto* bytes = reinterpret_cast<const std::byte*>(data);
        captured.assign(bytes, bytes + size);
    }
    return static_cast<std::int32_t>(size);
}

namespace {

constexpr std::uint32_t kCaps =
    pxa::game::capability_bit(pxa::game::RenderCapability::textured_quad) |
    pxa::game::capability_bit(pxa::game::RenderCapability::flat_quad) |
    pxa::game::capability_bit(pxa::game::RenderCapability::triangle_batch) |
    pxa::game::capability_bit(pxa::game::RenderCapability::affine_uv) |
    pxa::game::capability_bit(pxa::game::RenderCapability::painter_polygon) |
    pxa::game::capability_bit(pxa::game::RenderCapability::lit_palette_depth) |
    pxa::game::capability_bit(pxa::game::RenderCapability::painter_depth) |
    pxa::game::capability_bit(pxa::game::RenderCapability::depth_cutout) |
    pxa::game::capability_bit(pxa::game::RenderCapability::texture_slots_48);

}  // namespace

int main() {
    using namespace pxa::game;

    /* 1. Emit a terrain-like painter list through the SDK. */
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    RenderInfo info{};
    info.capabilities = kCaps;
    info.max_textures = 48;
    info.max_draw_bytes = 49152;
    Renderer renderer(transport, 7, info);
    DrawBuffer<49152> buffer;
    {
        auto frame = renderer.frame(buffer);
        frame.clear({0x867d});
        PolygonOptions options;
        options.painter = true;
        options.lit_palette = true;
        options.affine_uv = false;
        /* 200 merged quads across a few texture slots, UVs spanning four
         * blocks, real camera depth, palette light rows 0..6. */
        for (int index = 0; index < 200; ++index) {
            const float x0 = static_cast<float>((index * 13) % 260);
            const float y0 = static_cast<float>(40 + (index * 7) % 150);
            const float w = 32.0f + static_cast<float>(index % 5) * 8.0f;
            const float h = 24.0f;
            const float z = 4.0f + static_cast<float>(index % 40) * 0.7f;
            const std::uint16_t depth = pxa::game::painter_depth_from_z(z);
            const std::uint8_t light = static_cast<std::uint8_t>(index % 7);
            const std::uint8_t slot = static_cast<std::uint8_t>(index % 8);
            const std::array<Vertex, 4> quad{{
                {.x_q4 = static_cast<std::int16_t>(x0 * 16),
                 .y_q4 = static_cast<std::int16_t>(y0 * 16), .u_q4 = 0,
                 .v_q4 = 0, .light = light, .depth_q8 = depth},
                {.x_q4 = static_cast<std::int16_t>((x0 + w) * 16),
                 .y_q4 = static_cast<std::int16_t>(y0 * 16), .u_q4 = 1024,
                 .v_q4 = 0, .light = light, .depth_q8 = depth},
                {.x_q4 = static_cast<std::int16_t>((x0 + w) * 16),
                 .y_q4 = static_cast<std::int16_t>((y0 + h) * 16), .u_q4 = 1024,
                 .v_q4 = 512, .light = light, .depth_q8 = depth},
                {.x_q4 = static_cast<std::int16_t>(x0 * 16),
                 .y_q4 = static_cast<std::int16_t>((y0 + h) * 16), .u_q4 = 0,
                 .v_q4 = 512, .light = light, .depth_q8 = depth}}};
            frame.textured_quad(AtlasBinding{slot}, quad, options);
        }
        const auto submitted = frame.submit();
        assert(submitted);
    }
    assert(!captured.empty());
    std::printf("captured draw list: %zu bytes\n", captured.size());

    /* 2. Replay it through the host rasteriser. */
    constexpr int kWidth = 296;
    constexpr int kHeight = 240;
    static std::array<std::uint16_t, kWidth * kHeight> pixels{};
    static std::array<std::uint16_t, kWidth * kHeight> depth{};
    static std::uint8_t texels[16 * 16];
    static std::uint16_t palette[16 * 256];
    for (unsigned i = 0; i < sizeof(texels); ++i)
        texels[i] = static_cast<std::uint8_t>(1 + (i % 7));
    for (unsigned level = 0; level < 16; ++level)
        for (unsigned index = 0; index < 256; ++index)
            palette[level * 256 + index] =
                static_cast<std::uint16_t>(level == 0 ? 0x07e0 : 0x0400);

    pxa_raster_resources_t resources = {};
    resources.palette = palette;
    resources.palette_light_levels = 16;
    resources.capabilities = PXA_RASTER_CAP_TEXTURED_QUAD |
                             PXA_RASTER_CAP_PAINTER_POLYGON |
                             PXA_RASTER_CAP_LIT_PALETTE_DEPTH |
                             PXA_RASTER_CAP_PAINTER_DEPTH |
                             PXA_RASTER_CAP_TEXTURE_SLOTS_48;
    for (unsigned slot = 0; slot < 8; ++slot) {
        resources.textures[slot].pixels = texels;
        resources.textures[slot].width = 16;
        resources.textures[slot].height = 16;
    }
    pxa_raster_target_t target = {};
    target.pixels = pixels.data();
    target.depth_pixels = depth.data();
    target.width = kWidth;
    target.height = kHeight;
    target.stride_pixels = kWidth;
    target.depth_stride_pixels = kWidth;
    target.scratch_mode = PXA_RASTER_SCRATCH_DEPTH16;
    std::memset(depth.data(), 0, depth.size() * sizeof(depth[0]));

    pxa_raster_draw_list_view_t list;
    const auto status = pxa_raster_validate_draw_list(
        reinterpret_cast<const std::uint8_t*>(captured.data()), captured.size(),
        &target, &resources, &list);
    std::printf("validate status=%d commands=%u\n", static_cast<int>(status),
                static_cast<unsigned>(list.command_count));
    assert(status == PXA_STATUS_OK);
    pxa_raster_execute_draw_list(
        reinterpret_cast<const std::uint8_t*>(captured.data()), &list, &target,
        &resources, NULL);
    unsigned lit = 0;
    for (std::size_t i = 0; i < pixels.size(); ++i)
        if (pixels[i] != 0) ++lit;
    std::printf("lit pixels=%u\n", lit);
    assert(lit > 0);
    return 0;
}

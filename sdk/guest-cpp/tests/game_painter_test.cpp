// Painter polygon encoding: flags, required capabilities and the lit palette.
#include <pxa/game.hpp>

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
/* Submit goes through pxa_io (0x101); capture the draw list exactly as the
 * host receives it. */
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t operation,
                               std::uint8_t* data, std::uint32_t size) {
    if (operation == 0x101) {
        auto* bytes = reinterpret_cast<const std::byte*>(data);
        packets.emplace_back(bytes, bytes + size);
    }
    return static_cast<std::int32_t>(size);
}

namespace {

constexpr std::uint32_t kFullCaps =
    pxa::game::capability_bit(pxa::game::RenderCapability::textured_quad) |
    pxa::game::capability_bit(pxa::game::RenderCapability::triangle_batch) |
    pxa::game::capability_bit(pxa::game::RenderCapability::affine_uv) |
    pxa::game::capability_bit(pxa::game::RenderCapability::painter_polygon) |
    pxa::game::capability_bit(pxa::game::RenderCapability::lit_palette_depth) |
    pxa::game::capability_bit(pxa::game::RenderCapability::painter_depth) |
    pxa::game::capability_bit(pxa::game::RenderCapability::texture_slots_48);

std::array<pxa::game::Vertex, 4> quad_vertices() {
    return {{
        {.x_q4 = 0, .y_q4 = 0, .u_q4 = 0, .v_q4 = 0, .light = 0, .depth_q8 = 8},
        {.x_q4 = 640, .y_q4 = 0, .u_q4 = 256, .v_q4 = 0, .light = 0, .depth_q8 = 8},
        {.x_q4 = 640, .y_q4 = 640, .u_q4 = 256, .v_q4 = 256, .light = 0, .depth_q8 = 8},
        {.x_q4 = 0, .y_q4 = 640, .u_q4 = 0, .v_q4 = 256, .light = 0, .depth_q8 = 8},
    }};
}

}  // namespace

int main() {
    using namespace pxa::game;

    /* Painter + lit palette is the combination the host advertises through
     * PAINTER_DEPTH; it must encode as one batch with both flag bits set. */
    {
        pxa::Transport transport;
        transport.phase(pxa::Phase::event);
        RenderInfo info{};
        info.capabilities = kFullCaps;
        info.max_textures = 48;
        info.max_draw_bytes = 4096;
        Renderer renderer(transport, 1, info);
        DrawBuffer<4096> buffer;
        auto frame = renderer.frame(buffer);
        PolygonOptions options;
        options.affine_uv = true;
        options.painter = true;
        options.lit_palette = true;
        const auto vertices = quad_vertices();
        frame.textured_quad(AtlasBinding{0}, vertices, options);
        const auto used = frame.bytes_used();
        assert(used > 32);
        const auto submitted = frame.submit();
        assert(submitted);
        assert(!packets.empty());

        /* Walk the draw list records: [type][flags][size u16][slot]... */
        bool saw_painter_batch = false;
        for (const auto& packet : packets) {
            for (std::size_t i = 32; i + 12 <= packet.size();) {
                const auto type = std::to_integer<std::uint8_t>(packet[i]);
                const auto size = pxa::wire::get16(packet.data() + i + 2);
                if (size == 0) break;
                const auto flags = std::to_integer<std::uint8_t>(packet[i + 1]);
                if (type == 3 && (flags & 0x04) && (flags & 0x10)) {
                    saw_painter_batch = true;
                    /* A textured quad record carries four 12 byte vertices. */
                    assert(size == 56);
                    assert(std::to_integer<std::uint8_t>(packet[i + 4]) == 0);
                    assert(pxa::wire::get16(packet.data() + i + 8 + 10) == 8);
                }
                i += size;
            }
        }
        assert(saw_painter_batch);
    }

    /* Without PAINTER_DEPTH the batch must be dropped, not encoded. */
    {
        pxa::Transport transport;
        transport.phase(pxa::Phase::event);
        RenderInfo info{};
        info.capabilities =
            pxa::game::capability_bit(RenderCapability::textured_quad) |
            pxa::game::capability_bit(RenderCapability::triangle_batch) |
            pxa::game::capability_bit(RenderCapability::affine_uv) |
            pxa::game::capability_bit(RenderCapability::painter_polygon) |
            pxa::game::capability_bit(RenderCapability::lit_palette_depth);
        info.max_textures = 48;
        info.max_draw_bytes = 4096;
        Renderer renderer(transport, 1, info);
        DrawBuffer<4096> buffer;
        auto frame = renderer.frame(buffer);
        PolygonOptions options;
        options.affine_uv = true;
        options.painter = true;
        options.lit_palette = true;
        const auto vertices = quad_vertices();
        frame.textured_quad(AtlasBinding{0}, vertices, options);
        assert(frame.bytes_used() == 32); /* header only: nothing appended */
    }

    /* The painter depth helper matches the projector's encoding. */
    assert(pxa::game::painter_depth_from_z(1.0f) == 256);
    assert(pxa::game::painter_depth_from_z(0.0f) == 0);
    assert(pxa::game::painter_depth_from_z(0.001f) == 1);
    assert(pxa::game::painter_depth_from_z(255.0f) == 65280);
    assert(pxa::game::painter_depth_from_z(1000.0f) == 65535);

    /* A quad collapsed onto a screen edge has no area: the host rejects the
     * whole list for that, so the SDK must drop it silently. */
    {
        pxa::Transport transport;
        transport.phase(pxa::Phase::event);
        RenderInfo info{};
        info.capabilities = kFullCaps;
        info.max_textures = 48;
        info.max_draw_bytes = 4096;
        Renderer renderer(transport, 1, info);
        DrawBuffer<4096> buffer;
        auto frame = renderer.frame(buffer);
        PolygonOptions options;
        options.painter = true;
        options.lit_palette = true;
        const std::array<Vertex, 4> flat{{
            {.x_q4 = 0, .y_q4 = 0, .depth_q8 = 256},
            {.x_q4 = 640, .y_q4 = 0, .depth_q8 = 256},
            {.x_q4 = 640, .y_q4 = 0, .depth_q8 = 256},
            {.x_q4 = 0, .y_q4 = 0, .depth_q8 = 256},
        }};
        frame.textured_quad(AtlasBinding{0}, flat, options);
        assert(frame.bytes_used() == 32);
    }

    /* Coverage masks stay rejected: they conflict with the palette paths. */
    {
        pxa::Transport transport;
        transport.phase(pxa::Phase::event);
        RenderInfo info{};
        info.capabilities = kFullCaps;
        info.max_textures = 48;
        info.max_draw_bytes = 4096;
        Renderer renderer(transport, 1, info);
        DrawBuffer<4096> buffer;
        auto frame = renderer.frame(buffer);
        PolygonOptions options;
        options.coverage_mask = true;
        options.painter = true;
        const auto vertices = quad_vertices();
        frame.textured_quad(AtlasBinding{0}, vertices, options);
        assert(frame.bytes_used() == 32);
    }

    return 0;
}

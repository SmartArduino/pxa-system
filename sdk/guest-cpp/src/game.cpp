#include "pxa/game.hpp"

namespace pxa::game {
namespace {

constexpr std::uint8_t solid_color = 1;
constexpr std::uint8_t affine_uv = 2;
constexpr std::uint8_t painter = 4;
constexpr std::uint8_t transparent_index0 = 8;
constexpr std::uint8_t lit_palette = 16;
constexpr std::uint8_t blend_75 = 32;
constexpr std::uint8_t coverage_mask = 64;
constexpr std::uint8_t coverage_resolve = 128;

std::uint8_t flags_for(PolygonOptions options) noexcept {
    return (options.affine_uv ? affine_uv : 0) |
           (options.painter ? painter : 0) |
           (options.transparent_index0 ? transparent_index0 : 0) |
           (options.lit_palette ? lit_palette : 0) |
           (options.blend_75 ? blend_75 : 0) |
           (options.coverage_mask ? coverage_mask : 0) |
           (options.coverage_resolve ? coverage_resolve : 0);
}

std::uint32_t capabilities_for(std::uint8_t flags) noexcept {
    std::uint32_t required = 0;
    if (flags & affine_uv) required |= capability_bit(RenderCapability::affine_uv);
    if (flags & painter) required |= capability_bit(RenderCapability::painter_polygon);
    if (flags & lit_palette) required |= capability_bit(RenderCapability::lit_palette_depth);
    if ((flags & transparent_index0) && !(flags & painter))
        required |= capability_bit(RenderCapability::depth_cutout);
    if (flags & blend_75) required |= capability_bit(RenderCapability::blend_75);
    if (flags & coverage_mask) required |= capability_bit(RenderCapability::coverage_mask);
    if ((flags & coverage_mask) && !(flags & affine_uv))
        required |= capability_bit(RenderCapability::painter_perspective);
    if ((flags & painter) && (flags & lit_palette))
        required |= capability_bit(RenderCapability::painter_depth);
    return required;
}

void write_vertex(std::byte* out, const Vertex& vertex) noexcept {
    wire::put16(out, static_cast<std::uint16_t>(vertex.x_q4));
    wire::put16(out + 2, static_cast<std::uint16_t>(vertex.y_q4));
    wire::put16(out + 4, static_cast<std::uint16_t>(vertex.u_q4));
    wire::put16(out + 6, static_cast<std::uint16_t>(vertex.v_q4));
    out[8] = std::byte(vertex.light);
    out[9] = std::byte{};
    wire::put16(out + 10, vertex.depth_q8);
}

bool nonzero_triangle(const Vertex& a, const Vertex& b, const Vertex& c) noexcept {
    return std::int64_t(b.x_q4 - a.x_q4) * (c.y_q4 - a.y_q4) !=
           std::int64_t(b.y_q4 - a.y_q4) * (c.x_q4 - a.x_q4);
}

} // namespace

bool Frame::valid_texture_slot(AtlasBinding binding) noexcept {
    if (binding.slot >= 48) {
        error_ = Error::invalid_argument;
        return false;
    }
    if (renderer_.info().max_textures &&
        binding.slot >= renderer_.info().max_textures) {
        error_ = Error::unsupported;
        return false;
    }
    return binding.slot < 16 ||
           supports(capability_bit(RenderCapability::texture_slots_48));
}

Frame& Frame::textured_quad(AtlasBinding binding,
                            const std::array<Vertex, 4>& vertices,
                            PolygonOptions options) noexcept {
    if (error_ || !valid_texture_slot(binding)) return *this;
    const auto flags = flags_for(options);
    const bool invalid_coverage =
        (flags & coverage_mask) && (!(flags & painter) || (flags & lit_palette));
    const bool invalid_resolve =
        (flags & coverage_resolve) &&
        (flags & (coverage_mask | blend_75)) !=
            (coverage_mask | blend_75);
    if (invalid_coverage || invalid_resolve) {
        error_ = Error::invalid_argument;
        return *this;
    }
    const auto required = capability_bit(RenderCapability::textured_quad) |
                          capabilities_for(flags);
    if (!supports(required)) return *this;
    /* The host rejects a whole draw list when a polygon has zero projected
     * area, which happens whenever clipping collapses a quad onto a screen
     * edge. Drop such quads here, mirroring the triangle path's degenerate
     * triangle filter. */
    {
        const auto area2 = [&vertices] {
            std::int64_t total = 0;
            for (std::size_t i = 0; i < vertices.size(); ++i) {
                const auto& a = vertices[i];
                const auto& b = vertices[(i + 1) % vertices.size()];
                total += static_cast<std::int64_t>(a.x_q4) * b.y_q4 -
                         static_cast<std::int64_t>(b.x_q4) * a.y_q4;
            }
            return total;
        }();
        if (area2 == 0) return *this;
    }
    auto record = append(3, 56);
    if (record.empty()) return *this;
    record[1] = std::byte(flags);
    record[4] = std::byte(binding.slot);
    for (std::size_t i = 0; i < vertices.size(); ++i)
        write_vertex(record.data() + 8 + i * 12, vertices[i]);
    required_ |= required;
    return *this;
}

Frame& Frame::solid_depth_quad(const std::array<Vertex, 4>& vertices,
                               Color565 color, bool scanline) noexcept {
    const auto flags = static_cast<std::uint8_t>(solid_color |
        (scanline ? painter | lit_palette : 0));
    const auto required = capability_bit(RenderCapability::textured_quad) |
                          capabilities_for(flags);
    if (error_ || !supports(required)) return *this;
    auto record = append(3, 56);
    if (record.empty()) return *this;
    record[1] = std::byte(flags);
    wire::put16(record.data() + 6, color.value);
    for (std::size_t i = 0; i < vertices.size(); ++i)
        write_vertex(record.data() + 8 + i * 12, vertices[i]);
    required_ |= required;
    return *this;
}

bool Frame::reserve_depth_polygon(std::span<const Vertex> vertices) noexcept {
    if (error_) return false;
    if (vertices.size() < 3 || vertices.size() > 10) {
        error_ = Error::invalid_argument;
        return false;
    }
    std::size_t records = 0;
    if (vertices.size() == 4) {
        std::int64_t area = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& a = vertices[i];
            const auto& b = vertices[(i + 1) & 3];
            area += std::int64_t(a.x_q4) * b.y_q4 - std::int64_t(b.x_q4) * a.y_q4;
        }
        if (!area) return false;
        records = 1;
    } else {
        for (std::size_t i = 1; i + 1 < vertices.size(); ++i)
            records += nonzero_triangle(vertices[0], vertices[i], vertices[i + 1]);
    }
    if (records > 1024 - commands_ || records * 56 > bytes_.size() - used_) {
        error_ = Error::limit_exceeded;
        return false;
    }
    return true;
}

Frame& Frame::textured_depth_polygon(AtlasBinding binding,
                                     std::span<const Vertex> vertices,
                                     PolygonOptions options) noexcept {
    if (!reserve_depth_polygon(vertices)) return *this;
    options.painter = options.lit_palette = true;
    if (vertices.size() == 4) {
        textured_quad(binding, {vertices[0], vertices[1], vertices[2], vertices[3]}, options);
    } else {
        for (std::size_t i = 1; i + 1 < vertices.size(); ++i)
            if (nonzero_triangle(vertices[0], vertices[i], vertices[i + 1]))
                textured_quad(binding, {vertices[0], vertices[i], vertices[i + 1], vertices[i + 1]}, options);
    }
    return *this;
}

Frame& Frame::solid_depth_polygon(std::span<const Vertex> vertices,
                                  Color565 color) noexcept {
    if (!reserve_depth_polygon(vertices)) return *this;
    if (vertices.size() == 4) {
        solid_depth_quad({vertices[0], vertices[1], vertices[2], vertices[3]}, color, true);
    } else {
        for (std::size_t i = 1; i + 1 < vertices.size(); ++i)
            if (nonzero_triangle(vertices[0], vertices[i], vertices[i + 1]))
                solid_depth_quad({vertices[0], vertices[i], vertices[i + 1], vertices[i + 1]}, color, true);
    }
    return *this;
}

Frame& Frame::append_triangles(std::span<const Vertex> vertices,
                               AtlasBinding binding, std::uint8_t flags,
                               Color565 color,
                               std::uint32_t capabilities) noexcept {
    if (error_) return *this;
    if (vertices.empty() || vertices.size() % 3 != 0 ||
        vertices.size() > (UINT16_MAX - 12) / 12) {
        error_ = Error::invalid_argument;
        return *this;
    }
    if (!supports(capabilities)) return *this;
    auto record = append(6, static_cast<std::uint16_t>(12 + vertices.size() * 12));
    if (record.empty()) return *this;
    record[1] = std::byte(flags);
    record[4] = std::byte(binding.slot);
    wire::put16(record.data() + 6, color.value);
    wire::put16(record.data() + 8,
                static_cast<std::uint16_t>(vertices.size() / 3));
    for (std::size_t i = 0; i < vertices.size(); ++i)
        write_vertex(record.data() + 12 + i * 12, vertices[i]);
    required_ |= capabilities;
    return *this;
}

Frame& Frame::triangles(AtlasBinding binding,
                        std::span<const Vertex> vertices,
                        PolygonOptions options) noexcept {
    if (error_ || !valid_texture_slot(binding)) return *this;
    const auto flags = flags_for(options);
    /* The host's triangle-batch validation rejects painter with the lit
     * palette; only the textured-quad path supports that combination. */
    if ((flags & (coverage_mask | coverage_resolve)) ||
        ((flags & painter) && (flags & lit_palette))) {
        error_ = Error::invalid_argument;
        return *this;
    }
    return append_triangles(vertices, binding, flags, {},
                            capability_bit(RenderCapability::triangle_batch) |
                                capabilities_for(flags));
}

Frame& Frame::solid_triangles(std::span<const Vertex> vertices,
                              Color565 color) noexcept {
    return append_triangles(
        vertices, {}, solid_color, color,
        capability_bit(RenderCapability::triangle_batch));
}

Frame& Frame::palette_triangles(std::span<const Vertex> vertices,
                                std::uint8_t palette_index) noexcept {
    return append_triangles(vertices, {}, solid_color | painter,
        Color565{palette_index}, capability_bit(RenderCapability::triangle_batch) |
                                  capability_bit(RenderCapability::painter_polygon));
}

} // namespace pxa::game

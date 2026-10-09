#pragma once
#include "game.hpp"
#include <algorithm>
#include <cstdint>
#include <span>

namespace pxa::game3d {
enum class MaterialPath : std::uint8_t { perspective, affine, solid };
struct MaterialPolicy {
    // A nonzero threshold explicitly permits average-colour approximation.
    std::uint16_t solid_extent_q4 = 4 * 16;
};
// max_uv_texels is the maximum original UV span in texels, including repeats.
// The bound is conservative for a convex coplanar primitive: inverse depth is
// affine, and Q8 quantization is allowed another full unit of depth spread.
inline MaterialPath choose_material_path(std::span<const game::Vertex> vertices,
        std::uint32_t max_uv_texels, bool cutout, MaterialPolicy policy = {}) noexcept {
    if (vertices.size() < 3 || !max_uv_texels) return MaterialPath::perspective;
    int min_x=vertices[0].x_q4, max_x=min_x, min_y=vertices[0].y_q4, max_y=min_y;
    unsigned min_z=vertices[0].depth_q8, max_z=min_z;
    for (const auto& v : vertices) {
        min_x=std::min(min_x, int(v.x_q4)); max_x=std::max(max_x, int(v.x_q4));
        min_y=std::min(min_y, int(v.y_q4)); max_y=std::max(max_y, int(v.y_q4));
        min_z=std::min(min_z, unsigned(v.depth_q8)); max_z=std::max(max_z, unsigned(v.depth_q8));
    }
    if (!min_z) return MaterialPath::perspective;
    if (!cutout && policy.solid_extent_q4 &&
        (max_x-min_x < policy.solid_extent_q4 || max_y-min_y < policy.solid_extent_q4))
        return MaterialPath::solid;
    if (std::uint64_t(max_z-min_z+1)*max_uv_texels <= 2*min_z-1)
        return MaterialPath::affine;
    return MaterialPath::perspective;
}
} // namespace pxa::game3d

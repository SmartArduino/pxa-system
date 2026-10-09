#pragma once

#include "game.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <new>
#include <type_traits>

namespace pxa::game3d {

namespace detail {
// The wire's bounded Q4/Q8 values fit in 17 magnitude bits. IEEE-754 rounding
// avoids libc and Wasm's trapping float-to-integer conversions on AOT targets.
// Precondition: finite value with magnitude <= 65536; half ties go away from 0.
inline std::int32_t round_wire_value(float value) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto exponent = (bits >> 23) & 255u;
    if (exponent < 126) return 0;
    if (exponent >= 144) return bits >> 31 ? -65536 : 65536;
    const auto shift = 150u - exponent;
    const auto magnitude = ((bits & 0x7fffffu) | 0x800000u) + (1u << (shift - 1));
    const auto rounded = static_cast<std::int32_t>(magnitude >> shift);
    return bits >> 31 ? -rounded : rounded;
}
} // namespace detail


struct Vec3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

struct MeshVertex {
    Vec3 position;
    float u = 0;
    float v = 0;
    float light = 255;
};

// Integer attributes are already in the wire's Q4/palette-row format. This
// avoids converting constant/repeated mesh UVs and lighting for every frame.
struct FixedMeshVertex {
    Vec3 position;
    std::int16_t u_q4 = 0;
    std::int16_t v_q4 = 0;
    std::uint8_t light = 255;
};

struct Transform {
    Vec3 x_axis{1, 0, 0};
    Vec3 y_axis{0, 1, 0};
    Vec3 z_axis{0, 0, 1};
    Vec3 origin{};

    static Transform rotation_y(float radians) noexcept {
        const float sine = std::sin(radians);
        const float cosine = std::cos(radians);
        return {{cosine, 0, -sine}, {0, 1, 0}, {sine, 0, cosine}, {}};
    }

    Vec3 apply(Vec3 point) const noexcept {
        return {origin.x + x_axis.x * point.x + y_axis.x * point.y + z_axis.x * point.z,
                origin.y + x_axis.y * point.x + y_axis.y * point.y + z_axis.y * point.z,
                origin.z + x_axis.z * point.x + y_axis.z * point.y + z_axis.z * point.z};
    }
};

enum class FrontFace { clockwise, counter_clockwise, both };

class Projector {
public:
    static Result<Projector> create(std::uint16_t width, std::uint16_t height,
                                    float vertical_fov_radians, float near_z,
                                    float far_z) noexcept {
        if (!width || !height || width > 2047 || height > 2047 ||
            !std::isfinite(vertical_fov_radians) ||
            vertical_fov_radians <= 0 ||
            vertical_fov_radians >= 3.14159265f ||
            !std::isfinite(near_z) || !std::isfinite(far_z) ||
            near_z < 1.0f / 256.0f || far_z > 255.0f || near_z >= far_z)
            return std::unexpected(Error::invalid_argument);
        const float focal = static_cast<float>(height) /
                            (2.0f * std::tan(vertical_fov_radians / 2.0f));
        if (!std::isfinite(focal) || focal <= 0)
            return std::unexpected(Error::invalid_argument);
        return Projector(width, height, focal, near_z, far_z);
    }

    // Change the visibility range without recomputing the focal length/FOV.
    Result<void> clip_range(float near_z, float far_z) noexcept {
        if (!std::isfinite(near_z) || !std::isfinite(far_z) ||
            near_z < 1.0f/256.0f || far_z > 255.0f || near_z >= far_z)
            return std::unexpected(Error::invalid_argument);
        near_ = near_z; far_ = far_z;
        return {};
    }
    float focal_length() const noexcept { return focal_; }

    // Six frustum planes can add at most six vertices to a convex polygon.
    static constexpr std::size_t max_polygon_vertices = 10;
    static constexpr std::size_t max_triangle_vertices = 21;

    // Fixed attributes use the same clipping/depth semantics as MeshVertex.
    // Only crossing primitives need float attribute interpolation.
    Result<std::size_t> project_polygon_fixed(
        std::span<const FixedMeshVertex> polygon,
        std::span<game::Vertex> output) const noexcept {
        if(polygon.size()<3 || polygon.size()>4)
            return std::unexpected(Error::invalid_argument);
        unsigned crossed=0,outside=63;
        for(const auto& vertex:polygon) {
            const auto p=vertex.position;
            if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                return std::unexpected(Error::invalid_argument);
            const float hx=p.z*horizontal_,hy=p.z*vertical_;
            const unsigned code=(p.z<near_?1u:0u)|(p.z>far_?2u:0u)|
                (p.x < -hx?4u:0u)|(p.x>hx?8u:0u)|(p.y < -hy?16u:0u)|(p.y>hy?32u:0u);
            crossed|=code;outside&=code;
        }
        if(outside) return std::size_t{0};
        if(crossed) {
            std::array<MeshVertex,4> interpolated{};
            for(std::size_t i=0;i<polygon.size();++i) interpolated[i]={polygon[i].position,
                polygon[i].u_q4/16.0f,polygon[i].v_q4/16.0f,float(polygon[i].light)};
            return clip_and_project(std::span{interpolated}.first(polygon.size()), output, crossed);
        }
        if(output.size()<polygon.size()) return std::unexpected(Error::limit_exceeded);
        for(std::size_t i=0;i<polygon.size();++i) {
            const auto& source=polygon[i];
            const float scale=focal_/source.position.z;
            const float x=width_/2.0f+source.position.x*scale;
            const float y=height_/2.0f+source.position.y*scale;
            if(!std::isfinite(x) || !std::isfinite(y))
                return std::unexpected(Error::invalid_argument);
            output[i]={
                .x_q4=static_cast<std::int16_t>(std::clamp(detail::round_wire_value(x*16),0,int(width_)*16)),
                .y_q4=static_cast<std::int16_t>(std::clamp(detail::round_wire_value(y*16),0,int(height_)*16)),
                .u_q4=source.u_q4,.v_q4=source.v_q4,.light=source.light,
                .depth_q8=static_cast<std::uint16_t>(std::clamp(
                    detail::round_wire_value(source.position.z*256),1,65535))};
        }
        return polygon.size();
    }

    // Projects a convex triangle or quad, preserving perimeter order and UVs
    // through clipping. The returned vertices are a polygon, not triangles.
    Result<std::size_t> project_polygon(
        std::span<const MeshVertex> polygon,
        std::span<game::Vertex> output) const noexcept {
        if (polygon.size() < 3 || polygon.size() > 4)
            return std::unexpected(Error::invalid_argument);
        unsigned crossed = 0, outside = 63;
        for (const auto& vertex : polygon) {
            if (!valid(vertex)) return std::unexpected(Error::invalid_argument);
            const auto p = vertex.position;
            const float extent_x = p.z * horizontal_, extent_y = p.z * vertical_;
            const unsigned code = (p.z < near_ ? 1u : 0u) | (p.z > far_ ? 2u : 0u) |
                (p.x < -extent_x ? 4u : 0u) | (p.x > extent_x ? 8u : 0u) |
                (p.y < -extent_y ? 16u : 0u) | (p.y > extent_y ? 32u : 0u);
            crossed |= code;
            outside &= code;
        }
        if (outside) return std::size_t{0};
        // Most visible terrain faces need no clipping or intermediate copy.
        if (!crossed) return project_vertices(polygon, output);
        return clip_and_project(polygon, output, crossed);
    }

private:
    Result<std::size_t> clip_and_project(std::span<const MeshVertex> polygon,
                                       std::span<game::Vertex> output,
                                       unsigned crossed) const noexcept {
        std::size_t count = polygon.size();
        // Keep the same bounded stack storage, but construct only emitted
        // vertices: MeshVertex's default member initializers would otherwise
        // write both entire arrays on every clipped face. Placement copy
        // construction allocates nothing; trivial destruction allows reuse.
        static_assert(std::is_trivially_copyable_v<MeshVertex> &&
                      std::is_trivially_destructible_v<MeshVertex>);
        alignas(MeshVertex) std::byte a[sizeof(MeshVertex) * max_polygon_vertices];
        alignas(MeshVertex) std::byte b[sizeof(MeshVertex) * max_polygon_vertices];
        auto* input = reinterpret_cast<MeshVertex*>(a);
        auto* clipped = reinterpret_cast<MeshVertex*>(b);
        for (std::size_t i = 0; i < count; ++i)
            ::new (static_cast<void*>(input + i)) MeshVertex(polygon[i]);
        for (unsigned plane = 0; plane < 6; ++plane) {
            if (!(crossed & (1u << plane))) continue;
            if (!count) return std::size_t{0};
            std::size_t next = 0;
            auto emit = [&](const MeshVertex& vertex) {
                if (next && same_position(clipped[next - 1], vertex)) return true;
                if (next == max_polygon_vertices) return false;
                ::new (static_cast<void*>(clipped + next++)) MeshVertex(vertex);
                return true;
            };
            auto previous = input[count - 1];
            float previous_distance = distance(previous.position, plane);
            for (std::size_t i = 0; i < count; ++i) {
                const auto current = input[i];
                const float current_distance = distance(current.position, plane);
                if ((previous_distance >= 0) != (current_distance >= 0)) {
                    const float t = previous_distance /
                                    (previous_distance - current_distance);
                    if (!emit(interpolate(previous, current, t)))
                        return std::unexpected(Error::limit_exceeded);
                }
                if (current_distance >= 0) {
                    if (!emit(current))
                        return std::unexpected(Error::limit_exceeded);
                }
                previous = current;
                previous_distance = current_distance;
            }
            if (next > 1 && same_position(clipped[0], clipped[next - 1])) --next;
            std::swap(input, clipped);
            count = next;
        }
        return project_vertices({input, count}, output);
    }

    Result<std::size_t> project_vertices(std::span<const MeshVertex> input,
                                        std::span<game::Vertex> output) const noexcept {
        const auto count = input.size();
        if (count < 3) return std::size_t{0};
        if (count > output.size())
            return std::unexpected(Error::limit_exceeded);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& source = input[i];
            const float scale = focal_ / source.position.z;
            const float screen_x = width_ / 2.0f + source.position.x * scale;
            const float screen_y = height_ / 2.0f + source.position.y * scale;
            if (!std::isfinite(screen_x) || !std::isfinite(screen_y) ||
                source.u * 16 < INT16_MIN || source.u * 16 > INT16_MAX ||
                source.v * 16 < INT16_MIN || source.v * 16 > INT16_MAX)
                return std::unexpected(Error::invalid_argument);
            output[i] = {
                .x_q4 = static_cast<std::int16_t>(std::clamp(detail::round_wire_value(screen_x * 16), 0,
                                               static_cast<std::int32_t>(width_) * 16)),
                .y_q4 = static_cast<std::int16_t>(std::clamp(detail::round_wire_value(screen_y * 16), 0,
                                               static_cast<std::int32_t>(height_) * 16)),
                .u_q4 = static_cast<std::int16_t>(detail::round_wire_value(source.u * 16)),
                .v_q4 = static_cast<std::int16_t>(detail::round_wire_value(source.v * 16)),
                .light = static_cast<std::uint8_t>(std::clamp(detail::round_wire_value(source.light), 0, 255)),
                .depth_q8 = static_cast<std::uint16_t>(
                    std::clamp(detail::round_wire_value(source.position.z * 256), 1, 65535))};
        }
        return count;
    }

public:
    // Emits whole, already triangulated triples; callers must not fan these
    // vertices again. Use max_triangle_vertices for a fully clipped triangle.
    Result<std::size_t> project_triangle(
        const std::array<MeshVertex, 3>& triangle,
        std::span<game::Vertex> output,
        FrontFace front = FrontFace::clockwise) const noexcept {
        std::array<game::Vertex, max_polygon_vertices> projected{};
        auto polygon = project_polygon(triangle, projected);
        if (!polygon) return std::unexpected(polygon.error());
        const auto count = *polygon;
        std::size_t written = 0;
        for (std::size_t i = 1; i + 1 < count; ++i) {
            const auto& a = projected[0];
            const auto& b = projected[i];
            const auto& c = projected[i + 1];
            const auto cross = (static_cast<std::int64_t>(b.x_q4) - a.x_q4) *
                                   (static_cast<std::int64_t>(c.y_q4) - a.y_q4) -
                               (static_cast<std::int64_t>(b.y_q4) - a.y_q4) *
                                   (static_cast<std::int64_t>(c.x_q4) - a.x_q4);
            if (cross == 0 || (front == FrontFace::clockwise && cross < 0) ||
                (front == FrontFace::counter_clockwise && cross > 0))
                continue;
            if (written + 3 > output.size())
                return std::unexpected(Error::limit_exceeded);
            output[written++] = a;
            output[written++] = b;
            output[written++] = c;
        }
        return written;
    }

private:
    static bool same_position(const MeshVertex& a, const MeshVertex& b) noexcept {
        return a.position.x == b.position.x && a.position.y == b.position.y &&
               a.position.z == b.position.z;
    }
    Projector(std::uint16_t width, std::uint16_t height, float focal,
              float near_z, float far_z) noexcept
        : width_(width), height_(height), focal_(focal),
          horizontal_(width / (2.0f * focal)),
          vertical_(height / (2.0f * focal)),
          near_(near_z), far_(far_z) {}

    static bool valid(const MeshVertex& vertex) noexcept {
        const auto p = vertex.position;
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
               std::isfinite(vertex.u) && std::isfinite(vertex.v) &&
               std::isfinite(vertex.light);
    }

    float distance(Vec3 p, unsigned plane) const noexcept {
        switch (plane) {
        case 0: return p.z - near_;
        case 1: return far_ - p.z;
        case 2: return p.x + p.z * horizontal_;
        case 3: return p.z * horizontal_ - p.x;
        case 4: return p.y + p.z * vertical_;
        default: return p.z * vertical_ - p.y;
        }
    }

    static MeshVertex interpolate(const MeshVertex& a, const MeshVertex& b,
                                  float t) noexcept {
        if (t <= 0) return a;
        if (t >= 1) return b;
        return {{a.position.x + (b.position.x - a.position.x) * t,
                 a.position.y + (b.position.y - a.position.y) * t,
                 a.position.z + (b.position.z - a.position.z) * t},
                a.u + (b.u - a.u) * t,
                a.v + (b.v - a.v) * t,
                a.light + (b.light - a.light) * t};
    }

    std::uint16_t width_;
    std::uint16_t height_;
    float focal_;
    float horizontal_;
    float vertical_;
    float near_;
    float far_;
};

} // namespace pxa::game3d

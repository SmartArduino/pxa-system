#pragma once

#include "game.hpp"

#include <algorithm>
#include <cmath>

namespace pxa::game3d {

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

    Result<std::size_t> project_triangle(
        const std::array<MeshVertex, 3>& triangle,
        std::span<game::Vertex> output,
        FrontFace front = FrontFace::clockwise) const noexcept {
        std::array<MeshVertex, 12> input{};
        std::array<MeshVertex, 12> clipped{};
        std::copy(triangle.begin(), triangle.end(), input.begin());
        std::size_t count = 3;
        for (const auto& vertex : triangle) {
            if (!valid(vertex)) return std::unexpected(Error::invalid_argument);
        }
        for (unsigned plane = 0; plane < 6; ++plane) {
            if (!count) return std::size_t{0};
            std::size_t next = 0;
            auto previous = input[count - 1];
            float previous_distance = distance(previous.position, plane);
            for (std::size_t i = 0; i < count; ++i) {
                const auto current = input[i];
                const float current_distance = distance(current.position, plane);
                if ((previous_distance >= 0) != (current_distance >= 0)) {
                    if (next == clipped.size())
                        return std::unexpected(Error::limit_exceeded);
                    const float t = previous_distance /
                                    (previous_distance - current_distance);
                    clipped[next++] = interpolate(previous, current, t);
                }
                if (current_distance >= 0) {
                    if (next == clipped.size())
                        return std::unexpected(Error::limit_exceeded);
                    clipped[next++] = current;
                }
                previous = current;
                previous_distance = current_distance;
            }
            std::copy_n(clipped.begin(), next, input.begin());
            count = next;
        }
        if (count < 3) return std::size_t{0};
        std::array<game::Vertex, 12> projected{};
        for (std::size_t i = 0; i < count; ++i) {
            const auto& source = input[i];
            const float screen_x = width_ / 2.0f + focal_ * source.position.x / source.position.z;
            const float screen_y = height_ / 2.0f + focal_ * source.position.y / source.position.z;
            if (!std::isfinite(screen_x) || !std::isfinite(screen_y) ||
                source.u * 16 < INT16_MIN || source.u * 16 > INT16_MAX ||
                source.v * 16 < INT16_MIN || source.v * 16 > INT16_MAX)
                return std::unexpected(Error::invalid_argument);
            projected[i] = {
                .x_q4 = static_cast<std::int16_t>(std::clamp(std::lround(screen_x * 16), 0l,
                                               static_cast<long>(width_) * 16)),
                .y_q4 = static_cast<std::int16_t>(std::clamp(std::lround(screen_y * 16), 0l,
                                               static_cast<long>(height_) * 16)),
                .u_q4 = static_cast<std::int16_t>(std::lround(source.u * 16)),
                .v_q4 = static_cast<std::int16_t>(std::lround(source.v * 16)),
                .light = static_cast<std::uint8_t>(std::clamp(std::lround(source.light), 0l, 255l)),
                .depth_q8 = static_cast<std::uint16_t>(
                    std::clamp(std::lround(source.position.z * 256), 1l, 65535l))};
        }
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

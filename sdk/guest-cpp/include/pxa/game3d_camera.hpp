#pragma once
#include "game3d.hpp"
#include <array>

namespace pxa::game3d {
// World +Y is up; view +Y follows screen coordinates (down). Yaw zero looks
// along +Z, positive pitch looks down. Prepare once per camera change/frame.
struct CameraBasis {
    Vec3 eye;
    std::array<Vec3, 3> axes;
    static CameraBasis from_pose(Vec3 eye, float yaw, float pitch) noexcept {
        const float cy=std::cos(yaw), sy=std::sin(yaw);
        const float cp=std::cos(pitch), sp=std::sin(pitch);
        return {eye, {{{cy,-sy*sp,sy*cp}, {0,-cp,-sp}, {-sy,-cy*sp,cy*cp}}}};
    }
    Vec3 to_view(Vec3 world) const noexcept {
        // Subtract the origin before multiplying to retain precision when
        // games use large world coordinates.
        const float dx=world.x-eye.x, dy=world.y-eye.y, dz=world.z-eye.z;
        return {dx*axes[0].x+dy*axes[1].x+dz*axes[2].x,
                dx*axes[0].y+dy*axes[1].y+dz*axes[2].y,
                dx*axes[0].z+dy*axes[1].z+dz*axes[2].z};
    }
    Vec3 forward() const noexcept { return {axes[0].z,axes[1].z,axes[2].z}; }
    Vec3 right() const noexcept { return {axes[0].x,axes[1].x,axes[2].x}; }
};
} // namespace pxa::game3d

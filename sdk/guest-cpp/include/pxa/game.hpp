#pragma once

#include "core.hpp"
#include "assets.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <span>

namespace pxa::game {

struct Color565 {
    std::uint16_t value;
    static constexpr Color565 black() noexcept { return {0}; }
};

struct Sprite {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t source_x = 0;
    std::uint16_t source_y = 0;
    std::uint16_t source_width = 0;
    std::uint16_t source_height = 0;
};

struct AtlasBinding {
    std::uint8_t slot = 0;
};

enum class RenderCapability : std::uint32_t {
    flat_quad = 1,
    textured_quad = 2,
    additive_sprite = 4,
    sprite_batch = 8,
    triangle_batch = 16,
    affine_uv = 32,
    texture_slots_48 = 64,
    painter_polygon = 128,
    lit_palette_depth = 256,
    depth_cutout = 512,
    blend_75 = 1024,
    coverage_mask = 2048,
    sprite_palette_ramp = 4096,
    sprite_texel_alpha = 8192,
    painter_perspective = 16384,
    painter_depth = 32768,
};

constexpr std::uint32_t capability_bit(RenderCapability capability) noexcept {
    return static_cast<std::uint32_t>(capability);
}

struct SpriteOptions {
    bool transparent_index0 = true;
    bool solid_color = false;
    bool additive = false;
    bool palette_ramp = false;
    bool texel_alpha = false;
    Color565 color = Color565::black();
};

struct Vertex {
    std::int16_t x_q4 = 0;
    std::int16_t y_q4 = 0;
    std::int16_t u_q4 = 0;
    std::int16_t v_q4 = 0;
    std::uint8_t light = 255;
    std::uint16_t depth_q8 = 256;
};

inline std::uint16_t painter_depth_from_z(float z) noexcept {
    if (!(z > 0) || !std::isfinite(z)) return 0;
    if (z >= 65535.0f / 256.0f) return UINT16_MAX;
    const auto depth = static_cast<std::uint16_t>(z * 256.0f + .5f);
    return depth ? depth : 1;
}

struct PolygonOptions {
    bool affine_uv = false;
    bool painter = false;
    bool transparent_index0 = false;
    bool lit_palette = false;
    bool blend_75 = false;
    bool coverage_mask = false;
    bool coverage_resolve = false;
};

enum class Scratch : std::uint8_t { depth16, none, coverage_2bit };

struct FrameTick {
    std::uint64_t timestamp_us = 0;
    std::uint32_t simulation_delta_us = 0;
    std::uint32_t frame_delta_us = 0;
    std::uint8_t simulation_steps = 0;
};

// Declare App::loop_options only when the game needs a different simulation
// budget. The runtime folds these constants into its existing FixedStepper.
struct LoopOptions {
    std::uint32_t simulation_step_us = 16000;
    std::uint16_t frame_period_ms = 16;
    std::uint8_t maximum_updates = 4;
};

struct RenderInfo {
    std::uint32_t capabilities = 0;
    std::uint32_t max_draw_bytes = 0;
    std::uint16_t max_texture_dimension = 0;
    std::uint8_t max_textures = 0;
    std::uint16_t display_width = 0;
    std::uint16_t display_height = 0;
    std::uint16_t render_width = 0;
    std::uint16_t render_height = 0;
    std::uint8_t render_scale = 0;
    std::uint8_t supported_scale_mask = 0;
};

struct Telemetry {
    std::uint64_t submitted_frames = 0;
    std::uint64_t draw_list_bytes = 0;
    std::uint64_t covered_pixels = 0;
    std::uint64_t host_raster_us = 0;
    std::uint64_t queue_wait_us = 0;
    std::uint64_t present_us = 0;
    std::uint64_t dropped_frames = 0;
    std::uint32_t clear_commands = 0;
    std::uint32_t flat_quad_commands = 0;
    std::uint32_t textured_quad_commands = 0;
    std::uint32_t sprite_commands = 0;
    std::uint32_t rejected_lists = 0;
    std::uint32_t last_draw_list_bytes = 0;
    std::uint32_t last_covered_pixels = 0;
    std::uint32_t last_host_raster_us = 0;
    std::uint64_t rendered_frames = 0;
    std::uint64_t visible_frames = 0;
};

template<std::size_t Capacity>
class DrawBuffer {
    static_assert(Capacity >= 32 && Capacity <= 49152);
public:
    static constexpr std::size_t capacity() noexcept { return Capacity; }
    std::span<std::byte> bytes() noexcept { return bytes_; }
private:
    std::array<std::byte, Capacity> bytes_{};
};

class Renderer;

class Frame {
public:
    Frame(Renderer& renderer, std::span<std::byte> bytes,
          std::uint64_t id) noexcept;
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    Frame& clear(Color565 color) noexcept {
        auto record = append(1, 8);
        if (!record.empty()) wire::put16(record.data() + 4, color.value);
        return *this;
    }

    Frame& quad(const std::array<std::int16_t, 8>& xy_q4,
                Color565 color) noexcept {
        if (!supports(capability_bit(RenderCapability::flat_quad))) return *this;
        auto record = append(2, 24);
        if (record.empty()) return *this;
        wire::put16(record.data() + 4, color.value);
        for (std::size_t i = 0; i < xy_q4.size(); ++i)
            wire::put16(record.data() + 8 + 2 * i,
                        static_cast<std::uint16_t>(xy_q4[i]));
        required_ |= capability_bit(RenderCapability::flat_quad);
        return *this;
    }

    Frame& sprites(AtlasBinding binding, std::span<const Sprite> items,
                   SpriteOptions options = {}) noexcept {
        constexpr auto batch_capability =
            capability_bit(RenderCapability::sprite_batch);
        const auto flags = static_cast<std::uint8_t>(
            (options.transparent_index0 ? 1 : 0) |
            (options.solid_color ? 2 : 0) |
            (options.additive ? 4 : 0) |
            (options.palette_ramp ? 8 : 0) |
            (options.texel_alpha ? 16 : 0));
        if (items.empty()) return *this;
        if (items.size() > (UINT16_MAX - 12) / 16 ||
            (options.texel_alpha && options.additive) ||
            ((!options.solid_color && !options.palette_ramp) &&
             options.color.value != 0)) {
            error_ = Error::invalid_argument;
            return *this;
        }
        if (!valid_texture_slot(binding) || !supports(batch_capability))
            return *this;
        if ((options.additive &&
             !supports(capability_bit(RenderCapability::additive_sprite))) ||
            (options.palette_ramp &&
             !supports(capability_bit(RenderCapability::sprite_palette_ramp))) ||
            (options.texel_alpha &&
             !supports(capability_bit(RenderCapability::sprite_texel_alpha))))
            return *this;
        const auto size = static_cast<std::uint16_t>(12 + 16 * items.size());
        auto record = append(5, size);
        if (record.empty()) return *this;
        record[1] = std::byte(flags);
        record[4] = std::byte(binding.slot);
        wire::put16(record.data() + 6, options.color.value);
        wire::put16(record.data() + 8,
                    static_cast<std::uint16_t>(items.size()));
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& sprite = items[i];
            if (!sprite.width || !sprite.height || !sprite.source_width ||
                !sprite.source_height) {
                error_ = Error::invalid_argument;
                return *this;
            }
            auto* out = record.data() + 12 + i * 16;
            wire::put16(out, static_cast<std::uint16_t>(sprite.x));
            wire::put16(out + 2, static_cast<std::uint16_t>(sprite.y));
            wire::put16(out + 4, sprite.width);
            wire::put16(out + 6, sprite.height);
            wire::put16(out + 8, sprite.source_x);
            wire::put16(out + 10, sprite.source_y);
            wire::put16(out + 12, sprite.source_width);
            wire::put16(out + 14, sprite.source_height);
        }
        required_ |= batch_capability;
        if (options.additive)
            required_ |= capability_bit(RenderCapability::additive_sprite);
        if (options.palette_ramp)
            required_ |= capability_bit(RenderCapability::sprite_palette_ramp);
        if (options.texel_alpha)
            required_ |= capability_bit(RenderCapability::sprite_texel_alpha);
        return *this;
    }

    Frame& textured_quad(AtlasBinding binding,
                         const std::array<Vertex, 4>& vertices,
                         PolygonOptions options = {}) noexcept;
    // Opaque RGB565 quad in the shared depth buffer; no texture binding needed.
    // Scanline mode requires the painter-depth capability.
    Frame& solid_depth_quad(const std::array<Vertex, 4>& vertices,
                            Color565 color, bool scanline = false) noexcept;
    // Convex perimeter, 3..10 vertices, in the shared scanline depth buffer.
    // Texture lighting uses palette rows. Clipped triangles repeat their final
    // corner in the existing quad ABI; larger polygons fan exactly once.
    // Zero-area fans caused by wire quantization are omitted.
    Frame& textured_depth_polygon(AtlasBinding binding,
                                  std::span<const Vertex> vertices,
                                  PolygonOptions options = {}) noexcept;
    Frame& solid_depth_polygon(std::span<const Vertex> vertices,
                               Color565 color) noexcept;
    Frame& triangles(AtlasBinding binding, std::span<const Vertex> vertices,
                     PolygonOptions options = {}) noexcept;
    Frame& solid_triangles(std::span<const Vertex> vertices,
                           Color565 color) noexcept;

    Result<void> submit() noexcept;
    std::size_t bytes_used() const noexcept { return used_; }

private:
    bool reserve_depth_polygon(std::span<const Vertex> vertices) noexcept;
    bool supports(std::uint32_t capability) noexcept;
    bool valid_texture_slot(AtlasBinding binding) noexcept;
    Frame& append_triangles(std::span<const Vertex> vertices,
                            AtlasBinding binding, std::uint8_t flags,
                            Color565 color, std::uint32_t capabilities) noexcept;
    std::span<std::byte> append(std::uint8_t type,
                                std::uint16_t size) noexcept {
        if (error_) return {};
        if (commands_ >= 1024 || size > bytes_.size() - used_) {
            error_ = Error::limit_exceeded;
            return {};
        }
        auto record = bytes_.subspan(used_, size);
        // Encoders overwrite every payload byte; reserved bytes live in the header.
        for (std::size_t i = 0; i < size && i < 12; ++i)
            record[i] = std::byte{};
        record[0] = std::byte(type);
        wire::put16(record.data() + 2, size);
        used_ += size;
        ++commands_;
        return record;
    }

    Renderer& renderer_;
    std::span<std::byte> bytes_;
    std::uint64_t id_;
    std::size_t used_ = 32;
    std::uint32_t required_ = 0;
    std::uint32_t commands_ = 0;
    std::optional<Error> error_;
};

class Renderer {
public:
    Renderer(Transport& transport, std::uint64_t handle,
             std::uint32_t capabilities) noexcept
        : transport_(transport), handle_(transport, handle),
          info_{.capabilities = capabilities} {}
    Renderer(Transport& transport, std::uint64_t handle,
             RenderInfo info) noexcept
        : transport_(transport), handle_(transport, handle), info_(info) {}

    template<std::size_t Capacity>
    Frame frame(DrawBuffer<Capacity>& buffer) noexcept {
        return Frame(*this, buffer.bytes(), next_frame_);
    }

    std::uint32_t capabilities() const noexcept { return info_.capabilities; }
    bool supports(RenderCapability capability) const noexcept {
        return (info_.capabilities & capability_bit(capability)) != 0;
    }
    const RenderInfo& info() const noexcept { return info_; }
    Transport& transport() noexcept { return transport_; }
    std::uint64_t handle() const noexcept { return handle_.handle(); }

    // The renderer retains the bound asset. The caller can release its load
    // handle immediately, avoiding a live handle for every atlas texture.
    Result<void> bind_asset(const Asset& asset, AtlasBinding binding = {}) noexcept {
        const auto kind = asset.descriptor().kind;
        if (!asset.handle() || (kind != AssetKind::texture && kind != AssetKind::palette) ||
            (kind == AssetKind::palette && binding.slot != 0) || binding.slot >= 48)
            return std::unexpected(Error::invalid_argument);
        if (kind == AssetKind::texture &&
            ((info_.max_textures && binding.slot >= info_.max_textures) ||
             (binding.slot >= 16 && !supports(RenderCapability::texture_slots_48))))
            return std::unexpected(Error::unsupported);
        std::array<std::byte, 16> bytes{};
        wire::put16(bytes.data(), 1);
        bytes[4] = std::byte(static_cast<std::uint8_t>(kind));
        bytes[5] = std::byte(binding.slot);
        wire::put64(bytes.data() + 8, asset.handle());
        auto result = transport_.io(handle(), 0x103, bytes);
        if (!result) return std::unexpected(result.error());
        return *result == bytes.size() ? Result<void>{}
            : Result<void>{std::unexpected(Error::protocol_error)};
    }

    Result<void> bind_assets(std::span<const std::uint64_t> textures,
                             std::uint64_t palette = 0) noexcept {
        if (textures.size() > 48 || (!palette && textures.empty()))
            return std::unexpected(Error::invalid_argument);
        if ((info_.max_textures && textures.size() > info_.max_textures) ||
            (textures.size() > 16 &&
             !supports(RenderCapability::texture_slots_48)))
            return std::unexpected(Error::unsupported);
        std::array<std::byte, 4 + 49 * 12> bytes{};
        const auto count = textures.size() + (palette ? 1 : 0);
        wire::put16(bytes.data(), static_cast<std::uint16_t>(count));
        for (std::size_t i = 0; i < textures.size(); ++i) {
            bytes[4 + i * 12] = std::byte{1};
            bytes[5 + i * 12] = std::byte(i);
            wire::put64(bytes.data() + 8 + i * 12, textures[i]);
        }
        if (palette) {
            auto* item = bytes.data() + 4 + textures.size() * 12;
            item[0] = std::byte{2};
            wire::put64(item + 4, palette);
        }
        auto result = transport_.io(handle_.handle(), 0x103,
                                    {bytes.data(), 4 + count * 12});
        if (!result) return std::unexpected(result.error());
        if (*result != 4 + count * 12)
            return std::unexpected(Error::protocol_error);
        return {};
    }

    Result<Telemetry> telemetry() noexcept {
        std::array<std::byte, 104> bytes{};
        auto result = transport_.io(handle_.handle(), 0x102, bytes);
        if (!result) return std::unexpected(result.error());
        if (*result != bytes.size())
            return std::unexpected(Error::protocol_error);
        Telemetry out;
        auto u64 = [&](std::size_t at) { return wire::get64(bytes.data() + at); };
        auto u32 = [&](std::size_t at) { return wire::get32(bytes.data() + at); };
        out.submitted_frames = u64(0);
        out.draw_list_bytes = u64(8);
        out.covered_pixels = u64(16);
        out.host_raster_us = u64(24);
        out.queue_wait_us = u64(32);
        out.present_us = u64(40);
        out.dropped_frames = u64(48);
        out.clear_commands = u32(56);
        out.flat_quad_commands = u32(60);
        out.textured_quad_commands = u32(64);
        out.sprite_commands = u32(68);
        out.rejected_lists = u32(72);
        out.last_draw_list_bytes = u32(76);
        out.last_covered_pixels = u32(80);
        out.last_host_raster_us = u32(84);
        out.rendered_frames = u64(88);
        out.visible_frames = u64(96);
        return out;
    }

private:
    friend class Frame;
    struct ContextTag {};
    Transport& transport_;
    Resource<ContextTag> handle_;
    RenderInfo info_;
    std::uint64_t next_frame_ = 1;
};

inline Frame::Frame(Renderer& renderer, std::span<std::byte> bytes,
                     std::uint64_t id) noexcept
    : renderer_(renderer), bytes_(bytes), id_(id) {
    if (bytes.size() < 32 || id == 0) error_ = Error::invalid_argument;
}

inline bool Frame::supports(std::uint32_t capability) noexcept {
    if ((renderer_.capabilities() & capability) == capability) return true;
    error_ = Error::unsupported;
    return false;
}

inline Result<void> Frame::submit() noexcept {
    if (error_) return std::unexpected(*error_);
    if (!commands_ || used_ > UINT32_MAX ||
        (required_ & ~renderer_.capabilities()) != 0)
        return std::unexpected(Error::invalid_argument);
    if (renderer_.info().max_draw_bytes &&
        used_ > renderer_.info().max_draw_bytes)
        return std::unexpected(Error::limit_exceeded);
    auto* out = bytes_.data();
    wire::put32(out, 0x4c525850);
    wire::put16(out + 4, 1);
    wire::put16(out + 6, 7);
    wire::put32(out + 8, static_cast<std::uint32_t>(used_));
    wire::put32(out + 12, required_);
    wire::put32(out + 16, commands_);
    wire::put64(out + 20, id_);
    wire::put32(out + 28, 0);
    auto result = renderer_.transport().io(renderer_.handle(), 0x101,
                                            bytes_.first(used_));
    if (!result) return std::unexpected(result.error());
    if (*result != used_) return std::unexpected(Error::protocol_error);
    ++renderer_.next_frame_;
    return {};
}

} // namespace pxa::game

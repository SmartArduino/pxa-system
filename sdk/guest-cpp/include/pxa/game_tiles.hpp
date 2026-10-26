#pragma once
#include "game_compute_wire_generated.hpp"
#include <optional>
#include <cstring>

namespace pxa::game {
inline constexpr std::uint64_t priority_tile_batches_feature = 64;
using PriorityTile = wire_compute::pxa_game_render_tile_wire_t;
struct PriorityTileTarget {
    std::uint32_t color_offset, priority_offset, pixel_count, pitch;
};

// Caller-owned storage, synchronous borrowed execution, no heap allocation.
class PriorityTileBatch {
public:
    PriorityTileBatch(std::span<std::byte> storage, PriorityTileTarget target) noexcept
        : storage_(storage), resource_begin_(storage.size()), target_(target) {
        if (storage.size() < PXA_GAME_RENDER_TILES_HEADER_BYTES ||
            storage.size() > PXA_GAME_RENDER_TILES_MAX_PACKET_BYTES ||
            !target.pixel_count || !target.pitch || target.pitch > target.pixel_count)
            error_ = Error::invalid_argument;
    }
    std::size_t remaining() const noexcept { return error_ ? 0 : resource_begin_ - command_end_; }
    std::uint32_t count() const noexcept { return count_; }
    // Initialize reserved sources before execution; offsets are packet-relative.
    Result<std::uint32_t> reserve_source(std::size_t bytes) noexcept {
        if (error_) return std::unexpected(*error_);
        if (!bytes) return fail(Error::invalid_argument);
        if (bytes > resource_begin_) return fail(Error::limit_exceeded);
        auto at = resource_begin_ - bytes;
        auto padding = reinterpret_cast<std::uintptr_t>(storage_.data() + at) & 3u;
        if (at < padding || at - padding < command_end_) return fail(Error::limit_exceeded);
        resource_begin_ = at - padding;
        return static_cast<std::uint32_t>(resource_begin_);
    }
    Result<std::uint32_t> copy_source(std::span<const std::byte> source) noexcept {
        auto at = reserve_source(source.size());
        if (at) std::memmove(storage_.data() + *at, source.data(), source.size());
        return at;
    }
    Result<void> append(const PriorityTile& t) noexcept {
        if (error_) return std::unexpected(*error_);
        if (!t.width || !t.height || t.source_x + t.width > 8 || t.source_y + t.height > 8 ||
            t.reserved0 || t.reserved1 || (t.flags & ~3u) ||
            (t.palette_entries != 4 && t.palette_entries != 16 && t.palette_entries != 256) ||
            !source_range(t.tile_offset, 64) || !source_range(t.palette_offset, t.palette_entries * 2u))
            return fail(Error::invalid_argument);
        if (count_ == PXA_GAME_RENDER_TILES_MAX_TILES || remaining() < PXA_GAME_RENDER_TILES_COMMAND_BYTES)
            return fail(Error::limit_exceeded);
        wire_compute::pxa_game_render_tile_encode(storage_.data() + command_end_,
            PXA_GAME_RENDER_TILES_COMMAND_BYTES, &t);
        command_end_ += PXA_GAME_RENDER_TILES_COMMAND_BYTES; ++count_;
        return {};
    }
private:
    friend class RasterContext;
    std::unexpected<Error> fail(Error error) noexcept { error_ = error; return std::unexpected(error); }
    bool source_range(std::size_t at, std::size_t bytes) const noexcept {
        return at >= resource_begin_ && at <= storage_.size() && bytes <= storage_.size() - at;
    }
    std::span<std::byte> storage_;
    std::size_t command_end_ = PXA_GAME_RENDER_TILES_HEADER_BYTES, resource_begin_;
    PriorityTileTarget target_;
    std::uint32_t count_ = 0;
    std::optional<Error> error_;
};
} // namespace pxa::game

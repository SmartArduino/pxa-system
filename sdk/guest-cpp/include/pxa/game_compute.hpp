#pragma once
#include "game_compute_wire_generated.hpp"
#include "game_tiles.hpp"
#include <optional>
#include <cstring>
namespace pxa::game {
using IndexedBlit = wire_compute::pxa_game_render_blit_wire_t;
struct ComputeTelemetry {
    std::uint64_t batches, rejected, commands, pixels, validation_us, execute_us;
    std::uint32_t last_validation_us, last_execute_us, max_commands;
};
class RasterContext {
public:
    RasterContext(Transport& transport, std::uint64_t handle, std::uint32_t max_commands) noexcept
        : transport_(transport), handle_(transport, handle), max_commands_(max_commands) {}
    std::uint64_t handle() const noexcept { return handle_.handle(); }
    std::uint32_t max_commands() const noexcept { return max_commands_; }
    Result<void> execute(PriorityTileBatch& batch) noexcept {
        if (batch.error_) return std::unexpected(*batch.error_);
        if (!batch.count_) return {};
        if (batch.count_ > max_commands_) return std::unexpected(Error::limit_exceeded);
        const wire_compute::pxa_game_render_tiles_header_wire_t header{
            PXA_GAME_RENDER_TILES_MAGIC, 1, 0, batch.count_, static_cast<std::uint32_t>(batch.command_end_),
            batch.target_.color_offset, batch.target_.priority_offset,
            batch.target_.pixel_count, batch.target_.pitch};
        wire_compute::pxa_game_render_tiles_header_encode(batch.storage_.data(),
            PXA_GAME_RENDER_TILES_HEADER_BYTES, &header);
        auto result = transport_.io(handle(), 0x10a, batch.storage_);
        if (!result) return std::unexpected(result.error());
        if (*result != batch.storage_.size()) return std::unexpected(Error::protocol_error);
        return {};
    }
    Result<void> blit(const IndexedBlit& frame) noexcept {
        std::array<std::byte, PXA_GAME_RENDER_BLIT_BYTES> bytes{};
        wire_compute::pxa_game_render_blit_encode(bytes.data(), bytes.size(), &frame);
        auto result = transport_.io(handle(), 0x108, bytes);
        if (!result) return std::unexpected(result.error());
        if (*result != bytes.size()) return std::unexpected(Error::protocol_error);
        return {};
    }
    // Exact nearest-neighbour sampling, without the legacy 320x240 grid.
    // Memory remains borrowed only until the call returns, including workers.
    Result<void> blit_rect(const IndexedBlit& frame) noexcept {
        std::array<std::byte, PXA_GAME_RENDER_BLIT_BYTES> bytes{};
        wire_compute::pxa_game_render_blit_encode(bytes.data(), bytes.size(), &frame);
        auto result = transport_.io(handle(), 0x109, bytes);
        if (!result) return std::unexpected(result.error());
        if (*result != bytes.size()) return std::unexpected(Error::protocol_error);
        return {};
    }
    Result<ComputeTelemetry> telemetry() noexcept {
        std::array<std::byte, 64> bytes{};
        auto result = transport_.io(handle(), 0x10c, bytes);
        if (!result) return std::unexpected(result.error());
        if (*result != 64 || wire::get32(bytes.data() + 60))
            return std::unexpected(Error::protocol_error);
        const auto* p = bytes.data();
        return ComputeTelemetry{wire::get64(p), wire::get64(p + 8), wire::get64(p + 16),
            wire::get64(p + 24), wire::get64(p + 32), wire::get64(p + 40),
            wire::get32(p + 48), wire::get32(p + 52), wire::get32(p + 56)};
    }
private:
    struct Tag {};
    Transport& transport_;
    Resource<Tag> handle_;
    std::uint32_t max_commands_;
};
} // namespace pxa::game

#pragma once
#include "game_spans_wire_generated.hpp"
#include <optional>

namespace pxa::game {
using IndexedSpan = wire_spans::pxa_game_render_span_wire_t;
using IndexedSpanHeader = wire_spans::pxa_game_render_spans_header_wire_t;

class SpanBatch {
public:
    SpanBatch(std::span<std::byte> storage, IndexedSpanHeader header) noexcept
        : storage_(storage) {
        header.magic = PXA_GAME_RENDER_SPANS_MAGIC;
        header.major = 1; header.minor = 0; header.count = 0; header.reserved = 0;
        if (!wire_spans::pxa_game_render_spans_header_encode(
                storage.data(), storage.size(), &header))
            error_ = Error::invalid_argument;
    }
    Result<void> append(IndexedSpan span) noexcept {
        if (error_) return std::unexpected(*error_);
        if (!span.count) { error_ = Error::invalid_argument; return std::unexpected(*error_); }
        const auto position = 72u + std::size_t(count_) * 24u;
        if (position > storage_.size() || storage_.size() - position < 24) {
            error_ = Error::limit_exceeded; return std::unexpected(*error_);
        }
        wire_spans::pxa_game_render_span_encode(storage_.data() + position, 24, &span);
        ++count_;
        return {};
    }
    std::uint32_t count() const noexcept { return count_; }
private:
    friend class RasterContext;
    std::span<std::byte> storage_;
    std::uint32_t count_ = 0;
    std::optional<Error> error_;
};

using ProjectedRow = wire_spans::pxa_game_render_row_wire_t;
using SpanProjection = wire_spans::pxa_game_render_projection_wire_t;

class ProjectedBatch {
public:
    ProjectedBatch(std::span<std::byte> storage, IndexedSpanHeader header,
        const SpanProjection& projection) noexcept : storage_(storage) {
        if (storage.size() < PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES) {
            error_ = Error::invalid_argument; return;
        }
        header.magic = PXA_GAME_RENDER_PROJECTED_MAGIC;
        header.major = 1; header.minor = 0; header.count = 0; header.reserved = 0;
        wire_spans::pxa_game_render_spans_header_encode(storage.data(), storage.size(), &header);
        wire_spans::pxa_game_render_projection_encode(storage.data() + PXA_GAME_RENDER_SPANS_HEADER_BYTES,
            PXA_GAME_RENDER_PROJECTION_BYTES, &projection);
    }
    Result<void> append(ProjectedRow row) noexcept {
        if (error_) return std::unexpected(*error_);
        if (!row.count || row.reserved) {
            error_ = Error::invalid_argument; return std::unexpected(*error_);
        }
        const auto position = PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES +
            std::size_t(count_) * PXA_GAME_RENDER_ROW_BYTES;
        if (position > storage_.size() || storage_.size() - position < PXA_GAME_RENDER_ROW_BYTES) {
            error_ = Error::limit_exceeded; return std::unexpected(*error_);
        }
        wire_spans::pxa_game_render_row_encode(storage_.data() + position, PXA_GAME_RENDER_ROW_BYTES, &row);
        ++count_;
        return {};
    }
    std::uint32_t count() const noexcept { return count_; }
private:
    friend class RasterContext;
    std::span<std::byte> storage_;
    std::uint32_t count_ = 0;
    std::optional<Error> error_;
};

struct SpanTelemetry {
    std::uint64_t batches, rejected, spans, pixels, validation_us, execute_us;
    std::uint32_t last_validation_us, last_execute_us, max_spans;
};

class RasterContext {
public:
    RasterContext(Transport& transport, std::uint64_t handle, std::uint32_t max_spans) noexcept
        : transport_(transport), handle_(transport, handle), max_spans_(max_spans) {}
    std::uint64_t handle() const noexcept { return handle_.handle(); }
    std::uint32_t max_spans() const noexcept { return max_spans_; }
    Result<void> execute(SpanBatch& batch) noexcept {
        if (batch.error_) return std::unexpected(*batch.error_);
        if (!batch.count_) return {};
        if (batch.count_ > max_spans_) return std::unexpected(Error::limit_exceeded);
        wire::put32(batch.storage_.data() + 8, batch.count_);
        const auto size = 72u + std::size_t(batch.count_) * 24u;
        auto result = transport_.io(handle(), 0x104, batch.storage_.first(size));
        if (!result) return std::unexpected(result.error());
        if (*result != size) return std::unexpected(Error::protocol_error);
        return {};
    }
    Result<void> execute(ProjectedBatch& batch) noexcept {
        if (batch.error_) return std::unexpected(*batch.error_);
        if (!batch.count_) return {};
        if (batch.count_ > max_spans_) return std::unexpected(Error::limit_exceeded);
        wire::put32(batch.storage_.data() + 8, batch.count_);
        const auto size = PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES +
            std::size_t(batch.count_) * PXA_GAME_RENDER_ROW_BYTES;
        auto result = transport_.io(handle(), 0x106, batch.storage_.first(size));
        if (!result) return std::unexpected(result.error());
        if (*result != size) return std::unexpected(Error::protocol_error);
        return {};
    }
    Result<SpanTelemetry> telemetry() noexcept {
        std::array<std::byte, 64> bytes{};
        auto result = transport_.io(handle(), 0x105, bytes);
        if (!result) return std::unexpected(result.error());
        if (*result != 64 || wire::get32(bytes.data() + 60))
            return std::unexpected(Error::protocol_error);
        const auto* p = bytes.data();
        return SpanTelemetry{wire::get64(p), wire::get64(p + 8), wire::get64(p + 16),
            wire::get64(p + 24), wire::get64(p + 32), wire::get64(p + 40),
            wire::get32(p + 48), wire::get32(p + 52), wire::get32(p + 56)};
    }
private:
    struct Tag {};
    Transport& transport_;
    Resource<Tag> handle_;
    std::uint32_t max_spans_;
};
} // namespace pxa::game

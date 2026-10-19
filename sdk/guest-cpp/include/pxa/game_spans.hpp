#pragma once
#include "game_spans_wire_generated.hpp"
#include <optional>
#include <cstring>

namespace pxa::game {
using IndexedSpan = wire_spans::pxa_game_render_span_wire_t;
using IndexedBlit = wire_spans::pxa_game_render_blit_wire_t;
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

class FrameBatch {
public:
    explicit FrameBatch(std::span<std::byte> storage) noexcept : storage_(storage), resource_begin_(storage.size()) {
        if (storage.size() < PXA_GAME_RENDER_FRAME_HEADER_BYTES ||
            storage.size() > PXA_GAME_RENDER_FRAME_MAX_PACKET_BYTES)
            error_ = Error::invalid_argument;
    }
    std::size_t remaining() const noexcept { return error_ ? 0 : resource_begin_ - command_end_; }
    // Initialize all reserved bytes before submitting the batch.
    Result<std::uint32_t> reserve_source(std::size_t bytes) noexcept {
        if (error_) return std::unexpected(*error_);
        if (!bytes) { error_ = Error::invalid_argument; return std::unexpected(*error_); }
        if (bytes > resource_begin_) { error_ = Error::limit_exceeded; return std::unexpected(*error_); }
        auto position = resource_begin_ - bytes;
        const auto padding = reinterpret_cast<std::uintptr_t>(storage_.data() + position) & 3u;
        if (position < padding || position - padding < command_end_) {
            error_ = Error::limit_exceeded; return std::unexpected(*error_);
        }
        position -= padding;
        resource_begin_ = position;
        return static_cast<std::uint32_t>(position);
    }
    Result<std::uint32_t> copy_source(std::span<const std::byte> source) noexcept {
        auto offset = reserve_source(source.size());
        if (offset) std::memmove(storage_.data() + *offset, source.data(), source.size());
        return offset;
    }
    Result<void> append(IndexedSpanHeader material, const SpanProjection& projection,
        std::span<const ProjectedRow> rows) noexcept {
        if (error_) return std::unexpected(*error_);
        if (rows.empty() || rows.size() > 4096) {
            error_ = Error::invalid_argument; return std::unexpected(*error_);
        }
        const auto size = PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES + rows.size() * PXA_GAME_RENDER_ROW_BYTES;
        if (count_ == PXA_GAME_RENDER_FRAME_MAX_MATERIALS || size > remaining()) {
            error_ = Error::limit_exceeded; return std::unexpected(*error_);
        }
        for (const auto& row : rows) if (!row.count || row.reserved) {
            error_ = Error::invalid_argument; return std::unexpected(*error_);
        }
        material.magic = PXA_GAME_RENDER_FRAME_MATERIAL_MAGIC;
        material.major = 1; material.minor = 0; material.count = rows.size(); material.reserved = 0;
        auto* record = storage_.data() + command_end_;
        wire_spans::pxa_game_render_spans_header_encode(record, PXA_GAME_RENDER_SPANS_HEADER_BYTES, &material);
        wire_spans::pxa_game_render_projection_encode(record + PXA_GAME_RENDER_SPANS_HEADER_BYTES,
            PXA_GAME_RENDER_PROJECTION_BYTES, &projection);
        for (std::size_t i = 0; i < rows.size(); ++i)
            wire_spans::pxa_game_render_row_encode(record + PXA_GAME_RENDER_PROJECTED_PREFIX_BYTES +
                i * PXA_GAME_RENDER_ROW_BYTES, PXA_GAME_RENDER_ROW_BYTES, &rows[i]);
        command_end_ += size; ++count_;
        if (rows.size() > max_rows_) max_rows_ = rows.size();
        return {};
    }
    std::uint32_t count() const noexcept { return count_; }
private:
    friend class RasterContext;
    std::span<std::byte> storage_;
    std::size_t command_end_ = PXA_GAME_RENDER_FRAME_HEADER_BYTES, resource_begin_;
    std::uint32_t count_ = 0, max_rows_ = 0;
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
    Result<void> execute(FrameBatch& batch) noexcept {
        if (batch.error_) return std::unexpected(*batch.error_);
        if (!batch.count_) return {};
        if (batch.max_rows_ > max_spans_) return std::unexpected(Error::limit_exceeded);
        const wire_spans::pxa_game_render_frame_header_wire_t header{
            PXA_GAME_RENDER_FRAME_MAGIC, 1, 0, batch.count_, static_cast<std::uint32_t>(batch.command_end_)};
        wire_spans::pxa_game_render_frame_header_encode(batch.storage_.data(),
            PXA_GAME_RENDER_FRAME_HEADER_BYTES, &header);
        auto result = transport_.io(handle(), 0x107, batch.storage_);
        if (!result) return std::unexpected(result.error());
        if (*result != batch.storage_.size()) return std::unexpected(Error::protocol_error);
        return {};
    }
    Result<void> blit(const IndexedBlit& frame) noexcept {
        std::array<std::byte, PXA_GAME_RENDER_BLIT_BYTES> bytes{};
        wire_spans::pxa_game_render_blit_encode(bytes.data(), bytes.size(), &frame);
        auto result = transport_.io(handle(), 0x108, bytes);
        if (!result) return std::unexpected(result.error());
        if (*result != bytes.size()) return std::unexpected(Error::protocol_error);
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

#pragma once

#include "core.hpp"

// Optional callback API for additional UI surfaces and Canvas stream I/O.
// Request IDs are u32 fields in one-way commands, distinct from Core tokens.
namespace pxa::ui {

enum class SurfaceRole : std::uint8_t { application = 1, dialog, overlay, external };
struct SurfaceReady {
    std::uint32_t request, surface;
    std::int32_t status;
    std::span<const std::byte> environment_records;
};
struct CanvasStreamReady {
    std::uint32_t request;
    std::uint64_t handle;
    std::int32_t status;
};

inline Result<SurfaceReady> decode_surface_ready(const Event& event) noexcept {
    if (event.service != 3 || event.opcode != 0x8004 || event.token || event.payload.size() < 12)
        return std::unexpected(Error::protocol_error);
    const auto* p = event.payload.data();
    SurfaceReady result{wire::get32(p), wire::get32(p + 4),
        static_cast<std::int32_t>(wire::get32(p + 8)), event.payload.subspan(12)};
    if (!result.request || (result.status == 0) != (result.surface != 0) ||
        (result.status && !result.environment_records.empty()))
        return std::unexpected(Error::protocol_error);
    return result;
}
inline Result<CanvasStreamReady> decode_canvas_stream_ready(const Event& event) noexcept {
    if (event.service != 3 || event.opcode != 0x8005 || event.token || event.payload.size() != 16)
        return std::unexpected(Error::protocol_error);
    const auto* p = event.payload.data();
    CanvasStreamReady result{wire::get32(p), wire::get64(p + 4),
        static_cast<std::int32_t>(wire::get32(p + 12))};
    if (!result.request || (result.status == 0) != (result.handle != 0))
        return std::unexpected(Error::protocol_error);
    return result;
}

struct CanvasStreamTag {};
class CanvasStream {
public:
    // Adopt exactly once after decoding a successful CanvasStreamReady event.
    // Closing or recreating its node invalidates the stream's node instance.
    CanvasStream(Transport& transport, std::uint64_t handle) noexcept
        : transport_(&transport), resource_(transport, handle) {}
    explicit operator bool() const noexcept { return bool(resource_); }
    std::uint64_t handle() const noexcept { return resource_.handle(); }
    Result<void> close() noexcept { return resource_.close(); }
    Result<std::uint32_t> write(std::span<const std::byte> bytes) noexcept {
        if (!resource_) return std::unexpected(Error::bad_state);
        if (bytes.empty()) return std::unexpected(Error::invalid_argument);
        auto result = transport_->io(handle(), 2, {const_cast<std::byte*>(bytes.data()), bytes.size()});
        if (result && *result > bytes.size()) return std::unexpected(Error::protocol_error);
        return result;
    }
private:
    Transport* transport_;
    Resource<CanvasStreamTag> resource_;
};

class TransportService {
public:
    explicit TransportService(Transport& transport) noexcept : transport_(transport) {}
    Result<void> open_surface(std::uint32_t request, SurfaceRole role) noexcept {
        auto value = static_cast<std::uint8_t>(role);
        if (!request || value < 1 || value > 4) return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 8> payload{};
        wire::put32(payload.data(), request); payload[4] = std::byte(value);
        return transport_.send(3, 5, 0, payload);
    }
    Result<void> close_surface(std::uint32_t surface) noexcept {
        if (surface <= 1) return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 4> payload;
        wire::put32(payload.data(), surface);
        return transport_.send(3, 6, 0, payload);
    }
    Result<void> open_canvas_stream(std::uint32_t request, std::uint32_t surface,
        std::uint32_t node) noexcept {
        if (!request || !surface || !node) return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 12> payload;
        wire::put32(payload.data(), request); wire::put32(payload.data() + 4, surface);
        wire::put32(payload.data() + 8, node);
        return transport_.send(3, 10, 0, payload);
    }
    // Payload-only equivalent of the C UI wire command builder. This also
    // supports streaming Canvas BEGIN/WRITE/PRESENT on any logical surface.
    Result<void> command(std::uint16_t opcode, std::span<const std::byte> payload) noexcept {
        if (opcode < 1 || opcode > 10) return std::unexpected(Error::invalid_argument);
        return transport_.send(3, opcode, 0, payload);
    }
private:
    Transport& transport_;
};

} // namespace pxa::ui

#pragma once

#include "task.hpp"
#include "pixels.hpp"

#include <cstdint>
#include <span>

namespace pxa {

struct SurfaceOptions {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t buffers = 2;
    bool direct_scanout = false;
    std::uint32_t max_buffer_bytes = 1024 * 1024;
};

struct SurfaceLayer {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::int16_t z = 0;
    bool visible = true;
};

struct SurfaceRect { std::uint16_t x, y, width, height; };
enum class SurfaceFormat : std::uint16_t { rgb565 = 1, argb8888_premultiplied = 2 };
struct SurfaceStreamOptions {
    std::uint16_t width = 0, height = 0;
    SurfaceFormat format = SurfaceFormat::rgb565;
    std::uint8_t buffers = 2;
    bool direct_scanout = false;
};

struct SurfaceState {
    std::uint64_t submitted_frames = 0;
    std::uint64_t presented_frames = 0;
    std::uint64_t dropped_frames = 0;
    std::uint64_t replaced_frames = 0;
    std::uint64_t released_frames = 0;
    std::uint32_t free_buffers = 0;
    std::uint32_t flags = 0;
};

struct SurfaceRelease {
    std::uint64_t handle = 0;
    std::uint64_t frame_id = 0;
    std::uint8_t buffer_index = 0;
};

Result<SurfaceRelease> decode_surface_release(const Event& event) noexcept;

namespace detail { struct SurfaceControl; }

class SurfaceFrame {
public:
    SurfaceFrame(const SurfaceFrame&) = delete;
    SurfaceFrame& operator=(const SurfaceFrame&) = delete;
    SurfaceFrame(SurfaceFrame&& other) noexcept;
    SurfaceFrame& operator=(SurfaceFrame&& other) noexcept;
    ~SurfaceFrame();

    std::span<std::byte> pixels() const & noexcept;
    std::span<std::byte> pixels() const && = delete("Keep the SurfaceFrame alive while borrowing its pixels");
    // The view has the frame lease's lifetime; discard it after present/reset.
    Result<Rgb565Pixels> rgb565() const & noexcept;
    Result<Rgb565Pixels> rgb565() const && = delete("Keep the SurfaceFrame alive while borrowing its pixels");
    std::uint32_t stride_bytes() const noexcept;
    std::uint8_t buffer_index() const noexcept { return index_; }
    Result<void> present(std::uint64_t frame_id) noexcept;

private:
    friend class Surface;
    SurfaceFrame(detail::SurfaceControl* control, std::uint8_t index) noexcept;
    void reset() noexcept;
    detail::SurfaceControl* control_ = nullptr;
    std::uint8_t index_ = 0;
};

class Surface {
public:
    Surface() = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    Surface(Surface&& other) noexcept;
    Surface& operator=(Surface&& other) noexcept;
    ~Surface();

    explicit operator bool() const noexcept;
    std::uint64_t handle() const noexcept;
    std::uint16_t width() const noexcept;
    std::uint16_t height() const noexcept;
    std::uint32_t stride_bytes() const noexcept;
    std::uint32_t frame_bytes() const noexcept;
    std::uint8_t buffer_count() const noexcept;
    Result<SurfaceFrame> acquire() noexcept;
    Task<void> configure(SurfaceLayer layer);
    Task<SurfaceState> query_state();
    Task<void> opaque_ui_regions(std::span<const SurfaceRect> regions);
    // Explicit close preserves ownership on rejection and can be retried.
    Result<void> close() noexcept;
    void reset() noexcept;

private:
    friend class SurfaceService;
    friend class SurfaceStream;
    static Task<void> configure_request(Transport&, RequestTable&,
        std::uint64_t handle, SurfaceLayer);
    static Task<SurfaceState> query_request(Transport&, RequestTable&,
        std::uint64_t handle);
    static Task<void> regions_request(Transport&, RequestTable&, std::uint64_t,
        std::span<const SurfaceRect>);
    explicit Surface(detail::SurfaceControl* control) noexcept
        : control_(control) {}
    detail::SurfaceControl* control_ = nullptr;
};

struct SurfaceStreamTag {};

// Host-owned frame buffers for copied RGB565/alpha surfaces. The caller owns
// upload storage; no pinned-memory declaration or Guest allocation is needed.
class SurfaceStream {
public:
    SurfaceStream(SurfaceStream&&) noexcept = default;
    SurfaceStream& operator=(SurfaceStream&&) noexcept = default;
    explicit operator bool() const noexcept { return bool(resource_); }
    std::uint64_t handle() const noexcept { return resource_.handle(); }
    std::uint32_t stride_bytes() const noexcept { return stride_; }
    std::uint32_t frame_bytes() const noexcept { return frame_bytes_; }
    Result<void> close() noexcept { return resource_.close(); }
    Result<void> write(std::span<const std::byte> pixels) noexcept;
    Result<void> present(std::uint64_t frame_id, std::span<const SurfaceRect> damage = {}) noexcept;
    Task<void> configure(SurfaceLayer layer) {
        if (!resource_) return Task<void>::failed(Error::bad_state);
        if (!layer.width || !layer.height) return Task<void>::failed(Error::invalid_argument);
        return Surface::configure_request(*transport_, *requests_, handle(), layer);
    }
    Task<SurfaceState> query_state() {
        if (!resource_) return Task<SurfaceState>::failed(Error::bad_state);
        return Surface::query_request(*transport_, *requests_, handle());
    }
    Task<void> opaque_ui_regions(std::span<const SurfaceRect> regions) {
        if (!resource_) return Task<void>::failed(Error::bad_state);
        return Surface::regions_request(*transport_, *requests_, handle(), regions);
    }
private:
    friend class SurfaceService;
    SurfaceStream(Transport& transport, RequestTable& requests, std::uint64_t handle,
        std::uint32_t stride, std::uint32_t bytes) noexcept
        : transport_(&transport), requests_(&requests), resource_(transport, handle),
          stride_(stride), frame_bytes_(bytes) {}
    Transport* transport_;
    RequestTable* requests_;
    Resource<SurfaceStreamTag> resource_;
    std::uint32_t stride_, frame_bytes_;
};

class SurfaceService {
public:
    SurfaceService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<Surface> create_mapped(this SurfaceService self, SurfaceOptions options);
    Task<SurfaceStream> create_stream(this SurfaceService self, SurfaceStreamOptions options);

private:
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa

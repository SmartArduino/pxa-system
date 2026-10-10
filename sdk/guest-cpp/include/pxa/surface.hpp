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
    // Explicit close preserves ownership on rejection and can be retried.
    Result<void> close() noexcept;
    void reset() noexcept;

private:
    friend class SurfaceService;
    static Task<void> configure_request(Transport&, RequestTable&,
        std::uint64_t handle, SurfaceLayer);
    static Task<SurfaceState> query_request(Transport&, RequestTable&,
        std::uint64_t handle);
    explicit Surface(detail::SurfaceControl* control) noexcept
        : control_(control) {}
    detail::SurfaceControl* control_ = nullptr;
};

class SurfaceService {
public:
    SurfaceService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Task<Surface> create_mapped(this SurfaceService self, SurfaceOptions options);

private:
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa

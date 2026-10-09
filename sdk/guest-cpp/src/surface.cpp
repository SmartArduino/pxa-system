#include "pxa/surface.hpp"

#include <array>
#include <cstdlib>
#include <limits>
#include <new>
#include <utility>

namespace pxa {
namespace detail {

struct SurfaceControl {
    Transport* transport;
    RequestTable* requests;
    std::uint64_t handle;
    std::byte* pixels;
    std::uint64_t last_frame_id;
    std::uint32_t stride_bytes;
    std::uint32_t frame_bytes;
    std::uint16_t width;
    std::uint16_t height;
    std::uint8_t buffer_count;
    std::uint8_t references;
    bool open;
};

static void close(SurfaceControl* control) noexcept {
    if (!control || !control->open) return;
    control->open = false;
    (void)control->transport->close(control->handle);
}

static void release(SurfaceControl* control) noexcept {
    if (control && --control->references == 0) {
        control->~SurfaceControl();
        std::free(control);
    }
}

} // namespace detail

Result<SurfaceRelease> decode_surface_release(const Event& event) noexcept {
    if (event.service != 16 || event.opcode != 0x8001 || event.token != 0 ||
        event.payload.size() != 20) return std::unexpected(Error::protocol_error);
    const auto bytes = event.payload;
    if (bytes[9] != std::byte{} || bytes[10] != std::byte{} ||
        bytes[11] != std::byte{})
        return std::unexpected(Error::protocol_error);
    SurfaceRelease released{
        .handle = wire::get64(bytes.data()),
        .frame_id = wire::get64(bytes.data() + 12),
        .buffer_index = std::to_integer<std::uint8_t>(bytes[8])};
    if (!released.handle || !released.frame_id)
        return std::unexpected(Error::protocol_error);
    return released;
}

SurfaceFrame::SurfaceFrame(detail::SurfaceControl* control,
                           std::uint8_t index) noexcept
    : control_(control), index_(index) {
    ++control_->references;
}

SurfaceFrame::SurfaceFrame(SurfaceFrame&& other) noexcept
    : control_(std::exchange(other.control_, nullptr)), index_(other.index_) {}

SurfaceFrame& SurfaceFrame::operator=(SurfaceFrame&& other) noexcept {
    if (this != &other) {
        reset();
        control_ = std::exchange(other.control_, nullptr);
        index_ = other.index_;
    }
    return *this;
}

SurfaceFrame::~SurfaceFrame() { reset(); }

void SurfaceFrame::reset() noexcept {
    if (!control_) return;
    detail::close(control_);
    detail::release(std::exchange(control_, nullptr));
}

std::span<std::byte> SurfaceFrame::pixels() const noexcept {
    if (!control_ || !control_->open) return {};
    return {control_->pixels + std::size_t(index_) * control_->frame_bytes,
            control_->frame_bytes};
}

std::uint32_t SurfaceFrame::stride_bytes() const noexcept {
    return control_ && control_->open ? control_->stride_bytes : 0;
}

Result<Rgb565Pixels> SurfaceFrame::rgb565() const noexcept {
    if (!control_ || !control_->open) return std::unexpected(Error::bad_state);
    return Rgb565Pixels::from_bytes(pixels(), control_->width, control_->height,
                                    control_->stride_bytes);
}

Result<void> SurfaceFrame::present(std::uint64_t frame_id) noexcept {
    if (!control_ || !control_->open)
        return std::unexpected(Error::bad_state);
    if (!frame_id || frame_id <= control_->last_frame_id)
        return std::unexpected(Error::invalid_argument);
    std::array<std::byte, 16> record{};
    record[0] = std::byte(index_);
    wire::put64(record.data() + 8, frame_id);
    auto result = control_->transport->io(control_->handle, 0x102, record);
    if (!result) return std::unexpected(result.error());
    if (*result != record.size()) return std::unexpected(Error::protocol_error);
    control_->last_frame_id = frame_id;
    detail::release(std::exchange(control_, nullptr));
    return {};
}

Surface::Surface(Surface&& other) noexcept
    : control_(std::exchange(other.control_, nullptr)) {}

Surface& Surface::operator=(Surface&& other) noexcept {
    if (this != &other) {
        reset();
        control_ = std::exchange(other.control_, nullptr);
    }
    return *this;
}

Surface::~Surface() { reset(); }

Surface::operator bool() const noexcept {
    return control_ && control_->open;
}

std::uint64_t Surface::handle() const noexcept {
    return control_ && control_->open ? control_->handle : 0;
}

std::uint16_t Surface::width() const noexcept {
    return control_ ? control_->width : 0;
}

std::uint16_t Surface::height() const noexcept {
    return control_ ? control_->height : 0;
}

std::uint32_t Surface::stride_bytes() const noexcept {
    return control_ ? control_->stride_bytes : 0;
}

std::uint32_t Surface::frame_bytes() const noexcept {
    return control_ ? control_->frame_bytes : 0;
}

std::uint8_t Surface::buffer_count() const noexcept {
    return control_ ? control_->buffer_count : 0;
}

void Surface::reset() noexcept {
    if (!control_) return;
    detail::close(control_);
    detail::release(std::exchange(control_, nullptr));
}

Result<SurfaceFrame> Surface::acquire() noexcept {
    if (!*this) return std::unexpected(Error::bad_state);
    std::array<std::byte, 4> record{};
    auto result = control_->transport->io(control_->handle, 0x101, record);
    if (!result) return std::unexpected(result.error());
    if (*result != record.size() || record[1] != std::byte{} ||
        record[2] != std::byte{} || record[3] != std::byte{} ||
        std::to_integer<std::uint8_t>(record[0]) >= control_->buffer_count) {
        detail::close(control_);
        return std::unexpected(Error::protocol_error);
    }
    return SurfaceFrame(control_, std::to_integer<std::uint8_t>(record[0]));
}

Task<void> Surface::configure(SurfaceLayer layer) {
    if (!*this) co_return std::unexpected(Error::bad_state);
    if (!layer.width || !layer.height)
        co_return std::unexpected(Error::invalid_argument);
    auto* transport = control_->transport;
    auto* requests = control_->requests;
    std::array<std::byte, 24> payload{};
    wire::put64(payload.data(), control_->handle);
    wire::put32(payload.data() + 8,
                static_cast<std::uint32_t>(layer.x));
    wire::put32(payload.data() + 12,
                static_cast<std::uint32_t>(layer.y));
    wire::put16(payload.data() + 16, layer.width);
    wire::put16(payload.data() + 18, layer.height);
    wire::put16(payload.data() + 20,
                static_cast<std::uint16_t>(layer.z));
    payload[22] = std::byte(layer.visible ? 1 : 0);
    auto event = co_await Response(*transport, *requests, 16, 2, payload);
    if (!event) co_return std::unexpected(event.error());
    if (event->payload.size() != 4)
        co_return std::unexpected(Error::protocol_error);
    auto status = static_cast<std::int32_t>(wire::get32(event->payload.data()));
    if (status != 0)
        co_return std::unexpected(static_cast<Error>(status));
    co_return Result<void>{};
}

Task<SurfaceState> Surface::query_state() {
    if (!*this) co_return std::unexpected(Error::bad_state);
    auto* transport = control_->transport;
    auto* requests = control_->requests;
    std::array<std::byte, 8> payload{};
    wire::put64(payload.data(), control_->handle);
    auto event = co_await Response(*transport, *requests, 16, 4, payload);
    if (!event) co_return std::unexpected(event.error());
    if (event->payload.size() < 4)
        co_return std::unexpected(Error::protocol_error);
    auto status = static_cast<std::int32_t>(wire::get32(event->payload.data()));
    if (status != 0) {
        if (event->payload.size() != 4)
            co_return std::unexpected(Error::protocol_error);
        co_return std::unexpected(static_cast<Error>(status));
    }
    if (event->payload.size() != 52)
        co_return std::unexpected(Error::protocol_error);
    const auto* bytes = event->payload.data();
    co_return SurfaceState{
        .submitted_frames = wire::get64(bytes + 4),
        .presented_frames = wire::get64(bytes + 12),
        .dropped_frames = wire::get64(bytes + 20),
        .replaced_frames = wire::get64(bytes + 28),
        .released_frames = wire::get64(bytes + 36),
        .free_buffers = wire::get32(bytes + 44),
        .flags = wire::get32(bytes + 48)};
}

Task<Surface> SurfaceService::create_mapped(this SurfaceService self,
                                           SurfaceOptions options) {
    auto& [transport_, requests_] = self;
    if (!options.width || !options.height || options.buffers < 2 ||
        options.buffers > 3 || options.max_buffer_bytes == 0)
        co_return std::unexpected(Error::invalid_argument);
    std::array<std::byte, 8> payload{};
    wire::put16(payload.data(), options.width);
    wire::put16(payload.data() + 2, options.height);
    wire::put16(payload.data() + 4, 1);
    payload[6] = std::byte(options.buffers);
    payload[7] = std::byte(4 | (options.direct_scanout ? 2 : 0));
    auto event = co_await Response(transport_, requests_, 16, 1,
                                   payload, true);
    if (!event) co_return std::unexpected(event.error());
    if (event->payload.size() < 4)
        co_return std::unexpected(Error::protocol_error);
    auto status = static_cast<std::int32_t>(wire::get32(event->payload.data()));
    if (status != 0) {
        if (event->payload.size() != 4)
            co_return std::unexpected(Error::protocol_error);
        co_return std::unexpected(static_cast<Error>(status));
    }
    if (event->payload.size() < 12)
        co_return std::unexpected(Error::protocol_error);
    const auto handle = wire::get64(event->payload.data() + 4);
    if (!handle) co_return std::unexpected(Error::protocol_error);
    struct PendingTag {};
    Resource<PendingTag> pending(transport_, handle);
    if (event->payload.size() != 24 || event->payload[21] != std::byte{} ||
        event->payload[22] != std::byte{} ||
        event->payload[23] != std::byte{})
        co_return std::unexpected(Error::protocol_error);
    const auto stride = wire::get32(event->payload.data() + 12);
    const auto frame_bytes = wire::get32(event->payload.data() + 16);
    const auto buffers = std::to_integer<std::uint8_t>(event->payload[20]);
    const std::uint64_t total = std::uint64_t(frame_bytes) * buffers;
    if (buffers != options.buffers || stride < std::uint32_t(options.width) * 2 ||
        frame_bytes < std::uint64_t(stride) * options.height ||
        total > UINT32_MAX)
        co_return std::unexpected(Error::protocol_error);
    if (total > options.max_buffer_bytes ||
        total > std::numeric_limits<std::size_t>::max() -
                    sizeof(detail::SurfaceControl) - 127)
        co_return std::unexpected(Error::resource_limit);
    const auto allocation_size =
        (sizeof(detail::SurfaceControl) + 63 + std::size_t(total) + 63) &
        ~std::size_t{63};
    void* memory = std::aligned_alloc(64, allocation_size);
    if (!memory) co_return std::unexpected(Error::resource_limit);
    auto* control = new (memory) detail::SurfaceControl{
        &transport_, &requests_, handle, nullptr, 0, stride, frame_bytes,
        options.width, options.height, buffers, 1, true};
    const auto pixel_address =
        (reinterpret_cast<std::uintptr_t>(memory) +
         sizeof(detail::SurfaceControl) + 63) & ~std::uintptr_t{63};
    control->pixels = reinterpret_cast<std::byte*>(pixel_address);
    auto registered = transport_.io(handle, 0x100,
                                     {control->pixels, std::size_t(total)});
    if (!registered || *registered != total) {
        detail::release(control);
        co_return std::unexpected(registered
            ? Error::protocol_error : registered.error());
    }
    pending.release();
    co_return Surface(control);
}

} // namespace pxa

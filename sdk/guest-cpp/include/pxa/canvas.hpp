#pragma once

#include "core.hpp"
#include <array>
#include <algorithm>
#include <optional>
#include <string_view>

namespace pxa::ui {

struct CanvasRegion {
    std::int32_t x, y, width, height;
    constexpr bool operator==(const CanvasRegion&) const noexcept = default;
};

// Commands occupy their final WRITE packet. No intermediate display list or
// heap allocation is needed; Capacity is the application's explicit budget.
template<std::size_t Capacity = 480>
class CanvasCommands {
    static_assert(Capacity >= 4 && Capacity <= wire::max_control_bytes - 32);
    friend class CanvasRef;
public:
    void reset() noexcept { used_ = 32; error_.reset(); clip_depth_ = 0; }
    std::size_t bytes_used() const noexcept { return used_ - 32; }
    CanvasCommands& rect(std::int32_t x, std::int32_t y, std::uint32_t width,
                         std::uint32_t height, std::uint32_t rgba,
                         std::uint16_t radius = 0) noexcept {
        if (!width || !height) return fail(Error::invalid_argument);
        auto out = record(1, 28);
        if (!out.empty()) {
            coordinates(out.data(), x, y, width, height);
            wire::put32(out.data() + 16, rgba);
            wire::put16(out.data() + 20, radius);
        }
        return *this;
    }
    CanvasCommands& line(std::int32_t x1, std::int32_t y1, std::int32_t x2,
                         std::int32_t y2, std::uint32_t rgba,
                         std::uint16_t width = 1) noexcept {
        if (!width) return fail(Error::invalid_argument);
        auto out = record(3, 22);
        if (!out.empty()) {
            coordinates(out.data(), x1, y1, static_cast<std::uint32_t>(x2),
                                             static_cast<std::uint32_t>(y2));
            wire::put32(out.data() + 16, rgba);
            wire::put16(out.data() + 20, width);
        }
        return *this;
    }
    CanvasCommands& text(std::int32_t x, std::int32_t y, std::uint32_t width,
                         std::uint32_t rgba, std::string_view value,
                         std::uint8_t font = 1, std::uint8_t align = 0) noexcept {
        if (!width || font > 3 || align > 2 || value.size() > UINT16_MAX - 18)
            return fail(Error::invalid_argument);
        auto out = record(5, static_cast<std::uint16_t>(18 + value.size()));
        if (!out.empty()) {
            wire::put32(out.data(), static_cast<std::uint32_t>(x));
            wire::put32(out.data() + 4, static_cast<std::uint32_t>(y));
            wire::put32(out.data() + 8, width);
            wire::put32(out.data() + 12, rgba);
            out[16] = std::byte(font); out[17] = std::byte(align);
            std::copy(value.begin(), value.end(), reinterpret_cast<char*>(out.data() + 18));
        }
        return *this;
    }
    // Optional Host feature SIZED_TEXT (bit 11). One clipped line; size is native Surface pixels.
    CanvasCommands& text_sized(CanvasRegion box, std::uint32_t rgba,
        std::string_view value, std::uint16_t size_pixels, std::uint8_t align=0) noexcept {
        if (box.width <= 0 || box.height <= 0 || size_pixels < 8 || size_pixels > 128 ||
            align > 2 || value.size() > UINT16_MAX-24)
            return fail(Error::invalid_argument);
        auto out=record(12, static_cast<std::uint16_t>(24+value.size()));
        if (!out.empty()) {
            coordinates(out.data(),box.x,box.y,box.width,box.height);
            wire::put32(out.data()+16,rgba);wire::put16(out.data()+20,size_pixels);
            out[22]=std::byte(align);
            std::copy(value.begin(),value.end(),reinterpret_cast<char*>(out.data()+24));
        }
        return *this;
    }
    CanvasCommands& clip(CanvasRegion region) noexcept {
        if (region.width <= 0 || region.height <= 0)
            return fail(Error::invalid_argument);
        auto out = record(7, 16);
        if (!out.empty()) {
            coordinates(out.data(), region.x, region.y, region.width, region.height);
            ++clip_depth_;
        }
        return *this;
    }
    CanvasCommands& pop_clip() noexcept {
        if (!clip_depth_) return fail(Error::bad_state);
        record(8, 0);
        if (!error_) --clip_depth_;
        return *this;
    }
private:
    CanvasCommands& fail(Error error) noexcept { if (!error_) error_ = error; return *this; }
    static void coordinates(std::byte* out, std::int32_t x, std::int32_t y,
                            std::uint32_t width, std::uint32_t height) noexcept {
        wire::put32(out, static_cast<std::uint32_t>(x));
        wire::put32(out + 4, static_cast<std::uint32_t>(y));
        wire::put32(out + 8, width); wire::put32(out + 12, height);
    }
    std::span<std::byte> record(std::uint8_t type, std::uint16_t payload) noexcept {
        if (error_) return {};
        const auto size = std::size_t(payload) + 4;
        if (size > packet_.size() - used_) { fail(Error::limit_exceeded); return {}; }
        auto* out = packet_.data() + used_;
        std::fill_n(out, size, std::byte{});
        out[0] = std::byte(type); wire::put16(out + 2, payload);
        used_ += size;
        return {out + 4, std::size_t(payload)};
    }
    std::array<std::byte, 32 + Capacity> packet_{};
    std::size_t used_ = 32;
    std::optional<Error> error_;
    std::uint16_t clip_depth_ = 0;
};

class CanvasRef {
    template<class, std::size_t, std::size_t, std::size_t, std::size_t> friend class Page;
public:
    CanvasRef() = default;
    CanvasRef(const CanvasRef&) = delete;
    CanvasRef& operator=(const CanvasRef&) = delete;
    bool mounted() const noexcept { return owner_ != nullptr; }
    template<std::size_t Capacity>
    Result<void> present(CanvasCommands<Capacity>& commands,
                         std::span<const CanvasRegion> dirty = {}) noexcept {
        if (!mounted() || !transport_) return std::unexpected(Error::bad_state);
        if (commands.error_) return std::unexpected(*commands.error_);
        if (commands.clip_depth_ || dirty.size() > 4)
            return std::unexpected(Error::invalid_argument);
        for (const auto& region : dirty)
            if (region.width <= 0 || region.height <= 0)
                return std::unexpected(Error::invalid_argument);
        if (frame_ == UINT32_MAX) return std::unexpected(Error::limit_exceeded);
        // BEGIN consumes this generation even when WRITE/PRESENT fails.
        const auto frame = ++frame_;
        std::array<std::byte, 16> begin{};
        wire::put32(begin.data(), 1); wire::put32(begin.data() + 4, node_);
        wire::put32(begin.data() + 8, frame);
        auto result = transport_->send(3, 7, 0, begin);
        if (!result) return result;
        if (commands.bytes_used()) {
            auto* prefix = commands.packet_.data() + wire::header_bytes;
            wire::put32(prefix, 1); wire::put32(prefix + 4, node_);
            wire::put32(prefix + 8, frame);
            result = transport_->send_prebuilt(3, 8, 0,
                std::span{commands.packet_}.first(commands.used_));
            if (!result) return result;
        }
        std::array<std::byte, 13 + 4 * 16> present{};
        wire::put32(present.data(), 1); wire::put32(present.data() + 4, node_);
        wire::put32(present.data() + 8, frame);
        present[12] = std::byte(dirty.size());
        for (std::size_t i = 0; i < dirty.size(); ++i) {
            const auto& r = dirty[i];
            CanvasCommands<>::coordinates(present.data() + 13 + i * 16,
                                           r.x, r.y, r.width, r.height);
        }
        return transport_->send(3, 9, 0, std::span{present}.first(13 + dirty.size() * 16));
    }
private:
    Transport* transport_ = nullptr;
    void* owner_ = nullptr;
    std::uint32_t node_ = 0, frame_ = 0;
};

} // namespace pxa::ui

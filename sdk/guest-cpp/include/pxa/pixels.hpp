#pragma once

#include "core.hpp"

namespace pxa {

// Borrowed RGB565 little-endian pixels. Works with unaligned buffers and padded
// rows without assuming native uint16_t alignment, byte order or aliasing.
class Rgb565Row {
public:
    class Pixel {
    public:
        explicit constexpr Pixel(std::byte* pixel) noexcept : pixel_(pixel) {}
        constexpr operator std::uint16_t() const noexcept { return wire::get16(pixel_); }
        constexpr Pixel& operator=(std::uint16_t color) noexcept {
            wire::put16(pixel_, color);
            return *this;
        }
        constexpr Pixel& operator=(const Pixel& other) noexcept {
            return *this = static_cast<std::uint16_t>(other);
        }
    private:
        std::byte* pixel_;
    };
    // Like span::operator[], indexing requires x < size().
    constexpr Pixel operator[](std::size_t x) const noexcept { return Pixel(bytes_.data() + x * 2); }
    constexpr std::size_t size() const noexcept { return bytes_.size() / 2; }
    constexpr void fill(std::uint16_t color) const noexcept {
        for (std::size_t x = 0; x < size(); ++x) (*this)[x] = color;
    }
private:
    friend class Rgb565Pixels;
    explicit constexpr Rgb565Row(std::span<std::byte> bytes) noexcept : bytes_(bytes) {}
    std::span<std::byte> bytes_;
};

class Rgb565Pixels {
public:
    Rgb565Pixels() = default;
    static constexpr Result<Rgb565Pixels> from_bytes(std::span<std::byte> bytes,
        std::uint16_t width, std::uint16_t height, std::uint32_t stride) noexcept {
        if (!width || !height || stride < std::uint32_t(width) * 2 ||
            std::uint64_t(stride) * height > bytes.size())
            return std::unexpected(Error::invalid_argument);
        return Rgb565Pixels(bytes, width, height, stride);
    }
    constexpr std::uint16_t width() const noexcept { return width_; }
    constexpr std::uint16_t height() const noexcept { return height_; }
    constexpr Result<Rgb565Row> row(std::size_t y) const noexcept {
        if (y >= height_) return std::unexpected(Error::invalid_argument);
        return Rgb565Row(bytes_.subspan(y * stride_, std::size_t(width_) * 2));
    }
    constexpr void fill(std::uint16_t color) const noexcept {
        for (std::size_t y = 0; y < height_; ++y) row(y)->fill(color);
    }
private:
    constexpr Rgb565Pixels(std::span<std::byte> bytes, std::uint16_t width,
        std::uint16_t height, std::uint32_t stride) noexcept
        : bytes_(bytes), stride_(stride), width_(width), height_(height) {}
    std::span<std::byte> bytes_;
    std::uint32_t stride_ = 0;
    std::uint16_t width_ = 0, height_ = 0;
};

} // namespace pxa

#pragma once

#include "core.hpp"

#include <bit>
#include <concepts>
#include <limits>
#include <type_traits>

// Optional application data codec. This does not encode PXA service packets
// or serialize native object layouts. Integer/IEEE scalar fields are little endian.
namespace pxa::binary {

static_assert(std::endian::native == std::endian::little ||
              std::endian::native == std::endian::big);

template<class T>
concept Scalar = std::same_as<T, bool> ||
    (std::integral<T> && (sizeof(T) == 1 || sizeof(T) == 2 ||
                         sizeof(T) == 4 || sizeof(T) == 8)) ||
    ((std::same_as<T, float> || std::same_as<T, double>) &&
     std::numeric_limits<T>::is_iec559 && (sizeof(T) == 4 || sizeof(T) == 8));

template<Scalar T>
constexpr Result<void> write(std::span<std::byte> output, T value,
                              std::size_t offset = 0) noexcept {
    if (offset > output.size() || sizeof(T) > output.size() - offset)
        return std::unexpected(Error::resource_limit);
    if constexpr (std::same_as<T, bool>) {
        output[offset] = value ? std::byte{1} : std::byte{0};
    } else {
        const auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            const auto at = std::endian::native == std::endian::little
                ? i : sizeof(T) - 1 - i;
            output[offset + i] = bytes[at];
        }
    }
    return {};
}

template<Scalar T>
constexpr Result<T> read(std::span<const std::byte> input,
                          std::size_t offset = 0) noexcept {
    if (offset > input.size() || sizeof(T) > input.size() - offset)
        return std::unexpected(Error::protocol_error);
    if constexpr (std::same_as<T, bool>) {
        if (input[offset] != std::byte{0} && input[offset] != std::byte{1})
            return std::unexpected(Error::protocol_error);
        return input[offset] == std::byte{1};
    } else {
        std::array<std::byte, sizeof(T)> bytes{};
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            const auto at = std::endian::native == std::endian::little
                ? i : sizeof(T) - 1 - i;
            bytes[at] = input[offset + i];
        }
        return std::bit_cast<T>(bytes);
    }
}

template<Scalar T>
constexpr std::array<std::byte, sizeof(T)> encode(T value) noexcept {
    std::array<std::byte, sizeof(T)> output{};
    (void)write(output, value);
    return output;
}

template<Scalar T>
constexpr Result<T> decode(std::span<const std::byte> input) noexcept {
    if (input.size() != sizeof(T))
        return std::unexpected(Error::protocol_error);
    return read<T>(input);
}

class Reader {
public:
    explicit constexpr Reader(std::span<const std::byte> input) noexcept
        : input_(input) {}

    template<Scalar T> constexpr Result<T> read() noexcept {
        auto value = binary::read<T>(input_, position_);
        if (value) position_ += sizeof(T);
        return value;
    }
    constexpr Result<std::span<const std::byte>> bytes(std::size_t size) noexcept {
        if (size > remaining()) return std::unexpected(Error::protocol_error);
        auto value = input_.subspan(position_, size);
        position_ += size;
        return value;
    }
    constexpr std::size_t position() const noexcept { return position_; }
    constexpr std::size_t remaining() const noexcept { return input_.size() - position_; }
    constexpr Result<void> finish() const noexcept {
        if (remaining()) return std::unexpected(Error::protocol_error);
        return {};
    }
private:
    std::span<const std::byte> input_;
    std::size_t position_ = 0;
};

class Writer {
public:
    explicit constexpr Writer(std::span<std::byte> output) noexcept : output_(output) {}
    template<Scalar T> constexpr Result<void> write(T value) noexcept {
        auto result = binary::write(output_, value, position_);
        if (result) position_ += sizeof(T);
        return result;
    }
    constexpr Result<void> bytes(std::span<const std::byte> input) noexcept {
        if (input.size() > remaining()) return std::unexpected(Error::resource_limit);
        for (std::size_t i = 0; i < input.size(); ++i) output_[position_ + i] = input[i];
        position_ += input.size();
        return {};
    }
    constexpr std::size_t size() const noexcept { return position_; }
    constexpr std::size_t remaining() const noexcept { return output_.size() - position_; }
    constexpr std::span<const std::byte> written() const noexcept { return output_.first(position_); }
private:
    std::span<std::byte> output_;
    std::size_t position_ = 0;
};

} // namespace pxa::binary

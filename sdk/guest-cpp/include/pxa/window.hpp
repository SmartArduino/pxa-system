#pragma once

#include "task.hpp"

#include <array>
#include <string_view>

namespace pxa {

struct Insets {
    std::uint32_t left = 0;
    std::uint32_t top = 0;
    std::uint32_t right = 0;
    std::uint32_t bottom = 0;
};

struct WindowMetrics {
    std::uint64_t revision = 0;
    std::uint32_t logical_width = 0;
    std::uint32_t logical_height = 0;
    std::uint32_t pixel_width = 0;
    std::uint32_t pixel_height = 0;
    std::uint32_t density_numerator = 0;
    std::uint32_t density_denominator = 0;
    Insets safe_insets;
    Insets system_bar_insets;
    std::uint8_t orientation = 0;
    bool focused = false;
};

enum class BackAction : std::uint8_t { close = 0, stay = 1 };

inline Result<WindowMetrics> decode_window_metrics(
    std::span<const std::byte> bytes) noexcept {
    if (bytes.size() != 98) return std::unexpected(Error::protocol_error);
    constexpr std::array<std::uint16_t, 8> offsets{0, 12, 24, 36,
                                                    48, 68, 88, 93};
    constexpr std::array<std::uint16_t, 8> lengths{8, 8, 8, 8,
                                                    16, 16, 1, 1};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        if (wire::get16(bytes.data() + offsets[i]) != i + 1 ||
            wire::get16(bytes.data() + offsets[i] + 2) != lengths[i])
            return std::unexpected(Error::protocol_error);
    }
    WindowMetrics result;
    result.revision = wire::get64(bytes.data() + 4);
    result.logical_width = wire::get32(bytes.data() + 16);
    result.logical_height = wire::get32(bytes.data() + 20);
    result.pixel_width = wire::get32(bytes.data() + 28);
    result.pixel_height = wire::get32(bytes.data() + 32);
    result.density_numerator = wire::get32(bytes.data() + 40);
    result.density_denominator = wire::get32(bytes.data() + 44);
    auto insets = [&](Insets& target, std::size_t at) {
        target.left = wire::get32(bytes.data() + at);
        target.top = wire::get32(bytes.data() + at + 4);
        target.right = wire::get32(bytes.data() + at + 8);
        target.bottom = wire::get32(bytes.data() + at + 12);
    };
    insets(result.safe_insets, 52);
    insets(result.system_bar_insets, 72);
    result.orientation = std::to_integer<std::uint8_t>(bytes[92]);
    auto focused = std::to_integer<std::uint8_t>(bytes[97]);
    if (!result.revision || !result.logical_width || !result.logical_height ||
        !result.pixel_width || !result.pixel_height ||
        !result.density_numerator || !result.density_denominator ||
        result.orientation > 2 || focused > 1)
        return std::unexpected(Error::protocol_error);
    result.focused = focused != 0;
    return result;
}

class WindowService {
public:
    WindowService(Transport& transport, RequestTable& requests) noexcept
        : transport_(transport), requests_(requests) {}

    Result<void> toast(std::string_view message,
                       std::uint16_t duration_ms = 2000) noexcept {
        if (message.empty() || message.size() > 240 ||
            duration_ms < 500 || duration_ms > 5000 ||
            message.find('\0') != std::string_view::npos)
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 242> payload{};
        wire::put16(payload.data(), duration_ms);
        for (std::size_t i = 0; i < message.size(); ++i)
            payload[2 + i] = std::byte(message[i]);
        return transport_.send(2, 3, 0,
                               {payload.data(), message.size() + 2});
    }

    Result<void> fullscreen() noexcept {
        std::array<std::byte, 15> payload{};
        for (std::size_t i = 0; i < 3; ++i) {
            wire::put16(payload.data() + i * 5,
                        static_cast<std::uint16_t>(i + 1));
            wire::put16(payload.data() + i * 5 + 2, 1);
            payload[i * 5 + 4] = std::byte(i == 0 ? 1 : 2);
        }
        return transport_.send(2, 1, 0, payload);
    }

    Task<WindowMetrics> snapshot(this WindowService self) {
        auto& [transport_, requests_] = self;
        auto response = co_await Response(transport_, requests_, 2, 2, {});
        if (!response) co_return std::unexpected(response.error());
        if (response->payload.size() < 4)
            co_return std::unexpected(Error::protocol_error);
        auto status = static_cast<std::int32_t>(
            wire::get32(response->payload.data()));
        if (status != 0) {
            if (response->payload.size() != 4)
                co_return std::unexpected(Error::protocol_error);
            co_return std::unexpected(static_cast<Error>(status));
        }
        co_return decode_window_metrics(response->payload.subspan(4));
    }

private:
    Transport& transport_;
    RequestTable& requests_;
};

} // namespace pxa

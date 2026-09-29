#pragma once

#include "core.hpp"

#include <array>
#include <string_view>

namespace pxa {

enum class LogLevel : std::uint8_t {
    trace = 0, debug = 1, info = 2, warning = 3, error = 4
};

class LogService {
public:
    explicit LogService(Transport& transport) noexcept
        : transport_(transport) {}

    Result<void> write(LogLevel level, std::string_view message) noexcept {
        if (message.empty() || message.size() > 256 ||
            message.find('\0') != std::string_view::npos)
            return std::unexpected(Error::invalid_argument);
        std::array<std::byte, 257> payload{};
        payload[0] = std::byte(static_cast<std::uint8_t>(level));
        for (std::size_t i = 0; i < message.size(); ++i)
            payload[1 + i] = std::byte(message[i]);
        return transport_.send(19, 1, 0,
                               {payload.data(), message.size() + 1});
    }
private:
    Transport& transport_;
};

} // namespace pxa

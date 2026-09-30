#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>

inline std::string_view work_completion_key(
    std::uint32_t id, std::array<char, 32>& storage) noexcept {
    constexpr std::string_view prefix = "sync.completed.";
    for (std::size_t i = 0; i < prefix.size(); ++i)
        storage[i] = prefix[i];
    const auto result = std::to_chars(storage.data() + prefix.size(),
                                      storage.data() + storage.size(), id);
    return result.ec == std::errc{}
        ? std::string_view{storage.data(),
                           static_cast<std::size_t>(result.ptr - storage.data())}
        : std::string_view{};
}

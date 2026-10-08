#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>

inline std::string_view work_completion_key(
    std::uint32_t action, std::array<char, 32>& storage) noexcept {
    // Host Work IDs may be reused after a queue is emptied and reloaded.
    // The caller persists an action sequence independently of that queue.
    constexpr std::string_view prefix = "sync.action.";
    for (std::size_t i = 0; i < prefix.size(); ++i)
        storage[i] = prefix[i];
    const auto result = std::to_chars(storage.data() + prefix.size(),
                                      storage.data() + storage.size(), action);
    return result.ec == std::errc{}
        ? std::string_view{storage.data(),
                           static_cast<std::size_t>(result.ptr - storage.data())}
        : std::string_view{};
}

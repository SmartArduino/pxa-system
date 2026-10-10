#pragma once
#include "core.hpp"
#include <charconv>
#include <cstdio>
#include <string_view>

namespace pxa::detail {
// Error fallback only: fixed storage and no general printf formatter. Optional
// callers' own error handlers continue to take precedence.
[[gnu::noinline]] inline void report_error(std::string_view operation, Error error) noexcept {
    char number[16];
    auto end = std::to_chars(number, number + sizeof(number), static_cast<int>(error)).ptr;
    std::fwrite(operation.data(), 1, operation.size(), stderr);
    std::fwrite(number, 1, std::size_t(end - number), stderr);
    std::fputc('\n', stderr);
}
} // namespace pxa::detail

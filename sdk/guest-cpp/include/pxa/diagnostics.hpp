#pragma once
#include "core.hpp"
#include "log.hpp"
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

// WASI descriptors may be /dev/null. Use the declared PXA log capability for
// runtime diagnostics; stderr remains useful for native SDK tests.
[[gnu::noinline]] inline void report_error(Transport& transport, std::string_view operation, Error error) noexcept {
    std::array<char, 256> message{};
    const auto size = operation.size() < 240 ? operation.size() : 240;
    for (std::size_t i = 0; i < size; ++i) message[i] = operation[i];
    const auto end = std::to_chars(message.data() + size, message.data() + message.size(), static_cast<int>(error)).ptr;
    (void)LogService(transport).write(LogLevel::error, {message.data(), std::size_t(end-message.data())});
    report_error(operation, error);
}
} // namespace pxa::detail

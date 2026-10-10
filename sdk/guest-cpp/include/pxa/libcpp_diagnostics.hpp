#pragma once

// Optional libc++ customization. Include before any standard-library header
// in every application translation unit. Keep hardening checks and fatal
// termination, without bringing in printf's floating-point formatter.
namespace pxa::detail {
[[noreturn]] inline void bounded_libcpp_abort(const char*, ...) noexcept;
}
#ifndef _LIBCPP_VERBOSE_ABORT
#define _LIBCPP_VERBOSE_ABORT(...) ::pxa::detail::bounded_libcpp_abort(__VA_ARGS__)
#endif

#include <charconv>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstddef>

namespace pxa::detail {
[[noreturn, gnu::noinline]] inline void bounded_libcpp_abort_v(const char* format, va_list source) noexcept {
    char text[512];
    char* out = text;
    char* const end = text + sizeof(text);
    auto append = [&](const char* s) {
        if (!s) s = "(null)";
        while (*s && out != end) *out++ = *s++;
    };
    auto number = [&](auto value) {
        char digits[32];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        for (auto p = digits; p != result.ptr && out != end; ++p) *out++ = *p;
    };
    append("libc++ fatal: ");
    va_list args;
    va_copy(args, source);
    const char* p = format ? format : "(null diagnostic)";
    while (*p && out != end) {
        if (*p != '%') { *out++ = *p++; continue; }
        const char* start = p++;
        if (*p == '%') { *out++ = *p++; continue; }
        if (*p == 's') { append(va_arg(args, const char*)); ++p; continue; }
        if (*p == 'd' || *p == 'i') { number(va_arg(args, int)); ++p; continue; }
        if (*p == 'u') { number(va_arg(args, unsigned)); ++p; continue; }
        if (*p == 'z' && p[1] == 'u') { number(va_arg(args, std::size_t)); p += 2; continue; }
        if (*p == 't' && (p[1] == 'd' || p[1] == 'i')) { number(va_arg(args, std::ptrdiff_t)); p += 2; continue; }
        // A diagnostic with an unsupported format stays readable as a literal.
        // Stop interpreting arguments: guessing vararg types would be unsafe.
        append(start); break;
    }
    va_end(args);
    (void)std::fwrite(text, 1, std::size_t(out - text), stderr);
    (void)std::fputc('\n', stderr);
    std::abort();
}
[[noreturn, gnu::noinline]] inline void bounded_libcpp_abort(const char* format, ...) noexcept {
    va_list args;
    va_start(args, format);
    bounded_libcpp_abort_v(format, args);
}
} // namespace pxa::detail

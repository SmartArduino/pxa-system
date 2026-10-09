#pragma once
#include <cstdint>
#if defined(__wasi__)
#include <wasi/api.h>
#else
#include <chrono>
#endif
namespace pxa {
// Optional synchronous measurement clock. WASI builds must declare the
// monotonic-clock and wall-clock features required by clock_time_get.
inline bool profile_clock_ns(std::uint64_t& ns) noexcept {
#if defined(__wasi__)
    return __wasi_clock_time_get(__WASI_CLOCKID_MONOTONIC,1,&ns)==0;
#else
    ns=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    return true;
#endif
}
// Low 32 bits avoid libc timespec conversion and 64-bit division for short
// intervals on 32-bit AOT targets. Each interval must be shorter than 4.29 s.
inline std::uint32_t profile_short_tick() noexcept {
    std::uint64_t ns=0;
    (void)profile_clock_ns(ns);
    return static_cast<std::uint32_t>(ns);
}
inline std::uint32_t profile_short_us(std::uint32_t before,std::uint32_t after) noexcept {
    return (after-before)/1000u;
}
} // namespace pxa

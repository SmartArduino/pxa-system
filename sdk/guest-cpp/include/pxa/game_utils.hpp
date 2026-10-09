#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace pxa::game {
// Explicitly owned, fixed storage. Reusing the oldest slot is useful for
// bounded transient effects; callers must not retain a slot past N acquires.
template<class T, std::size_t N> class RecyclingPool {
    static_assert(N > 0 && N <= UINT32_MAX);
public:
    T& acquire() noexcept {
        auto& value = storage_[cursor_];
        cursor_ = cursor_ + 1 == N ? 0 : cursor_ + 1;
        value = T{};
        return value;
    }
    std::span<T, N> items() noexcept { return storage_; }
    std::span<const T, N> items() const noexcept { return storage_; }
private:
    std::array<T, N> storage_{};
    std::uint32_t cursor_ = 0;
};

struct DurationStats {
    std::uint64_t total_us = 0;
    std::uint32_t samples = 0;
    std::uint32_t min_us = UINT32_MAX;
    std::uint32_t max_us = 0;
    std::uint32_t mean_us() const noexcept {
        return samples ? static_cast<std::uint32_t>(total_us / samples) : 0;
    }
    void record(std::uint32_t us) noexcept {
        // Saturation keeps exceptionally long sessions from wrapping counters.
        if (samples == UINT32_MAX || UINT64_MAX - total_us < us) return;
        ++samples; total_us += us;
        if (us < min_us) min_us = us;
        if (us > max_us) max_us = us;
    }
};
// Does not import a clock, allocate a buffer, or log on its own. Record actual
// synchronous monotonic durations; asynchronous reply arrival is not CPU time.
template<std::size_t N> struct StageStatistics {
    std::array<DurationStats, N> stages{};
    void reset() noexcept { stages = {}; }
    bool record(std::size_t stage, std::uint32_t us) noexcept {
        if (stage >= N) return false;
        stages[stage].record(us); return true;
    }
};
} // namespace pxa::game

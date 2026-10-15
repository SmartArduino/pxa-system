#pragma once
#include "ui_input.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>

namespace pxa::ui {

struct PressOptions {
    std::chrono::microseconds long_press{550000};
    std::int32_t slop = 8; // Logical pixels, like CanvasPointer coordinates.
    constexpr bool valid() const noexcept { return long_press.count() >= 0 && slop >= 0; }
};
enum class PressResult : std::uint8_t { none, tap, long_press };

// One pointer stream, no allocation or timer. Feed events and call advance()
// from an existing frame clock. Native on_long_press views do that themselves.
class PressGesture {
public:
    [[nodiscard]] constexpr PressResult update(const CanvasPointer& event,
                                               PressOptions options = {}) noexcept {
        if (!options.valid()) { reset(); return PressResult::none; }
        if (event.action() == PointerPhase::down) {
            if (capture_.try_capture(event)) {
                began_ = event.timestamp_us;
                x_ = event.x; y_ = event.y; moved_ = fired_ = false;
            }
            return PressResult::none;
        }
        if (!capture_.owns(event) || event.timestamp_us < began_) return PressResult::none;
        const auto dx = std::int64_t(event.x) - x_, dy = std::int64_t(event.y) - y_;
        moved_ |= dx > options.slop || dx < -std::int64_t(options.slop) ||
                  dy > options.slop || dy < -std::int64_t(options.slop);
        if (event.action() == PointerPhase::cancel) { reset(); return PressResult::none; }
        if (event.action() != PointerPhase::up) return PressResult::none;
        auto result = PressResult::none;
        if (!moved_ && !fired_) {
            // If a frame was delayed, never turn a completed hold into a tap.
            result = advance(event.timestamp_us, options) ? PressResult::long_press : PressResult::tap;
        }
        reset();
        return result;
    }
    [[nodiscard]] constexpr bool advance(std::uint64_t now_us, PressOptions options = {}) noexcept {
        if (!pending(options) || now_us < began_ ||
            now_us - began_ < std::uint64_t(options.long_press.count())) return false;
        fired_ = true; // Consume before dispatch: callbacks may replace the UI.
        return true;
    }
    constexpr bool active() const noexcept { return capture_.active(); }
    constexpr bool owns(const CanvasPointer& event) const noexcept { return capture_.owns(event); }
    constexpr bool dragged() const noexcept { return moved_; }
    constexpr bool long_pressed() const noexcept { return fired_; }
    constexpr bool pending(PressOptions options = {}) const noexcept {
        return options.valid() && options.long_press.count() > 0 && active() && !moved_ && !fired_;
    }
    constexpr std::uint64_t deadline(PressOptions options = {}) const noexcept {
        if (!pending(options)) return 0;
        const auto delay = std::uint64_t(options.long_press.count());
        return began_ > UINT64_MAX - delay ? UINT64_MAX : began_ + delay;
    }
    constexpr std::int32_t start_x() const noexcept { return x_; }
    constexpr std::int32_t start_y() const noexcept { return y_; }
    constexpr void reset() noexcept { capture_.reset(); moved_ = fired_ = false; }
    // Page commits may change generation while preserving the same node.
    // Call only after a successful commit; stale UI events are filtered by Page.
    constexpr void rebase(std::uint32_t generation) noexcept {
        if (active()) capture_.generation_ = generation;
    }
private:
    PointerCapture capture_;
    std::uint64_t began_ = 0;
    std::int32_t x_ = 0, y_ = 0;
    bool moved_ = false, fired_ = false;
};

namespace detail {
struct GestureMethods {
    void (*tick)(void*, std::uint64_t);
    std::uint64_t (*deadline)(const void*);
    void (*cancel)(void*);
    void (*rebase)(void*, std::uint32_t);
};
template<class T> inline constexpr GestureMethods gesture_methods{
    [](void* p, std::uint64_t now) { static_cast<T*>(p)->gesture_tick(now); },
    [](const void* p) { return static_cast<const T*>(p)->gesture_deadline(); },
    [](void* p) { static_cast<T*>(p)->gesture_cancel(); },
    [](void* p, std::uint32_t generation) { static_cast<T*>(p)->gesture_rebase(generation); }
};
struct GestureHook { void* object; const GestureMethods* methods; };
template<std::size_t N> class GestureRegistry {
public:
    template<class T> bool add(T& value) noexcept {
        if (count_ == N) return false;
        hooks_[count_++] = {&value, &gesture_methods<T>}; return true;
    }
    void tick(std::uint64_t now) { for (std::size_t i=0; i<count_; ++i) hooks_[i].methods->tick(hooks_[i].object,now); }
    std::uint64_t deadline() const noexcept {
        std::uint64_t next = 0;
        for (std::size_t i=0; i<count_; ++i) {
            const auto due = hooks_[i].methods->deadline(hooks_[i].object);
            if (due && (!next || due < next)) next=due;
        }
        return next;
    }
    void cancel() noexcept { for (std::size_t i=0; i<count_; ++i) hooks_[i].methods->cancel(hooks_[i].object); }
    void rebase(std::uint32_t generation) noexcept { for (std::size_t i=0; i<count_; ++i) hooks_[i].methods->rebase(hooks_[i].object,generation); }
    void clear() noexcept { cancel(); count_=0; }
private:
    std::array<GestureHook,N> hooks_{};
    std::size_t count_=0;
};
template<> class GestureRegistry<0> {
public:
    template<class T> bool add(T&) noexcept { return false; }
    void tick(std::uint64_t) noexcept {}
    constexpr std::uint64_t deadline() const noexcept { return 0; }
    void cancel() noexcept {}
    void rebase(std::uint32_t) noexcept {}
    void clear() noexcept {}
};
struct NoClick { void operator()() const noexcept {} };
} // namespace detail
} // namespace pxa::ui

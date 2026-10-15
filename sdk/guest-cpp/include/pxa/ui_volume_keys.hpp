#pragma once
#include "ui.hpp"

namespace pxa::ui {

// Wrap the top-level view. Capture is opt-in and reactive; false gives volume
// controls back to the system. Adds one binding/handler only to users of it.
template<class View, class Callback>
class VolumeKeysView {
public:
    static constexpr Capacity capacity = [] {
        auto value = capacity_of<View>;
        ++value.bindings; ++value.handlers;
        return value;
    }();
    static constexpr bool overlay = [] {
        if constexpr (requires { View::overlay; }) return View::overlay;
        else return false;
    }();
    constexpr VolumeKeysView(View view, State<bool>& enabled, Callback callback)
        : view_(std::move(view)), enabled_(enabled), callback_(std::move(callback)) {}
    void reset_mount() noexcept requires detail::ResettableView<View> { detail::reset_view(view_); }
    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        if (parent != 1) return owner.fail(Error::invalid_argument);
        return view_.render(owner, parent) && write(owner.transaction(), parent, enabled_) &&
            owner.template observe<State<bool>, write>(enabled_, parent) &&
            owner.on_volume_key(parent, *this);
    }
    void operator()(const VolumeKeyEvent& event) {
        if (enabled_.get()) std::invoke(callback_, event);
    }
private:
    static bool write(Transaction<>& tx, std::uint32_t node, const State<bool>& enabled) {
        return tx.u64(node, protocol::event_mask, enabled.get() ? UINT64_C(1) << 4 : 0);
    }
    View view_;
    State<bool>& enabled_;
    [[no_unique_address]] Callback callback_;
};

template<class View, class Callback>
    requires ViewLike<std::decay_t<View>> &&
        std::invocable<std::decay_t<Callback>&, const VolumeKeyEvent&> &&
        std::same_as<std::invoke_result_t<std::decay_t<Callback>&, const VolumeKeyEvent&>, void>
constexpr auto CaptureVolumeKeys(View&& view, State<bool>& enabled, Callback&& callback) {
    return VolumeKeysView<std::decay_t<View>, std::decay_t<Callback>>(
        std::forward<View>(view), enabled, std::forward<Callback>(callback));
}

} // namespace pxa::ui

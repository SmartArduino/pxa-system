#pragma once
#include "ui.hpp"
#include "ui_geometry.hpp"

namespace pxa::ui {

namespace detail {
struct DefaultVisibility {};
template<bool Enabled> struct VisibilitySubscription {};
template<> struct VisibilitySubscription<true> { Subscription item; };
}

template<class Child, bool Bound, class Visibility = detail::DefaultVisibility, class Inset = Dp>
class SafeAreaView : public ViewModifiers {
    template<class, bool, class, class> friend class SafeAreaView;
    static constexpr bool state_visibility = std::same_as<Visibility, State<bool>*>;
public:
    static constexpr Capacity capacity = [] {
        auto result = capacity_of<Child>;
        ++result.nodes;
        if constexpr (Bound || state_visibility) ++result.bindings;
        return result;
    }();
    using Metrics = std::conditional_t<Bound, State<DisplayMetrics>*, DisplayMetrics>;
    SafeAreaView(Metrics metrics, Child child, Visibility visible = {}, Inset inset = {})
        : metrics_(metrics), child_(std::move(child)), inset_(inset), visible_(visible) {}
    template<std::uint16_t Key, class Self, class Value>
        requires ((Key == protocol::visible && (std::same_as<Value, bool> ||
                  std::same_as<Value, State<bool>*>)) ||
                  (Key == 268 && (std::same_as<Value, Dp> || std::same_as<Value, Padding>)))
    constexpr auto apply_modifier(this Self&& self, Value value) {
        if constexpr (Key == protocol::visible)
            return SafeAreaView<Child, Bound, Value, Inset>(self.metrics_,
                std::forward<Self>(self).child_, value, self.inset_);
        else return SafeAreaView<Child, Bound, Visibility, Value>(self.metrics_,
                std::forward<Self>(self).child_, self.visible_, value);
    }
    void reset_mount() noexcept requires detail::ResettableView<Child> { detail::reset_view(child_); }
    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        const auto node = owner.create(parent, protocol::box);
        if (!node || !owner.transaction().u8(node, protocol::layout, protocol::column) ||
            !owner.transaction().fill(node, protocol::width) ||
            !owner.transaction().fill(node, protocol::height) ||
            !write(owner.transaction(), node, *this)) return false;
        if constexpr (Bound || state_visibility)
            if (!owner.template observe<SafeAreaView, write>(*this, node)) return false;
        return child_.render(owner, node);
    }
    void subscribe(Subscription& item) noexcept requires (Bound || state_visibility) {
        if constexpr (Bound) metrics_->subscribe(item);
        if constexpr (state_visibility) {
            if constexpr (Bound) {
                visibility_subscription_.item.dirty_word = item.dirty_word;
                visibility_subscription_.item.mask = item.mask;
                visible_->subscribe(visibility_subscription_.item);
            } else visible_->subscribe(item);
        }
    }
    void unsubscribe(Subscription& item) noexcept requires (Bound || state_visibility) {
        if constexpr (Bound) metrics_->unsubscribe(item);
        if constexpr (state_visibility) {
            if constexpr (Bound) visible_->unsubscribe(visibility_subscription_.item);
            else visible_->unsubscribe(item);
        }
    }
private:
    const DisplayMetrics& metrics() const noexcept {
        if constexpr (Bound) return metrics_->get();
        else return metrics_;
    }
    static bool write(Transaction<>& tx, std::uint32_t node, const SafeAreaView& view) {
        const auto& d = view.metrics();
        if (!d.width || !d.height || d.width > 65535 || d.height > 65535 ||
            !d.density_q16 || d.shape > 2) return tx.fail(Error::invalid_argument);
        const auto rect = safe_rectangle(d);
        const std::array<std::uint32_t, 4> pixels{std::uint32_t(rect.x), std::uint32_t(rect.y),
            d.width - std::uint32_t(rect.x + rect.width),
            d.height - std::uint32_t(rect.y + rect.height)};
        std::array<std::byte, 16> bytes{};
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            const auto inset = [&] {
                if constexpr (std::same_as<Inset, Dp>) return view.inset_.value;
                else {
                    switch (i) {
                        case 0: return view.inset_.left.value;
                        case 1: return view.inset_.top.value;
                        case 2: return view.inset_.right.value;
                        default: return view.inset_.bottom.value;
                    }
                }
            }();
            if (inset < 0 || inset > INT32_MAX / 64)
                return tx.fail(Error::invalid_argument);
            // Protocol padding uses 1/64 dp. Round inward in physical pixels.
            const std::uint64_t value = (std::uint64_t(pixels[i]) * 4194304u +
                d.density_q16 - 1) / d.density_q16 + std::uint64_t(inset) * 64;
            if (value > INT32_MAX) return tx.fail(Error::limit_exceeded);
            wire::put32(bytes.data() + i * 4, std::uint32_t(value));
        }
        bool visible = true;
        if constexpr (std::same_as<Visibility, bool>) visible = view.visible_;
        else if constexpr (state_visibility) visible = view.visible_->get();
        // Compare without rounding away any positive physical content size.
        visible = visible && (std::uint64_t(wire::get32(bytes.data())) + wire::get32(bytes.data() + 8)) *
            d.density_q16 < std::uint64_t(d.width) * 4194304u &&
            (std::uint64_t(wire::get32(bytes.data() + 4)) + wire::get32(bytes.data() + 12)) *
            d.density_q16 < std::uint64_t(d.height) * 4194304u;
        return tx.u8(node, protocol::visible, visible) &&
            tx.property(node, 268, bytes);
    }
    Metrics metrics_;
    Child child_;
    Inset inset_{};
    [[no_unique_address]] Visibility visible_{};
    [[no_unique_address]] detail::VisibilitySubscription<Bound && state_visibility> visibility_subscription_{};
};

template<class Child> requires ViewLike<std::decay_t<Child>>
auto SafeArea(DisplayMetrics metrics, Child&& child) {
    return SafeAreaView<std::decay_t<Child>, false>(metrics, std::forward<Child>(child));
}
template<class Child> requires ViewLike<std::decay_t<Child>>
auto SafeArea(State<DisplayMetrics>& metrics, Child&& child) {
    return SafeAreaView<std::decay_t<Child>, true>(&metrics, std::forward<Child>(child));
}
} // namespace pxa::ui

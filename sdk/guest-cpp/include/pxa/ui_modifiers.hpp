#pragma once
#include "ui.hpp"

namespace pxa::ui::detail {

template<class View> inline constexpr std::uint32_t text_node_offset = [] {
    if constexpr (requires { View::text_node_offset; }) return View::text_node_offset;
    else return std::uint32_t{0};
}();
template<class View> inline constexpr std::uint32_t interaction_node_offset = [] {
    if constexpr (requires { View::interaction_node_offset; }) return View::interaction_node_offset;
    else return std::uint32_t{0};
}();
template<std::uint16_t Key, class View>
constexpr decltype(auto) without_modifier(View&& view) {
    if constexpr (requires { std::forward<View>(view).template without_modifier<Key>(); })
        return std::forward<View>(view).template without_modifier<Key>();
    else return std::forward<View>(view);
}

template<std::uint16_t Key, class Value>
bool write_modifier(Transaction<>& tx, std::uint32_t node, const Value& value) {
    if constexpr (std::same_as<Value, CanvasRegion>) {
        return tx.u8(node, protocol::position, 1) &&
            tx.logical_px(node, protocol::x, value.x) &&
            tx.logical_px(node, protocol::y, value.y) &&
            tx.logical_px(node, protocol::width, value.width) &&
            tx.logical_px(node, protocol::height, value.height);
    } else if constexpr (std::same_as<Value, Padding>) {
        const std::array values{value.left, value.top, value.right, value.bottom};
        std::array<std::byte, 16> bytes{};
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (values[i].value < 0 || values[i].value > INT32_MAX / 64)
                return tx.fail(Error::invalid_argument);
            wire::put32(bytes.data() + 4 * i, std::uint32_t(values[i].value * 64));
        }
        return tx.property(node, Key, bytes);
    } else if constexpr (std::same_as<Value, Dp>) {
        if constexpr (Key >= protocol::width && Key <= protocol::max_height)
            return tx.template logical_px<Key>(node, value.value);
        else if constexpr (Key == 268) {
            if (value.value < 0 || value.value > INT32_MAX / 64)
                return tx.fail(Error::invalid_argument);
            std::array<std::byte, 16> bytes{};
            for (std::size_t i = 0; i < 4; ++i)
                wire::put32(bytes.data() + i * 4, std::uint32_t(value.value * 64));
            return tx.property(node, Key, bytes);
        } else return tx.dp(node, Key, value.value);
    } else if constexpr (std::same_as<Value, FillLength>) {
        return tx.fill(node, Key);
    } else if constexpr (std::same_as<Value, Color>) {
        if (value > Color::inverse_primary) return tx.fail(Error::invalid_argument);
        std::array<std::byte, 8> bytes{};
        bytes[1] = std::byte(static_cast<std::uint8_t>(value));
        return tx.property(node, Key, bytes);
    } else if constexpr (std::same_as<Value, std::uint32_t>) {
        return tx.rgba(node, Key, value);
    } else if constexpr (std::same_as<Value, Font>) {
        return value <= Font::display ? tx.u16(node, Key, std::uint16_t(value))
                                     : tx.fail(Error::invalid_argument);
    } else if constexpr (std::same_as<Value, Align> || std::same_as<Value, TextAlign>) {
        return std::uint8_t(value) <= 2 ? tx.u8(node, Key, std::uint8_t(value))
                                      : tx.fail(Error::invalid_argument);
    } else if constexpr (std::same_as<Value, Justify>) {
        const auto v = std::uint8_t(value);
        return v <= 2 || v == 4 || v == 5 ? tx.u8(node, Key, v)
                                         : tx.fail(Error::invalid_argument);
    } else if constexpr (std::same_as<Value, bool>) {
        return tx.u8(node, Key, value);
    } else if constexpr (std::same_as<Value, State<bool>*>) {
        return tx.u8(node, Key, value->get());
    } else return tx.u16(node, Key, value);
}

template<std::uint16_t Key, class Child, class Value>
class ModifiedView : public ViewModifiers {
public:
    static constexpr Capacity capacity = [] {
        auto result = capacity_of<Child>;
        if constexpr (std::same_as<Value, State<bool>*>) ++result.bindings;
        return result;
    }();
    static constexpr std::uint32_t text_node_offset = detail::text_node_offset<Child>;
    static constexpr std::uint32_t interaction_node_offset = detail::interaction_node_offset<Child>;
    static constexpr bool overlay = [] {
        if constexpr (requires { Child::overlay; }) return Child::overlay;
        else return false;
    }();
    constexpr ModifiedView(Child child, Value value)
        : child_(std::move(child)), value_(std::move(value)) {}
    void reset_mount() noexcept requires ResettableView<Child> { reset_view(child_); }

    template<std::uint16_t Remove, class Self>
    constexpr auto without_modifier(this Self&& self) {
        auto child = detail::without_modifier<Remove>(std::forward<Self>(self).child_);
        if constexpr (Remove == Key) return child;
        else return ModifiedView<Key, decltype(child), Value>(
            std::move(child), std::forward<Self>(self).value_);
    }
    // Intrinsic layout properties (SafeArea's visibility/padding) compose at
    // their source even when unrelated modifiers sit between the calls.
    template<std::uint16_t Property, class Self, class NewValue>
        requires requires(Child&& child, NewValue value) {
            std::move(child).template apply_modifier<Property>(value);
        }
    constexpr auto apply_modifier(this Self&& self, NewValue value) {
        auto child = std::forward<Self>(self).child_.template apply_modifier<Property>(value);
        return ModifiedView<Key, decltype(child), Value>(
            std::move(child), std::forward<Self>(self).value_);
    }

    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        return owner.decorate(child_, parent, [this](auto& page, std::uint32_t root) {
            constexpr bool text_property = Key == protocol::font_role ||
                Key == protocol::foreground || Key == protocol::text_align;
            const auto node = root + (text_property ? text_node_offset :
                Key == protocol::enabled ? interaction_node_offset : 0);
            if (!write_modifier<Key>(page.transaction(), node, value_)) return false;
            if constexpr (std::same_as<Value, State<bool>*>)
                return page.template observe<State<bool>, [](Transaction<>& tx,
                    std::uint32_t id, const State<bool>& state) {
                    return tx.u8(id, Key, state.get());
                }>(*value_, node);
            return true;
        });
    }
private:
    Child child_;
    [[no_unique_address]] Value value_;
};

template<std::uint16_t Key, class View, class Value>
constexpr auto modify(View&& view, Value value) {
    static_assert(ViewLike<std::decay_t<View>>,
        "UI modifiers require a view; finish Button.on_click() or use Component(model)");
    if constexpr (requires { std::forward<View>(view).template apply_modifier<Key>(value); })
        return std::forward<View>(view).template apply_modifier<Key>(value);
    else {
        auto child = without_modifier<Key>(std::forward<View>(view));
        return ModifiedView<Key, decltype(child), Value>(std::move(child), std::move(value));
    }
}

} // namespace pxa::ui::detail

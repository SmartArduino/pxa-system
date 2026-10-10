#pragma once
#include "ui.hpp"

namespace pxa::ui {

// Optional absolute layout and literal styling. No state, cache or allocation
// is added to ordinary Text/Button/Box views. Coordinates are logical pixels.
struct WidgetColor {
    std::uint32_t value = 0;
    bool is_theme = false;
    constexpr WidgetColor(std::uint32_t rgba = 0) : value(rgba) {}
    constexpr WidgetColor(Color role) : value(static_cast<std::uint8_t>(role)), is_theme(true) {}
    template<class Owner> bool write(Owner& owner, std::uint32_t node, std::uint16_t property) const {
        return is_theme ? owner.theme(node, property, static_cast<Color>(value))
                        : owner.transaction().rgba(node, property, value);
    }
};
struct WidgetStyle {
    CanvasRegion box{};
    WidgetColor foreground = Color::text, background = 0;
    Font font = Font::body;
    std::int32_t radius = 0;
    std::uint8_t align = 0;
    std::int32_t border_width = 0;
    WidgetColor border = Color::border;
};

template<class Owner>
[[gnu::noinline]] bool widget_style(Owner& owner, std::uint32_t node, const WidgetStyle& style) {
    auto& tx = owner.transaction();
    if (style.box.x < 0 || style.box.y < 0 || style.box.width <= 0 ||
        style.box.height <= 0 || style.font > Font::display || style.align > 2 ||
        style.border_width < 0 || style.border_width > INT32_MAX / 64)
        return owner.fail(Error::invalid_argument);
    return tx.u8(node, protocol::position, 1) &&
        tx.logical_px(node, protocol::x, style.box.x) &&
        tx.logical_px(node, protocol::y, style.box.y) &&
        tx.logical_px(node, protocol::width, style.box.width) &&
        tx.logical_px(node, protocol::height, style.box.height) &&
        owner.padding(node, 0) && tx.dp(node, protocol::border_width, style.border_width) &&
        (style.border_width == 0 || style.border.write(owner, node, protocol::border_color)) &&
        tx.dp(node, protocol::radius, style.radius) &&
        style.background.write(owner, node, protocol::background) &&
        style.foreground.write(owner, node, protocol::foreground) &&
        tx.u16(node, protocol::font_role, std::uint16_t(style.font)) &&
        tx.u8(node, protocol::text_align, style.align);
}

class StyledTextView : public ViewModifiers {
public:
    static constexpr Capacity capacity{1};
    constexpr StyledTextView(std::string_view value, WidgetStyle style)
        : value_(value), style_(style) {}
    template<class Owner> [[gnu::noinline]] bool render(Owner& owner, std::uint32_t parent) {
        auto node = owner.create(parent, protocol::text);
        return node && widget_style(owner, node, style_) &&
            owner.transaction().text(node, value_);
    }
private:
    std::string_view value_;
    WidgetStyle style_;
};
// Borrowed text must live until render; Host owns a copy after commit.
inline auto Text(std::string_view value, WidgetStyle style) {
    return StyledTextView(value, style);
}
class StyledBoundTextView : public ViewModifiers {
public:
    static constexpr Capacity capacity{1, 1};
    State<std::string>& value;
    WidgetStyle style;
    StyledBoundTextView(State<std::string>& text, WidgetStyle appearance)
        : value(text), style(appearance) {}
    template<class Owner> [[gnu::noinline]] bool render(Owner& owner, std::uint32_t parent) {
        auto node = owner.create(parent, protocol::text);
        return node && widget_style(owner, node, style) &&
            write_string_text(owner.transaction(), node, value.get()) &&
            owner.template bind<std::string, write_string_text>(value, node);
    }
};
inline auto Text(State<std::string>& value, WidgetStyle style) {
    return StyledBoundTextView{value, style};
}
template<class T> requires std::same_as<std::remove_cvref_t<T>, std::string> &&
                          (!std::is_lvalue_reference_v<T>)
auto Text(T&&, WidgetStyle) = delete;

template<class F> class StyledButtonView : public ViewModifiers {
public:
    static constexpr Capacity capacity{1, 0, 1};
    StyledButtonView(std::string_view label, WidgetStyle style, F callback)
        : label_(label), style_(style), callback_(std::move(callback)) {}
    template<class Owner> [[gnu::noinline]] bool render(Owner& owner, std::uint32_t parent) {
        auto node = owner.create(parent, protocol::control, protocol::button);
        return node && widget_style(owner, node, style_) &&
            owner.transaction().text(node, label_) &&
            owner.transaction().u64(node, protocol::event_mask, 1) &&
            owner.on_click(node, callback_);
    }
private:
    std::string_view label_;
    WidgetStyle style_;
    [[no_unique_address]] F callback_;
};
class StyledButtonBuilder {
public:
    StyledButtonBuilder(std::string_view label, WidgetStyle style)
        : label_(label), style_(style) {}
    // Explicitly borrowed: callback must outlive the mounted Page/fragment.
    template<class F> requires ClickCallback<std::decay_t<F>>
    auto on_click(F&& callback) const {
        return StyledButtonView<std::decay_t<F>>(label_, style_, std::forward<F>(callback));
    }
    template<class F> requires ClickCallback<F>
    auto on_click_ref(F& callback) const { return on_click(std::ref(callback)); }
    // Pointer-only controls can distinguish tap, drag and a held press without
    // also receiving a click on release. Uses the existing Host pointer ABI.
    template<class F> class PointerView : public ViewModifiers {
    public:
        static constexpr Capacity capacity{1, 0, 1};
        std::string_view label;
        WidgetStyle style;
        [[no_unique_address]] F callback;
        PointerView(std::string_view text, WidgetStyle appearance, F fn)
            : label(text), style(appearance), callback(std::move(fn)) {}
        template<class Owner> [[gnu::noinline]] bool render(Owner& owner, std::uint32_t parent) {
            auto node = owner.create(parent, protocol::control, protocol::button);
            return node && widget_style(owner, node, style) &&
                owner.transaction().text(node, label) &&
                owner.transaction().u64(node, protocol::event_mask, protocol::event_mask_pointer) &&
                owner.on_pointer(node, callback);
        }
    };
    template<class F> requires PointerCallback<std::decay_t<F>>
    auto on_pointer(F&& callback) const {
        return PointerView<std::decay_t<F>>{label_, style_, std::forward<F>(callback)};
    }
    template<class F> requires PointerCallback<F>
    auto on_pointer_ref(F& callback) const { return on_pointer(std::ref(callback)); }
private:
    std::string_view label_;
    WidgetStyle style_;
};
inline auto Button(std::string_view label, WidgetStyle style) {
    return StyledButtonBuilder(label, style);
}
template<class T> requires std::same_as<std::remove_cvref_t<T>, std::string> &&
                          (!std::is_lvalue_reference_v<T>)
auto Button(T&&, WidgetStyle) = delete;

template<class... Children> class StyledBoxView : public ViewModifiers {
public:
    static constexpr Capacity capacity = container_capacity<Children...>();
    StyledBoxView(WidgetStyle style, Children... children)
        : style_(style), children_(std::move(children)...) {}
    template<class Owner> [[gnu::noinline]] bool render(Owner& owner, std::uint32_t parent) {
        auto node = owner.create(parent, protocol::box);
        return node && widget_style(owner, node, style_) &&
            owner.transaction().u8(node, protocol::layout, protocol::stack) &&
            std::apply([&](auto&... child) {
                return (child.render(owner, node) && ...);
            }, children_);
    }
private:
    WidgetStyle style_;
    std::tuple<Children...> children_;
};
template<class... Children> requires (ViewLike<std::decay_t<Children>> && ...)
auto Box(WidgetStyle style, Children&&... children) {
    return StyledBoxView<std::decay_t<Children>...>(style,
        std::forward<Children>(children)...);
}

} // namespace pxa::ui

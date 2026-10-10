#pragma once

#include "i18n.hpp"
#include "ui.hpp"

namespace pxa::ui {
namespace detail {
inline std::string_view i18n_view(std::string_view value) noexcept { return value; }
template<std::size_t N> std::string_view i18n_view(const i18n::TextBuffer<N>& value) noexcept { return value.view(); }
template<class T> bool write_i18n_text(Transaction<>& tx, std::uint32_t node, const T& value) noexcept {
    return tx.text(node, i18n_view(value));
}
} // namespace detail

// Optional borrowed/static and owned/bounded text bindings. No std::string copy.
class BorrowedTextView : public TextAppearance {
public:
    static constexpr Capacity capacity{1};
    explicit BorrowedTextView(std::string_view value) noexcept : value_(value) {}
    template<class Page> bool render(Page& page, std::uint32_t parent) {
        const auto node = page.create(parent, protocol::text);
        return node && page.transaction().text(node, value_) && appearance(page, node);
    }
private:
    std::string_view value_;
};
template<class T> class BoundedTextView : public TextAppearance {
public:
    static constexpr Capacity capacity{1, 1};
    explicit BoundedTextView(State<T>& state) noexcept : state_(state) {}
    template<class Page> bool render(Page& page, std::uint32_t parent) {
        const auto node = page.create(parent, protocol::text);
        return node && detail::write_i18n_text(page.transaction(), node, state_.get()) &&
            page.template bind<T, detail::write_i18n_text<T>>(state_, node) && appearance(page, node);
    }
private:
    State<T>& state_;
};
template<class T> requires std::same_as<std::remove_cvref_t<T>, std::string_view>
auto Text(T&& value) noexcept { return BorrowedTextView(value); }
inline auto Text(State<std::string_view>& value) noexcept { return BoundedTextView(value); }
template<std::size_t N> auto Text(State<i18n::TextBuffer<N>>& value) noexcept { return BoundedTextView(value); }

template<class F> class BoundedButtonView {
public:
    static constexpr Capacity capacity{2, 1, 1};
    BoundedButtonView(State<std::string_view>& label, F callback) : label_(label), callback_(std::move(callback)) {}
    template<class Page> bool render(Page& page, std::uint32_t parent) {
        const auto node = page.create(parent, protocol::control, protocol::button);
        if (!node || !page.transaction().u8(node, protocol::layout, protocol::row) ||
            !page.transaction().u64(node, protocol::event_mask, 1) ||
            !page.theme(node, protocol::background, Color::primary) || !page.on_click(node, callback_)) return false;
        const auto text = page.create(node, protocol::text);
        return text && detail::write_i18n_text(page.transaction(), text, label_.get()) &&
            page.template bind<std::string_view, detail::write_i18n_text<std::string_view>>(label_, text) &&
            page.transaction().u16(text, protocol::font_role, 1) &&
            page.theme(text, protocol::foreground, Color::on_primary);
    }
private:
    State<std::string_view>& label_;
    [[no_unique_address]] F callback_;
};
class BoundedButtonBuilder {
public:
    explicit BoundedButtonBuilder(State<std::string_view>& label) noexcept : label_(label) {}
    template<class F> auto on_click(F&& callback) const {
        return BoundedButtonView<std::decay_t<F>>(label_, std::forward<F>(callback));
    }
private:
    State<std::string_view>& label_;
};
inline auto Button(State<std::string_view>& label) noexcept { return BoundedButtonBuilder(label); }
} // namespace pxa::ui

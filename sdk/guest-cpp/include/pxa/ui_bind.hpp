#pragma once
#include "ui.hpp"

namespace pxa::ui {
namespace detail {
template<class T> struct TextResult { using value_type = T; static constexpr bool result = false; };
template<class T> struct TextResult<Result<T>> { using value_type = T; static constexpr bool result = true; };
template<class T> concept TextValue = std::integral<T> || std::floating_point<T> ||
    std::convertible_to<const T&, std::string_view> || requires(const T& value) {
        { value.view() } -> std::convertible_to<std::string_view>;
    };
template<class T> concept TextOutput = TextValue<typename TextResult<std::remove_cvref_t<T>>::value_type>;

template<TextOutput T>
bool write_text_output(Transaction<>& tx, std::uint32_t node, const T& value) {
    if constexpr (TextResult<std::remove_cvref_t<T>>::result) {
        return value ? write_text_output(tx, node, *value) : tx.fail(value.error());
    } else if constexpr (std::same_as<T, bool>) {
        return tx.text(node, value ? "true" : "false");
    } else if constexpr (std::integral<T> || std::floating_point<T>) {
        char text[128];
        const auto [end, error] = std::to_chars(text, text + sizeof(text), value);
        return error == std::errc{} ? tx.text(node, {text, std::size_t(end - text)})
                                   : tx.fail(Error::limit_exceeded);
    } else if constexpr (std::convertible_to<const T&, std::string_view>) {
        return tx.text(node, std::string_view(value));
    } else return tx.text(node, value.view());
}

template<class T> struct StateSource {
    using value_type = T;
    State<T>* state;
    StateSource(State<T>& value) : state(&value) {}
    const T& get() const noexcept { return state->get(); }
    void subscribe(Subscription& item) noexcept { state->subscribe(item); }
    void unsubscribe(Subscription& item) noexcept { state->unsubscribe(item); }
};
template<class T> inline constexpr bool is_state_source = false;
template<class T> inline constexpr bool is_state_source<StateSource<T>> = true;
template<class Source, class Owner, auto Write>
bool bind_text_source(Owner& owner, Source& source, std::uint32_t node) {
    if constexpr (is_state_source<Source>)
        return owner.template bind<typename Source::value_type,
            write_text_output<typename Source::value_type>>(*source.state, node);
    else return owner.template observe<Source, Write>(source, node);
}
} // namespace detail

// The returned source is owned by one view. Dependencies are borrowed; no
// cached string, extra State, global dependency graph or heap storage is added.
template<class Function, class... Sources>
class ComputedSource {
    static_assert(sizeof...(Sources) > 0);
public:
    ComputedSource(Function function, Sources&... sources)
        : function_(std::move(function)), sources_(&sources...) {}
    ComputedSource(const ComputedSource&) = delete;
    ComputedSource(ComputedSource&& other)
        : function_(std::move(other.function_)), sources_(other.sources_) {
        if (other.subscriptions_[0].dirty_word) std::abort();
    }
    ~ComputedSource() {
        if (subscriptions_[0].dirty_word) detach(std::index_sequence_for<Sources...>{});
    }
    decltype(auto) get() const {
        return std::apply([this](auto*... sources) -> decltype(auto) {
            return std::invoke(function_, sources->get()...);
        }, sources_);
    }
    void subscribe(Subscription& item) noexcept {
        if (subscriptions_[0].dirty_word) std::abort();
        attach(item, std::index_sequence_for<Sources...>{});
    }
    void unsubscribe(Subscription&) noexcept { detach(std::index_sequence_for<Sources...>{}); }
private:
    template<std::size_t... I> void attach(Subscription& item, std::index_sequence<I...>) {
        ((subscriptions_[I].dirty_word = item.dirty_word,
          subscriptions_[I].mask = item.mask,
          std::get<I>(sources_)->subscribe(subscriptions_[I])), ...);
    }
    template<std::size_t... I> void detach(std::index_sequence<I...>) {
        (std::get<I>(sources_)->unsubscribe(subscriptions_[I]), ...);
    }
    [[no_unique_address]] mutable Function function_;
    std::tuple<Sources*...> sources_;
    std::array<Subscription, sizeof...(Sources)> subscriptions_{};
};

template<class Function, class... T>
    requires (sizeof...(T) > 0) && std::invocable<std::decay_t<Function>&, const T&...>
auto Computed(Function&& function, State<T>&... sources) {
    return ComputedSource<std::decay_t<Function>, State<T>...>(
        std::forward<Function>(function), sources...);
}

template<class Source>
    requires detail::TextOutput<decltype(std::declval<const Source&>().get())>
class SourceTextView : public TextAppearance {
public:
    static constexpr Capacity capacity{1, 1};
    explicit SourceTextView(Source source) : source_(std::move(source)) {}
    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        const auto node = owner.create(parent, protocol::text);
        return node && write(owner.transaction(), node, source_) &&
            detail::bind_text_source<Source, Owner, write>(owner, source_, node) && appearance(owner, node);
    }
private:
    static bool write(Transaction<>& tx, std::uint32_t node, const Source& source) {
        return detail::write_text_output(tx, node, source.get());
    }
    Source source_;
};

template<class T> requires detail::TextValue<T>
auto Text(State<T>& state) { return SourceTextView(detail::StateSource<T>(state)); }
template<class F, class... S>
auto Text(ComputedSource<F, S...>&& source) {
    return SourceTextView(std::move(source));
}

class BorrowedTextView : public TextAppearance {
public:
    static constexpr Capacity capacity{1};
    explicit BorrowedTextView(std::string_view text) : text_(text) {}
    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        const auto node = owner.create(parent, protocol::text);
        return node && owner.transaction().text(node, text_) && appearance(owner, node);
    }
private:
    std::string_view text_;
};
template<class T> requires std::same_as<std::remove_cvref_t<T>, std::string_view>
auto Text(T&& text) { return BorrowedTextView(text); }

template<class Source, class Callback, bool Pointer = false>
class SourceButtonView : public ViewModifiers {
public:
    static constexpr Capacity capacity{2, 1, 1};
    static constexpr std::uint32_t text_node_offset = 1;
    SourceButtonView(Source source, Callback callback)
        : source_(std::move(source)), callback_(std::move(callback)) {}
    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        const auto node = owner.create(parent, protocol::control, protocol::button);
        if (!node || !owner.transaction().u8(node, protocol::layout, protocol::row) ||
            !owner.transaction().u64(node, protocol::event_mask,
                                    Pointer ? protocol::event_mask_pointer : 1) ||
            !owner.theme(node, protocol::background, Color::primary)) return false;
        if constexpr (Pointer) {
            if (!owner.on_pointer(node, callback_)) return false;
        } else if (!owner.on_click(node, callback_)) return false;
        const auto label = owner.create(node, protocol::text);
        return label && write(owner.transaction(), label, source_) &&
            detail::bind_text_source<Source, Owner, write>(owner, source_, label) &&
            owner.transaction().u16(label, protocol::font_role, 1) &&
            owner.theme(label, protocol::foreground, Color::on_primary);
    }
private:
    static bool write(Transaction<>& tx, std::uint32_t node, const Source& source) {
        return detail::write_text_output(tx, node, source.get());
    }
    Source source_;
    [[no_unique_address]] Callback callback_;
};
template<class Source> class SourceButtonBuilder {
public:
    explicit SourceButtonBuilder(Source source) : source_(std::move(source)) {}
    template<class Callback> requires ClickCallback<std::decay_t<Callback>>
    auto on_click(Callback&& callback) && {
        return SourceButtonView<Source, std::decay_t<Callback>>(
            std::move(source_), std::forward<Callback>(callback));
    }
    template<class Callback> auto on_click_ref(Callback& callback) && {
        return std::move(*this).on_click(std::ref(callback));
    }
    template<class Callback> requires PointerCallback<std::decay_t<Callback>>
    auto on_pointer(Callback&& callback) && {
        return SourceButtonView<Source, std::decay_t<Callback>, true>(
            std::move(source_), std::forward<Callback>(callback));
    }
    template<class Callback> auto on_pointer_ref(Callback& callback) && {
        return std::move(*this).on_pointer(std::ref(callback));
    }
private:
    Source source_;
};
template<class T> requires detail::TextValue<T>
auto Button(State<T>& label) {
    return SourceButtonBuilder(detail::StateSource<T>(label));
}
template<class F, class... S>
auto Button(ComputedSource<F, S...>&& label) {
    return SourceButtonBuilder(std::move(label));
}

} // namespace pxa::ui

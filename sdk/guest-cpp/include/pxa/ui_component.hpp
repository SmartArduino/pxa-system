#pragma once
#include "ui.hpp"
#include <memory>
#include <optional>

namespace pxa::ui {
namespace detail {
struct IdentityComponentView {
    template<class V> constexpr decltype(auto) operator()(V&& view) const { return std::forward<V>(view); }
};
template<std::uint16_t Key, class Value, class Previous> struct ComponentModifier {
    [[no_unique_address]] Previous previous;
    Value value;
    template<class V> auto operator()(V view) { return modify<Key>(previous(std::move(view)), value); }
    template<std::uint16_t Remove, class Self> constexpr auto without_modifier(this Self&& self) {
        auto previous = detail::without_modifier<Remove>(std::forward<Self>(self).previous);
        if constexpr (Remove == Key) return previous;
        else return ComponentModifier<Key, Value, decltype(previous)>{
            std::move(previous), std::forward<Self>(self).value};
    }
};

template<class T, class Transform = IdentityComponentView> class ComponentView : public ViewModifiers {
    template<class, class> friend class ComponentView;
    using ModelView = decltype(std::declval<T&>().view());
    using Child = std::remove_cvref_t<std::invoke_result_t<Transform&, ModelView>>;
    static_assert(ViewLike<Child>, "Component.view() must return a UI view");
public:
    static constexpr Capacity capacity = capacity_of<Child>;
    static constexpr std::uint32_t text_node_offset = detail::text_node_offset<Child>;
    static constexpr std::uint32_t interaction_node_offset = detail::interaction_node_offset<Child>;
    static constexpr bool overlay = [] {
        if constexpr (requires { Child::overlay; }) return Child::overlay;
        else return false;
    }();
    explicit ComponentView(T model, Transform transform = {})
        : model_(std::move(model)), transform_(std::move(transform)) {}
    ComponentView(const ComponentView&) = delete;
    ComponentView(ComponentView&& other)
        : model_(std::move(other.model_)), transform_(std::move(other.transform_)) {
        if (other.child_) std::abort();
    }
    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        if (child_) return owner.fail(Error::bad_state);
        // Construct only after this wrapper has reached its stable Page address.
        child_.emplace(transform_(model_.view()));
        return child_->render(owner, parent);
    }
    void reset_mount() noexcept { child_.reset(); }
    template<std::uint16_t Key, class Self, class Value>
        requires std::constructible_from<T, decltype(std::declval<Self>().model_)>
    auto apply_modifier(this Self&& self, Value value) {
        if (self.child_) std::abort();
        auto previous = detail::without_modifier<Key>(std::forward<Self>(self).transform_);
        using Decorator = ComponentModifier<Key, Value, decltype(previous)>;
        return ComponentView<T, Decorator>(std::forward<Self>(self).model_,
            Decorator{std::move(previous), value});
    }
private:
    T model_;
    [[no_unique_address]] Transform transform_;
    std::optional<Child> child_;
};
template<class T> struct BorrowedComponent {
    T* model;
    auto view() { return model->view(); }
};

struct ComponentConfiguration {};
template<class T, class Transform, class... Args> class InPlaceComponentView : public ViewModifiers {
    template<class, class, class...> friend class InPlaceComponentView;
    using ModelView = decltype(std::declval<T&>().view());
    using Child = std::remove_cvref_t<std::invoke_result_t<Transform&, ModelView>>;
    static_assert(ViewLike<Child>, "Component.view() must return a UI view");
public:
    static constexpr Capacity capacity = capacity_of<Child>;
    static constexpr std::uint32_t text_node_offset = detail::text_node_offset<Child>;
    static constexpr std::uint32_t interaction_node_offset = detail::interaction_node_offset<Child>;
    static constexpr bool overlay = [] {
        if constexpr (requires { Child::overlay; }) return Child::overlay;
        else return false;
    }();
    explicit InPlaceComponentView(Args... args) : args_(std::move(args)...) {}
    InPlaceComponentView(ComponentConfiguration, Transform transform, std::tuple<Args...> args)
        : args_(std::move(args)), transform_(std::move(transform)) {}
    InPlaceComponentView(const InPlaceComponentView&) = delete;
    InPlaceComponentView(InPlaceComponentView&& other)
        : args_(std::move(other.args_)), transform_(std::move(other.transform_)) {
        if (other.model_) std::abort();
    }
    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        if (child_) return owner.fail(Error::bad_state);
        if (!model_)
            std::apply([this](auto&... args) { model_.emplace(std::move(args)...); }, args_);
        child_.emplace(transform_(model_->view()));
        return child_->render(owner, parent);
    }
    void reset_mount() noexcept { child_.reset(); }
    template<std::uint16_t Key, class Self, class Value>
        requires std::constructible_from<std::tuple<Args...>, decltype(std::declval<Self>().args_)>
    auto apply_modifier(this Self&& self, Value value) {
        if (self.model_) std::abort();
        auto previous = detail::without_modifier<Key>(std::forward<Self>(self).transform_);
        using Decorator = ComponentModifier<Key, Value, decltype(previous)>;
        return InPlaceComponentView<T, Decorator, Args...>(ComponentConfiguration{},
            Decorator{std::move(previous), value}, std::forward<Self>(self).args_);
    }
private:
    std::tuple<Args...> args_;
    [[no_unique_address]] Transform transform_;
    std::optional<T> model_;
    std::optional<Child> child_;
};
} // namespace detail

// Owned components may safely capture their own this in callbacks.
template<class T> requires requires(T& model) { model.view(); } && std::move_constructible<T>
auto Component(T&& model) {
    return detail::ComponentView<std::decay_t<T>>(std::forward<T>(model));
}
template<class T> requires requires(T& model) { model.view(); }
auto Component(T& model) {
    return detail::ComponentView(detail::BorrowedComponent<T>{&model});
}
template<class T, class... Args> requires std::constructible_from<T, std::decay_t<Args>...>
auto Component(Args&&... args) {
    return detail::InPlaceComponentView<T, detail::IdentityComponentView, std::decay_t<Args>...>(std::forward<Args>(args)...);
}
} // namespace pxa::ui

#pragma once

#include "ui_wire.hpp"

#include <charconv>
#include <concepts>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace pxa::ui {

struct Subscription {
    Subscription* next = nullptr;
    bool* dirty = nullptr;
    bool pending = false;
};

template<class T>
class State {
public:
    explicit State(T initial) : value_(std::move(initial)) {}
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    const T& get() const noexcept { return value_; }

    void set(T value) {
        if constexpr (requires(const T& a, const T& b) {
                          { a == b } -> std::convertible_to<bool>;
                      }) {
            if (value_ == value) return;
        }
        value_ = std::move(value);
        for (auto* item = subscribers_; item; item = item->next) {
            item->pending = true;
            *item->dirty = true;
        }
    }

    template<class F> void update(F&& operation) {
        set(operation(value_));
    }

    void subscribe(Subscription& item) noexcept {
        item.next = subscribers_;
        subscribers_ = &item;
    }
    void unsubscribe(Subscription& item) noexcept {
        auto** cursor = &subscribers_;
        while (*cursor && *cursor != &item) cursor = &(*cursor)->next;
        if (*cursor) *cursor = item.next;
        item.next = nullptr;
        item.dirty = nullptr;
    }
private:
    T value_;
    Subscription* subscribers_ = nullptr;
};

enum class Font : std::uint16_t {
    caption = 0, body = 1, title = 2, icon = 3,
    label = 4, headline = 5, display = 6
};

enum class Color : std::uint8_t {
    background = 0, surface = 1, primary = 2,
    on_primary = 3, text = 4, muted = 5, border = 6
};

inline bool write_int_text(Transaction<>& tx, std::uint32_t node,
                           const int& value) noexcept {
    char bytes[24];
    auto [end, error] = std::to_chars(bytes, bytes + sizeof(bytes), value);
    return error == std::errc{} &&
           tx.text(node, {bytes, static_cast<std::size_t>(end - bytes)});
}

inline bool write_int_value(Transaction<>& tx, std::uint32_t node,
                            const int& value) noexcept {
    return tx.i32(node, protocol::value, value);
}

inline bool write_bool_value(Transaction<>& tx, std::uint32_t node,
                             const bool& value) noexcept {
    return tx.i32(node, protocol::value, value ? 1 : 0);
}

inline bool write_string_text(Transaction<>& tx, std::uint32_t node,
                              const std::string& value) noexcept {
    return tx.text(node, value);
}

struct Dp { std::int32_t value; };
namespace literals {
constexpr Dp operator""_dp(unsigned long long value) noexcept {
    return {static_cast<std::int32_t>(value)};
}
} // namespace literals

class TextView {
public:
    explicit TextView(std::string text) : text_(std::move(text)) {}
    explicit TextView(State<int>& state) : state_(&state) {}
    explicit TextView(State<std::string>& state) : string_state_(&state) {}

    TextView& font(Font value) & noexcept {
        font_ = value;
        return *this;
    }
    TextView&& font(Font value) && noexcept {
        font_ = value;
        return std::move(*this);
    }
    TextView& color(Color value) & noexcept {
        color_ = value;
        return *this;
    }
    TextView&& color(Color value) && noexcept {
        color_ = value;
        return std::move(*this);
    }

    template<class Page>
    bool render(Page& page, std::uint32_t parent) {
        auto node = page.create(parent, protocol::text);
        if (!node) return false;
        if (string_state_) {
            if (!write_string_text(page.transaction(), node,
                                   string_state_->get()) ||
                !page.template bind<std::string, write_string_text>(
                    *string_state_, node)) return false;
        } else if (state_) {
            if (!write_int_text(page.transaction(), node, state_->get()) ||
                !page.template bind<int, write_int_text>(*state_, node))
                return false;
        } else if (!page.transaction().text(node, text_)) {
            return false;
        }
        return page.transaction().u16(node, protocol::font_role,
                                       static_cast<std::uint16_t>(font_)) &&
               page.theme(node, protocol::foreground, color_);
    }
private:
    std::string text_;
    State<int>* state_ = nullptr;
    State<std::string>* string_state_ = nullptr;
    Font font_ = Font::body;
    Color color_ = Color::text;
};

inline TextView Text(std::string value) { return TextView(std::move(value)); }
inline TextView Text(const char* value) { return TextView(std::string(value)); }
inline TextView Text(State<int>& state) { return TextView(state); }
inline TextView Text(State<std::string>& state) { return TextView(state); }

template<class F>
class ButtonView {
public:
    ButtonView(std::string label, F callback)
        : label_(std::move(label)), callback_(std::move(callback)) {}

    template<class Page>
    bool render(Page& page, std::uint32_t parent) {
        auto node = page.create(parent, protocol::control, protocol::button);
        if (!node || !page.transaction().u8(node, protocol::layout,
                                              protocol::row) ||
            !page.transaction().u64(node, protocol::event_mask, 1) ||
            !page.theme(node, protocol::background, Color::primary) ||
            !page.on_click(node, callback_)) return false;
        auto label_node = page.create(node, protocol::text);
        return label_node && page.transaction().text(label_node, label_) &&
               page.transaction().u16(label_node, protocol::font_role, 1) &&
               page.theme(label_node, protocol::foreground,
                          Color::on_primary);
    }
private:
    std::string label_;
    [[no_unique_address]] F callback_;
};

class ButtonBuilder {
public:
    explicit ButtonBuilder(std::string label) : label_(std::move(label)) {}
    template<class F> auto on_click(F&& callback) && {
        return ButtonView<std::decay_t<F>>(
            std::move(label_), std::forward<F>(callback));
    }
private:
    std::string label_;
};

inline ButtonBuilder Button(std::string label) {
    return ButtonBuilder(std::move(label));
}

class ToggleView {
public:
    ToggleView(std::string label, State<bool>& value)
        : label_(std::move(label)), value_(value) {}

    template<class Page> bool render(Page& page, std::uint32_t parent) {
        auto row = page.create(parent, protocol::box);
        if (!row ||
            !page.transaction().u8(row, protocol::layout, protocol::row) ||
            !page.transaction().dp(row, protocol::gap, 12) ||
            !page.transaction().u8(row, protocol::align, 1)) return false;
        auto label = page.create(row, protocol::text);
        if (!label || !page.transaction().text(label, label_)) return false;
        auto node = page.create(row, protocol::control, protocol::toggle);
        return node &&
            page.transaction().logical_px(node, protocol::width, 36) &&
            page.transaction().logical_px(node, protocol::height, 20) &&
            write_bool_value(page.transaction(), node, value_.get()) &&
            page.transaction().u64(node, protocol::event_mask, 2) &&
            page.template bind<bool, write_bool_value>(value_, node) &&
            page.on_value(node, value_);
    }
private:
    std::string label_;
    State<bool>& value_;
};

inline ToggleView Toggle(std::string label, State<bool>& value) {
    return {std::move(label), value};
}

class SliderView {
public:
    SliderView(State<int>& value, int minimum, int maximum, int step)
        : value_(value), minimum_(minimum), maximum_(maximum), step_(step) {}

    template<class Page> bool render(Page& page, std::uint32_t parent) {
        if (minimum_ > maximum_ || step_ <= 0 ||
            value_.get() < minimum_ || value_.get() > maximum_)
            return page.fail(Error::invalid_argument);
        auto node = page.create(parent, protocol::control, protocol::slider);
        return node &&
               page.transaction().fill(node, protocol::width) &&
               page.transaction().logical_px(node, protocol::height, 20) &&
               page.transaction().i32(node, protocol::min_value, minimum_) &&
               page.transaction().i32(node, protocol::max_value, maximum_) &&
               page.transaction().i32(node, protocol::step, step_) &&
               write_int_value(page.transaction(), node, value_.get()) &&
               page.transaction().u64(node, protocol::event_mask, 2) &&
               page.template bind<int, write_int_value>(value_, node) &&
               page.on_value(node, value_);
    }
private:
    State<int>& value_;
    int minimum_;
    int maximum_;
    int step_;
};

inline SliderView Slider(State<int>& value, int minimum,
                         int maximum, int step = 1) {
    return {value, minimum, maximum, step};
}

class ProgressView {
public:
    ProgressView(State<int>& value, int minimum, int maximum)
        : value_(value), minimum_(minimum), maximum_(maximum) {}

    template<class Page> bool render(Page& page, std::uint32_t parent) {
        if (minimum_ > maximum_ ||
            value_.get() < minimum_ || value_.get() > maximum_)
            return page.fail(Error::invalid_argument);
        auto node = page.create(parent, protocol::progress);
        return node &&
               page.transaction().fill(node, protocol::width) &&
               page.transaction().logical_px(node, protocol::height, 8) &&
               page.theme(node, protocol::background, Color::surface) &&
               page.transaction().i32(node, protocol::min_value, minimum_) &&
               page.transaction().i32(node, protocol::max_value, maximum_) &&
               write_int_value(page.transaction(), node, value_.get()) &&
               page.template bind<int, write_int_value>(value_, node);
    }
private:
    State<int>& value_;
    int minimum_;
    int maximum_;
};

inline ProgressView Progress(State<int>& value, int minimum = 0,
                             int maximum = 100) {
    return {value, minimum, maximum};
}

class TextInputView {
public:
    explicit TextInputView(State<std::string>& value) : value_(value) {}

    template<class Page> bool render(Page& page, std::uint32_t parent) {
        auto node = page.create(parent, protocol::control,
                                protocol::text_input);
        return node &&
               page.transaction().fill(node, protocol::width) &&
               page.transaction().logical_px(node, protocol::height, 32) &&
               write_string_text(page.transaction(), node, value_.get()) &&
               page.transaction().u64(node, protocol::event_mask, 1u << 5) &&
               page.template bind<std::string, write_string_text>(value_, node) &&
               page.on_text(node, value_);
    }
private:
    State<std::string>& value_;
};

inline TextInputView TextInput(State<std::string>& value) {
    return TextInputView(value);
}

enum class ImageFit : std::uint8_t { contain, stretch, cover };

class ImageView {
public:
    explicit ImageView(std::string asset) : asset_(std::move(asset)) {}
    ImageView& fit(ImageFit value) & noexcept { fit_ = value; return *this; }
    ImageView&& fit(ImageFit value) && noexcept {
        fit_ = value;
        return std::move(*this);
    }

    template<class Page> bool render(Page& page, std::uint32_t parent) {
        if (asset_.empty()) return page.fail(Error::invalid_argument);
        auto node = page.create(parent, protocol::image);
        return node && page.transaction().property(
                           node, protocol::asset,
                           {reinterpret_cast<const std::byte*>(asset_.data()),
                            asset_.size()}) &&
               page.transaction().u8(node, protocol::image_fit,
                                     static_cast<std::uint8_t>(fit_));
    }
private:
    std::string asset_;
    ImageFit fit_ = ImageFit::contain;
};

inline ImageView Image(std::string asset) {
    return ImageView(std::move(asset));
}

template<class... Children>
class BoxView {
public:
    BoxView(std::uint8_t layout, Children... children)
        : layout_(layout), children_(std::move(children)...) {}

    BoxView& gap(Dp value) & noexcept { gap_ = value.value; return *this; }
    BoxView&& gap(Dp value) && noexcept {
        gap_ = value.value;
        return std::move(*this);
    }
    BoxView& padding(Dp value) & noexcept {
        padding_ = value.value;
        return *this;
    }
    BoxView&& padding(Dp value) && noexcept {
        padding_ = value.value;
        return std::move(*this);
    }
    BoxView& fill() & noexcept { fill_ = true; return *this; }
    BoxView&& fill() && noexcept {
        fill_ = true;
        return std::move(*this);
    }

    template<class Page>
    bool render(Page& page, std::uint32_t parent) {
        auto node = page.create(parent, protocol::box);
        if (!node || !page.transaction().u8(node, protocol::layout, layout_))
            return false;
        if (fill_ && !page.transaction().fill(node, protocol::width))
            return false;
        if (gap_ && !page.transaction().dp(node, protocol::gap, gap_))
            return false;
        if (padding_ && !page.padding(node, padding_)) return false;
        return std::apply([&](auto&... child) {
            return (child.render(page, node) && ...);
        }, children_);
    }
private:
    std::uint8_t layout_;
    std::tuple<Children...> children_;
    std::int32_t gap_ = 0;
    std::int32_t padding_ = 0;
    bool fill_ = false;
};

template<class... Children>
auto Column(Children&&... children) {
    return BoxView<std::decay_t<Children>...>(
        protocol::column, std::forward<Children>(children)...);
}

template<class... Children>
auto Row(Children&&... children) {
    return BoxView<std::decay_t<Children>...>(
        protocol::row, std::forward<Children>(children)...);
}

template<class... Children>
auto Stack(Children&&... children) {
    return BoxView<std::decay_t<Children>...>(
        protocol::stack, std::forward<Children>(children)...);
}

template<class... Children>
class ScrollView {
public:
    explicit ScrollView(Children... children)
        : children_(std::move(children)...) {}
    template<class Page> bool render(Page& page, std::uint32_t parent) {
        auto node = page.create(parent, 3);
        if (!node ||
            !page.transaction().fill(node, protocol::width) ||
            !page.transaction().fill(node, protocol::height) ||
            !page.transaction().u8(node, protocol::scroll_axis, 1))
            return false;
        return std::apply([&](auto&... child) {
            return (child.render(page, node) && ...);
        }, children_);
    }
private:
    std::tuple<Children...> children_;
};

template<class... Children>
auto Scroll(Children&&... children) {
    return ScrollView<std::decay_t<Children>...>(
        std::forward<Children>(children)...);
}

template<class View, std::size_t MaxBindings = 32,
         std::size_t MaxHandlers = 32>
class Page {
public:
    Page(Transport& transport, View view)
        : transport_(transport), view_(std::move(view)) {}
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
    ~Page() {
        for (std::size_t i = 0; i < binding_count_; ++i)
            bindings_[i].unsubscribe(bindings_[i].state,
                                     bindings_[i].subscription);
    }

    Result<void> mount() noexcept {
        if (mounted_) return std::unexpected(Error::bad_state);
        const auto generation = transport_.next_ui_generation();
        if (!generation)
            return std::unexpected(Error::limit_exceeded);
        Transaction tx(transport_, generation, 1,
                       protocol::replace_surface);
        if (!tx.valid()) return std::unexpected(tx.error());
        current_ = &tx;
        mount_error_.reset();
        next_id_ = 1;
        binding_count_ = 0;
        handler_count_ = 0;
        auto root = create(0, protocol::root);
        const bool rendered = root &&
            tx.u8(root, protocol::layout, protocol::column) &&
            view_.render(*this, root);
        current_ = nullptr;
        if (!rendered)
            return std::unexpected(
                mount_error_.value_or(tx.error()));
        auto result = tx.commit();
        if (!result) return result;
        generation_ = generation;
        mounted_ = true;
        for (std::size_t i = 0; i < binding_count_; ++i)
            bindings_[i].subscribe(bindings_[i].state,
                                   bindings_[i].subscription);
        return {};
    }

    Result<void> flush() noexcept {
        if (!dirty_) return {};
        if (!mounted_)
            return std::unexpected(Error::bad_state);
        const auto generation = transport_.next_ui_generation();
        if (!generation) return std::unexpected(Error::limit_exceeded);
        Transaction tx(transport_, generation, 1, protocol::patch);
        if (!tx.valid()) return std::unexpected(tx.error());
        for (std::size_t i = 0; i < binding_count_; ++i) {
            auto& binding = bindings_[i];
            if (!binding.subscription.pending) continue;
            if (!binding.write(tx, binding.node, binding.state))
                return std::unexpected(tx.error());
        }
        auto result = tx.commit();
        if (!result) return result;
        generation_ = generation;
        dirty_ = false;
        for (std::size_t i = 0; i < binding_count_; ++i)
            bindings_[i].subscription.pending = false;
        return {};
    }

    bool handle(const Event& event) noexcept {
        if (!mounted_ || event.service != protocol::service ||
            event.opcode != 0x8001 || event.token != 0 ||
            event.payload.size() < 24 ||
            wire::get32(event.payload.data()) != 1 ||
            wire::get32(event.payload.data() + 8) != generation_ ||
            wire::get16(event.payload.data() + 12) == 0)
            return false;
        auto node = wire::get32(event.payload.data() + 4);
        auto kind = wire::get16(event.payload.data() + 12);
        for (std::size_t i = 0; i < handler_count_; ++i) {
            auto& handler = handlers_[i];
            if (handler.node == node && handler.kind == kind)
                return handler.call(handler.callback,
                                    event.payload.subspan(24));
        }
        return false;
    }

    std::uint32_t generation() const noexcept { return generation_; }
    bool dirty() const noexcept { return dirty_; }
    bool fail(Error error) noexcept {
        mount_error_ = error;
        return false;
    }
    Transaction<>& transaction() noexcept { return *current_; }

    std::uint32_t create(std::uint32_t parent, std::uint8_t type,
                         std::uint8_t subtype = 0) noexcept {
        if (!current_ || next_id_ == 0) return 0;
        const auto id = next_id_++;
        return current_->create(id, parent, type, subtype) ? id : 0;
    }

    template<class T,
             bool (*Write)(Transaction<>&, std::uint32_t, const T&)>
    bool bind(State<T>& state, std::uint32_t node) noexcept {
        if (binding_count_ == MaxBindings) {
            mount_error_ = Error::resource_limit;
            return false;
        }
        auto& binding = bindings_[binding_count_++];
        binding.state = &state;
        binding.node = node;
        binding.subscription.dirty = &dirty_;
        binding.subscribe = [](void* pointer, Subscription& subscription) {
            static_cast<State<T>*>(pointer)->subscribe(subscription);
        };
        binding.unsubscribe = [](void* pointer, Subscription& subscription) {
            static_cast<State<T>*>(pointer)->unsubscribe(subscription);
        };
        binding.write = [](Transaction<>& tx, std::uint32_t id,
                           void* pointer) {
            return Write(tx, id, static_cast<State<T>*>(pointer)->get());
        };
        return true;
    }

    template<class F> bool on_click(std::uint32_t node, F& callback) noexcept {
        if (handler_count_ == MaxHandlers) {
            mount_error_ = Error::resource_limit;
            return false;
        }
        handlers_[handler_count_++] = {
            node, 1, &callback,
            [](void* value, std::span<const std::byte>) {
                (*static_cast<F*>(value))();
                return true;
            }};
        return true;
    }

    template<class T> bool on_value(std::uint32_t node,
                                    State<T>& state) noexcept {
        static_assert(std::same_as<T, bool> || std::same_as<T, int>);
        if (handler_count_ == MaxHandlers) {
            mount_error_ = Error::resource_limit;
            return false;
        }
        handlers_[handler_count_++] = {
            node, 2, &state,
            [](void* pointer, std::span<const std::byte> data) {
                if (data.size() != 4) return false;
                auto value = static_cast<std::int32_t>(
                    wire::get32(data.data()));
                if constexpr (std::same_as<T, bool>) {
                    if (value < 0 || value > 1) return false;
                    static_cast<State<bool>*>(pointer)->set(value != 0);
                } else {
                    static_cast<State<int>*>(pointer)->set(value);
                }
                return true;
            }};
        return true;
    }

    bool on_text(std::uint32_t node, State<std::string>& state) noexcept {
        if (handler_count_ == MaxHandlers) {
            mount_error_ = Error::resource_limit;
            return false;
        }
        handlers_[handler_count_++] = {
            node, 6, &state,
            [](void* pointer, std::span<const std::byte> data) {
                if (data.size() > 64) return false;
                for (auto byte : data)
                    if (byte == std::byte{}) return false;
                static_cast<State<std::string>*>(pointer)->set(
                    {reinterpret_cast<const char*>(data.data()),
                     data.size()});
                return true;
            }};
        return true;
    }

    bool theme(std::uint32_t node, std::uint16_t property,
               Color value) noexcept {
        std::array<std::byte, 8> bytes{};
        bytes[1] = std::byte(static_cast<std::uint8_t>(value));
        return current_->property(node, property, bytes);
    }

    bool padding(std::uint32_t node, std::int32_t value) noexcept {
        if (value < 0 || value > INT32_MAX / 64) return false;
        std::array<std::byte, 16> bytes{};
        for (std::size_t i = 0; i < 4; ++i)
            wire::put32(bytes.data() + i * 4,
                        static_cast<std::uint32_t>(value * 64));
        return current_->property(node, 268, bytes);
    }

private:
    struct Binding {
        void* state = nullptr;
        std::uint32_t node = 0;
        Subscription subscription;
        void (*subscribe)(void*, Subscription&) = nullptr;
        void (*unsubscribe)(void*, Subscription&) = nullptr;
        bool (*write)(Transaction<>&, std::uint32_t, void*) = nullptr;
    };
    struct Handler {
        std::uint32_t node = 0;
        std::uint16_t kind = 0;
        void* callback = nullptr;
        bool (*call)(void*, std::span<const std::byte>) = nullptr;
    };

    Transport& transport_;
    View view_;
    std::array<Binding, MaxBindings> bindings_{};
    std::array<Handler, MaxHandlers> handlers_{};
    Transaction<>* current_ = nullptr;
    std::uint32_t generation_ = 0;
    std::uint32_t next_id_ = 1;
    std::size_t binding_count_ = 0;
    std::size_t handler_count_ = 0;
    bool mounted_ = false;
    bool dirty_ = false;
    std::optional<Error> mount_error_;
};

template<class View>
Page(Transport&, View) -> Page<View>;

} // namespace pxa::ui

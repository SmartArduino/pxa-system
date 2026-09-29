#pragma once

#include "ui.hpp"
#include "task.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <tuple>

namespace pxa::ui {

template<std::size_t MaxDepth = 8, std::size_t PageBytes = 4096,
         std::size_t RouteBytes = 96>
class Navigator {
    static_assert(MaxDepth > 0 && PageBytes > 0 && RouteBytes > 0);
    template<class Screen>
    struct Mounted {
        using View = decltype(std::declval<Screen&>().view());
        Screen screen;
        TaskScope tasks;
        Page<View> page;
        template<class Arguments>
        Mounted(const Arguments& arguments, Transport& transport)
            : screen(std::make_from_tuple<Screen>(arguments)),
              page(transport, screen.view()) {}
        void activated() noexcept {
            if constexpr (requires(Screen& value, TaskScope& scope) {
                              value.on_mount(scope);
                          }) screen.on_mount(tasks);
        }
        ~Mounted() {
            tasks.cancel();
        }
    };

    struct PageOps {
        void (*destroy)(void*) noexcept;
        bool (*handle)(void*, const Event&) noexcept;
        Result<void> (*flush)(void*) noexcept;
        void (*activate)(void*) noexcept;
        std::uint32_t (*generation)(void*) noexcept;
        TaskScope& (*tasks)(void*) noexcept;
    };
    struct RouteOps {
        void (*destroy)(void*) noexcept;
        void (*copy)(void*, const void*);
        Result<const PageOps*> (*mount)(const void*, void*, Transport&);
    };
    struct Route {
        alignas(std::max_align_t) std::byte bytes[RouteBytes];
        const RouteOps* ops = nullptr;
        void clear() noexcept {
            if (ops) ops->destroy(bytes);
            ops = nullptr;
        }
    };
    struct Slot {
        alignas(std::max_align_t) std::byte bytes[PageBytes];
        const PageOps* ops = nullptr;
        void clear() noexcept {
            if (ops) ops->destroy(bytes);
            ops = nullptr;
        }
    };
    enum class Action { none, push, replace, pop };

public:
    Navigator() = default;
    Navigator(const Navigator&) = delete;
    Navigator& operator=(const Navigator&) = delete;
    ~Navigator() { reset(); }

    Result<void> attach(Transport& transport) noexcept {
        if (transport_ && transport_ != &transport)
            return std::unexpected(Error::bad_state);
        transport_ = &transport;
        return flush();
    }

    template<class Screen, class... Args>
    Result<void> push(Args&&... args) {
        return schedule<Screen>(Action::push, std::forward<Args>(args)...);
    }

    template<class Screen, class... Args>
    Result<void> replace(Args&&... args) {
        return schedule<Screen>(Action::replace, std::forward<Args>(args)...);
    }

    Result<void> pop() noexcept {
        if (pending_ != Action::none) return std::unexpected(Error::busy);
        if (depth_ < 2) return std::unexpected(Error::not_found);
        pending_ = Action::pop;
        return {};
    }

    bool can_pop() const noexcept { return depth_ > 1; }
    std::size_t depth() const noexcept { return depth_; }
    std::uint32_t generation() const noexcept {
        const auto& slot = slots_[active_];
        return slot.ops ? slot.ops->generation(const_cast<std::byte*>(slot.bytes)) : 0;
    }
    Result<std::reference_wrapper<TaskScope>> tasks() noexcept {
        auto& slot = slots_[active_];
        if (!slot.ops) return std::unexpected(Error::bad_state);
        return std::ref(slot.ops->tasks(slot.bytes));
    }

    bool handle(const Event& event) noexcept {
        auto& slot = slots_[active_];
        if (!slot.ops || handling_) return false;
        handling_ = true;
        const auto result = slot.ops->handle(slot.bytes, event);
        handling_ = false;
        return result;
    }

    Result<void> flush() noexcept {
        if (!transport_ || handling_) return std::unexpected(Error::bad_state);
        if (pending_ != Action::none) {
            const auto action = pending_;
            auto& route = action == Action::pop ? history_[depth_ - 2] : pending_route_;
            const auto next = 1u - active_;
            auto& candidate = slots_[next];
            auto mounted = route.ops->mount(route.bytes, candidate.bytes, *transport_);
            if (!mounted) {
                pending_route_.clear();
                pending_ = Action::none;
                return std::unexpected(mounted.error());
            }
            candidate.ops = *mounted;
            slots_[active_].clear();
            active_ = next;
            if (action == Action::pop) {
                history_[--depth_].clear();
            } else {
                if (action == Action::replace && depth_) history_[--depth_].clear();
                auto& saved = history_[depth_++];
                route.ops->copy(saved.bytes, route.bytes);
                saved.ops = route.ops;
                pending_route_.clear();
            }
            pending_ = Action::none;
            candidate.ops->activate(candidate.bytes);
        }
        auto& slot = slots_[active_];
        if (!slot.ops) return {};
        slot.ops->tasks(slot.bytes).reap();
        return slot.ops->flush(slot.bytes);
    }

    void reset() noexcept {
        slots_[0].clear();
        slots_[1].clear();
        pending_route_.clear();
        for (auto& route : history_) route.clear();
        depth_ = 0;
        active_ = 0;
        pending_ = Action::none;
        transport_ = nullptr;
        handling_ = false;
    }

private:
    template<class Screen, class... Args>
    Result<void> schedule(Action action, Args&&... args) {
        using Arguments = std::tuple<std::decay_t<Args>...>;
        using PageType = Mounted<Screen>;
        static_assert(std::copy_constructible<Arguments>,
                      "Navigation arguments must be copyable; pass models with std::ref");
        static_assert((std::is_trivially_copy_constructible_v<std::decay_t<Args>> && ...),
                      "Navigation arguments must be plain values or std::ref models");
        static_assert(sizeof(Arguments) <= RouteBytes &&
                      alignof(Arguments) <= alignof(std::max_align_t),
                      "Navigation route arguments exceed the configured budget");
        static_assert(sizeof(PageType) <= PageBytes &&
                      alignof(PageType) <= alignof(std::max_align_t),
                      "Navigation page exceeds the configured budget");
        if (pending_ != Action::none) return std::unexpected(Error::busy);
        if (action == Action::push && depth_ == MaxDepth)
            return std::unexpected(Error::resource_limit);
        static constexpr PageOps page_ops{
            [](void* p) noexcept { std::destroy_at(static_cast<PageType*>(p)); },
            [](void* p, const Event& e) noexcept {
                return static_cast<PageType*>(p)->page.handle(e);
            },
            [](void* p) noexcept { return static_cast<PageType*>(p)->page.flush(); },
            [](void* p) noexcept { static_cast<PageType*>(p)->activated(); },
            [](void* p) noexcept { return static_cast<PageType*>(p)->page.generation(); },
            [](void* p) noexcept -> TaskScope& { return static_cast<PageType*>(p)->tasks; }
        };
        static constexpr RouteOps route_ops{
            [](void* p) noexcept { std::destroy_at(static_cast<Arguments*>(p)); },
            [](void* target, const void* source) {
                std::construct_at(static_cast<Arguments*>(target),
                                  *static_cast<const Arguments*>(source));
            },
            [](const void* route, void* target,
               Transport& transport) -> Result<const PageOps*> {
                auto* page = std::construct_at(static_cast<PageType*>(target),
                    *static_cast<const Arguments*>(route), transport);
                auto mounted = page->page.mount();
                if (!mounted) {
                    std::destroy_at(page);
                    return std::unexpected(mounted.error());
                }
                return &page_ops;
            }
        };
        std::construct_at(reinterpret_cast<Arguments*>(pending_route_.bytes),
                          std::forward<Args>(args)...);
        pending_route_.ops = &route_ops;
        pending_ = action;
        return {};
    }

    std::array<Route, MaxDepth> history_;
    Route pending_route_;
    std::array<Slot, 2> slots_;
    Transport* transport_ = nullptr;
    std::size_t depth_ = 0;
    unsigned active_ = 0;
    Action pending_ = Action::none;
    bool handling_ = false;
};

} // namespace pxa::ui

#pragma once
#include "task.hpp"
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace pxa::ui {
namespace detail {
template<class T> concept ActionResult = std::same_as<T, void> ||
    std::same_as<T, Result<void>> || std::same_as<T, Task<void>>;

template<class Function> class ActionCallback {
public:
    ActionCallback(TaskScope& scope, Function function)
        : scope_(&scope), function_(std::move(function)) {}
    template<class... Args> requires std::invocable<Function&, Args...> &&
        ActionResult<std::invoke_result_t<Function&, Args...>>
    void operator()(Args&&... args) {
        using Output = std::invoke_result_t<Function&, Args...>;
        if constexpr (std::same_as<Output, void>)
            std::invoke(function_, std::forward<Args>(args)...);
        else if constexpr (std::same_as<Output, Task<void>>) {
            auto result = scope_->start(std::invoke(function_, std::forward<Args>(args)...));
            if (!result) scope_->report(result.error());
        } else {
            auto result = std::invoke(function_, std::forward<Args>(args)...);
            if (!result) scope_->report(result.error());
        }
    }
private:
    TaskScope* scope_;
    [[no_unique_address]] Function function_;
};
} // namespace detail

// The closure is owned, the scope is borrowed. Choose application, foreground
// or page scope explicitly; Result errors and task-start failures use its hook.
template<class Function> auto Action(TaskScope& scope, Function&& function) {
    return detail::ActionCallback<std::decay_t<Function>>(
        scope, std::forward<Function>(function));
}
} // namespace pxa::ui

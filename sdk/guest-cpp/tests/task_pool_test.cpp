#include <pxa/task.hpp>

#include <array>
#include <cassert>
#include <optional>

pxa::Task<int> suspended() {
    co_await std::suspend_always{};
    co_return 42;
}

pxa::Task<std::array<std::byte, PXA_COROUTINE_SLOT_BYTES + 16>> oversized() {
    co_return std::array<std::byte, PXA_COROUTINE_SLOT_BYTES + 16>{};
}

int main() {
    const auto initial = pxa::task_pool_stats();
    assert(initial.active_slots == 0 && initial.peak_slots == 0);
    assert(initial.slot_count == PXA_COROUTINE_SLOT_COUNT);
    assert(initial.slot_bytes == PXA_COROUTINE_SLOT_BYTES);
    // Compare the reserved metadata against the old per-slot used flag/padding.
    struct alignas(std::max_align_t) OldSlot {
        std::byte bytes[PXA_COROUTINE_SLOT_BYTES];
        bool used;
    };
    if constexpr (PXA_COROUTINE_SLOT_COUNT >= 3 &&
                  PXA_COROUTINE_SLOT_BYTES % alignof(std::max_align_t) == 0)
        assert(initial.reserved_bytes <= sizeof(OldSlot) * initial.slot_count);

    std::array<std::optional<pxa::Task<int>>, PXA_COROUTINE_SLOT_COUNT> tasks;
    for (auto& task : tasks) {
        task.emplace(suspended());
        assert(task->valid());
    }
    auto full = pxa::task_pool_stats();
    assert(full.active_slots == tasks.size() && full.peak_slots == tasks.size());
    auto rejected = suspended();
    assert(!rejected.valid() && rejected.failure() == pxa::Error::resource_limit);
    assert(pxa::task_pool_stats().allocation_failures == 1);

    // Free alternating slots across bitmap-word boundaries, then reuse all of
    // them repeatedly. Live tasks must keep distinct frames throughout.
    for (unsigned repeat = 0; repeat < 128; ++repeat) {
        for (std::size_t i = 0; i < tasks.size(); i += 2) tasks[i].reset();
        assert(pxa::task_pool_stats().active_slots == tasks.size() / 2);
        for (std::size_t i = 0; i < tasks.size(); i += 2) {
            tasks[i].emplace(suspended());
            assert(tasks[i]->valid());
        }
        assert(pxa::task_pool_stats().active_slots == tasks.size());
    }
    for (auto& task : tasks) task.reset();
    assert(pxa::task_pool_stats().active_slots == 0);
    auto large = oversized();
    assert(!large.valid() && large.failure() == pxa::Error::resource_limit);
    assert(pxa::task_pool_stats().allocation_failures == 2);

    pxa::TaskScope scope;
    for (std::size_t i = 0; i < initial.slot_count &&
                              i < PXA_TASK_SCOPE_CAPACITY; ++i)
        assert(scope.start(suspended()));
    scope.cancel();
    assert(pxa::task_pool_stats().active_slots == 0);
    assert(pxa::task_pool_stats().peak_slots == initial.slot_count);
}

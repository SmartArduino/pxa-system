#pragma once

#include "ui.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>

namespace pxa::ui {

class ListState {
public:
    explicit ListState(std::uint32_t count = 0) : count_(count) {}
    std::uint32_t size() const noexcept { return count_; }
    void set_count(std::uint32_t count) {
        if (count == count_) return;
        count_ = count;
        invalidate();
    }
    void invalidate() { revision_.set(revision_.get() + 1); }
    std::uint64_t revision() const noexcept { return revision_.get(); }
    void subscribe(Subscription& item) noexcept { revision_.subscribe(item); }
    void unsubscribe(Subscription& item) noexcept { revision_.unsubscribe(item); }
private:
    std::uint32_t count_;
    State<std::uint64_t> revision_{0};
};

template<bool Virtual, std::size_t MaxRows, std::size_t RowBindings,
         std::size_t RowHandlers, class KeyFunction, class RowFunction>
class ListView {
    using Key = std::remove_cvref_t<std::invoke_result_t<KeyFunction&, std::uint32_t>>;
    using Row = std::invoke_result_t<RowFunction&, Key>;
    using RowPage = Page<Row,
        RowBindings == std::dynamic_extent ? capacity_of<Row>.bindings : RowBindings,
        RowHandlers == std::dynamic_extent ? capacity_of<Row>.handlers : RowHandlers, 0>;
    static_assert(MaxRows > 0 && std::is_trivially_copyable_v<Key> && sizeof(Key) <= 16,
                  "List keys must be small values; row capacity must be nonzero");
    struct Slot {
        alignas(RowPage) std::byte storage[sizeof(RowPage)];
        Key key{};
        std::uint32_t root = 0;
        std::uint32_t end = 0;
        std::uint32_t index = 0;
        bool used = false;
        bool committed = false;
        RowPage& page() noexcept {
            return *std::launder(reinterpret_cast<RowPage*>(storage));
        }
        void clear() noexcept {
            if (used) std::destroy_at(&page());
            used = committed = false;
        }
    };
public:
    static constexpr Capacity capacity{
        1 + 2 * MaxRows * (1 + capacity_of<Row>.nodes), 0, 0, 1};
    ListView(ListState& state, Dp extent, KeyFunction keys, RowFunction rows)
        : state_(state), keys_(std::move(keys)), rows_(std::move(rows)), extent_(extent) {}
    ListView(const ListView&) = delete;
    ListView& operator=(const ListView&) = delete;
    ListView(ListView&& other) noexcept
        : state_(other.state_), keys_(std::move(other.keys_)),
          rows_(std::move(other.rows_)), extent_(other.extent_) {
        grow_ = other.grow_;
        if (other.node_) std::abort();
    }
    ~ListView() {
        if (attached_) state_.unsubscribe(subscription_);
        for (auto& slot : slots_) slot.clear();
    }

    template<class Self> decltype(auto) grow(this Self&& self,
                                             std::uint16_t value = 1) noexcept {
        self.grow_ = value;
        return std::forward<Self>(self);
    }

    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        if (node_ || (Virtual && (extent_.value <= 0 || extent_.value > INT32_MAX / 64)))
            return owner.fail(Error::invalid_argument);
        node_ = owner.create(parent, Virtual ? protocol::virtual_list : protocol::box);
        if (!node_ || !owner.dynamic(*this)) return false;
        auto& tx = owner.transaction();
        if (!tx.fill(node_, protocol::width)) return false;
        if constexpr (Virtual) {
            const auto height = grow_ ?
                tx.logical_px(node_, protocol::height, 0) &&
                    tx.u16(node_, protocol::grow, grow_) : tx.fill(node_, protocol::height);
            return height &&
                tx.u8(node_, protocol::scroll_axis, 2) &&
                tx.u64(node_, protocol::event_mask, std::uint64_t{1} << 8) &&
                tx.dp(node_, protocol::item_extent, extent_.value);
        } else {
            return tx.u8(node_, protocol::layout, protocol::column);
        }
    }

    bool dirty() const noexcept {
        if (dirty_word_ || range_dirty_) return true;
        for (std::size_t i = 0; i < active_count_; ++i)
            if (active_[i]->page().dirty()) return true;
        return false;
    }

    Result<void> prepare(Transaction<>& tx, std::uint32_t& ids,
                          Transport& transport) {
        if (!dirty()) return {};
        prepared_ = true;
        prepared_revision_ = state_.revision();
        const bool structure = dirty_word_ || range_dirty_;
        prepared_structure_ = structure;
        pending_count_ = active_count_;
        for (std::size_t i = 0; i < active_count_; ++i) pending_[i] = active_[i];
        if (structure) {
            const auto first = Virtual ? std::min(first_, state_.size()) : 0u;
            const auto count = Virtual ? std::min(count_, state_.size() - first) : state_.size();
            if (count > MaxRows) return std::unexpected(Error::resource_limit);
            if constexpr (Virtual) {
                if (std::uint64_t(state_.size()) * extent_.value > INT32_MAX / 64)
                    return std::unexpected(Error::limit_exceeded);
                if (!tx.u32(node_, protocol::item_count, state_.size()))
                    return std::unexpected(tx.error());
            }
            pending_count_ = count;
            for (std::size_t i = 0; i < count; ++i) {
                pending_keys_[i] = std::invoke(keys_, first + static_cast<std::uint32_t>(i));
                for (std::size_t j = 0; j < i; ++j)
                    if (pending_keys_[j] == pending_keys_[i])
                        return std::unexpected(Error::invalid_argument);
            }
            for (std::size_t i = 0; i < count; ++i) {
                Slot* row = nullptr;
                for (std::size_t j = 0; j < active_count_; ++j)
                    if (active_[j]->key == pending_keys_[i]) { row = active_[j]; break; }
                if (!row) {
                    for (auto& slot : slots_)
                        if (!slot.used) { row = &slot; break; }
                    if (!row) return std::unexpected(Error::resource_limit);
                    if (!ids) return std::unexpected(Error::limit_exceeded);
                    row->root = ids++;
                    row->key = pending_keys_[i];
                    std::construct_at(reinterpret_cast<RowPage*>(row->storage),
                        transport, std::invoke(rows_, row->key));
                    row->used = true;
                    if (!tx.create(row->root, node_, protocol::box) ||
                        !tx.fill(row->root, protocol::width))
                        return std::unexpected(tx.error());
                    auto built = row->page().build_fragment(tx, row->root, ids);
                    if (!built) return built;
                    row->end = ids - 1;
                }
                pending_[i] = row;
                pending_indices_[i] = first + static_cast<std::uint32_t>(i);
                if constexpr (Virtual) {
                    if (!row->committed || row->index != pending_indices_[i]) {
                        if (!tx.u8(row->root, protocol::position, 1) ||
                            !tx.logical_px(row->root, protocol::x, 0) ||
                            !tx.logical_px(row->root, protocol::y,
                                static_cast<std::int32_t>(pending_indices_[i] *
                                                          std::uint64_t(extent_.value))) ||
                            !tx.logical_px(row->root, protocol::height, extent_.value))
                            return std::unexpected(tx.error());
                    }
                }
            }
            for (std::size_t i = 0; i < active_count_; ++i)
                if (!kept(active_[i]) && !tx.remove(active_[i]->root))
                    return std::unexpected(tx.error());
            if constexpr (!Virtual) {
                for (std::size_t i = 0; i < pending_count_; ++i)
                    if (!tx.move(pending_[i]->root, node_))
                        return std::unexpected(tx.error());
            }
        }
        for (std::size_t i = 0; i < pending_count_; ++i) {
            auto result = pending_[i]->page().write_fragment(tx);
            if (!result) return result;
        }
        return {};
    }

    void commit(std::uint32_t generation) noexcept {
        if (!prepared_) return;
        for (std::size_t i = 0; i < active_count_; ++i)
            if (!kept(active_[i])) active_[i]->clear();
        for (std::size_t i = 0; i < pending_count_; ++i) {
            active_[i] = pending_[i];
            active_[i]->committed = true;
            if (prepared_structure_) active_[i]->index = pending_indices_[i];
            active_[i]->page().activate_fragment(generation);
        }
        active_count_ = pending_count_;
        if (!attached_) {
            subscription_.dirty_word = &dirty_word_;
            subscription_.mask = 1;
            state_.subscribe(subscription_);
            attached_ = true;
        }
        dirty_word_ = state_.revision() == prepared_revision_ ? 0 : 1;
        range_dirty_ = false;
        prepared_ = false;
    }

    void rollback() noexcept {
        for (auto& slot : slots_)
            if (slot.used && !slot.committed) slot.clear();
        prepared_ = false;
        pending_count_ = 0;
    }

    bool handle(const Event& event) noexcept {
        const auto node = wire::get32(event.payload.data() + 4);
        const auto kind = wire::get16(event.payload.data() + 12);
        if constexpr (Virtual) {
            if (node == node_ && kind == 9) {
                if (event.payload.size() != 32) return false;
                const auto first = wire::get32(event.payload.data() + 24);
                const auto count = wire::get32(event.payload.data() + 28);
                if (first > state_.size() || count > state_.size() - first) return false;
                if (first != first_ || count != count_) {
                    first_ = first;
                    count_ = count;
                    range_dirty_ = true;
                }
                return true;
            }
        }
        for (std::size_t i = 0; i < active_count_; ++i) {
            auto& row = *active_[i];
            if (node >= row.root && node <= row.end)
                return row.page().handle_fragment(event);
        }
        return false;
    }
private:
    bool kept(const Slot* slot) const noexcept {
        for (std::size_t i = 0; i < pending_count_; ++i)
            if (pending_[i] == slot) return true;
        return false;
    }

    ListState& state_;
    [[no_unique_address]] KeyFunction keys_;
    [[no_unique_address]] RowFunction rows_;
    Dp extent_;
    std::array<Slot, MaxRows * 2> slots_;
    std::array<Slot*, MaxRows> active_{};
    std::array<Slot*, MaxRows> pending_{};
    std::array<Key, MaxRows> pending_keys_{};
    std::array<std::uint32_t, MaxRows> pending_indices_{};
    Subscription subscription_;
    std::uint64_t dirty_word_ = 1;
    std::uint64_t prepared_revision_ = 0;
    std::uint32_t node_ = 0;
    std::uint32_t first_ = 0;
    std::uint32_t count_ = 0;
    std::uint16_t grow_ = 0;
    std::size_t active_count_ = 0;
    std::size_t pending_count_ = 0;
    bool attached_ = false;
    bool range_dirty_ = false;
    bool prepared_ = false;
    bool prepared_structure_ = false;
};

template<std::size_t MaxRows, std::size_t RowBindings = std::dynamic_extent,
         std::size_t RowHandlers = std::dynamic_extent, class Keys, class Rows>
auto KeyedList(ListState& state, Keys&& keys, Rows&& rows) {
    return ListView<false, MaxRows, RowBindings, RowHandlers,
                    std::decay_t<Keys>, std::decay_t<Rows>>(
        state, Dp{0}, std::forward<Keys>(keys), std::forward<Rows>(rows));
}

template<std::size_t MaxRows, std::size_t RowBindings = std::dynamic_extent,
         std::size_t RowHandlers = std::dynamic_extent, class Keys, class Rows>
auto VirtualList(ListState& state, Dp item_extent, Keys&& keys, Rows&& rows) {
    return ListView<true, MaxRows, RowBindings, RowHandlers,
                    std::decay_t<Keys>, std::decay_t<Rows>>(
        state, item_extent, std::forward<Keys>(keys), std::forward<Rows>(rows));
}

} // namespace pxa::ui

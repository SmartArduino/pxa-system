#pragma once
#include "ui.hpp"

namespace pxa::ui {

// Optional bounded subtree replacement. Two inline Pages preserve callbacks
// and the live tree until commit succeeds; failed builds are safely discarded.
template<class Factory> class RefreshView : public ViewModifiers {
    using View = std::invoke_result_t<Factory&>;
    using Fragment = Page<View>;
public:
    static constexpr Capacity capacity{1 + 2 * capacity_of<View>.nodes, 0, 0, 1};
    RefreshView(State<std::uint32_t>& revision, Factory factory)
        : revision_(revision), factory_(std::move(factory)) {}
    RefreshView(RefreshView&& other) noexcept
        : revision_(other.revision_), factory_(std::move(other.factory_)) {
        if (other.anchor_ || other.pages_[0] || other.pages_[1]) std::abort();
    }
    RefreshView(const RefreshView&) = delete;
    void reset_mount() noexcept {
        if (active_ >= 0) std::abort();
        pages_[0].reset(); pages_[1].reset();
        anchor_ = root_ = candidate_ = 0; pending_ = -1;
    }
    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        anchor_ = owner.create(parent, protocol::box);
        return anchor_ && owner.dynamic(*this) &&
            owner.transaction().fill(anchor_, protocol::width) &&
            owner.transaction().fill(anchor_, protocol::height) &&
            owner.transaction().u8(anchor_, protocol::layout, protocol::stack) &&
            owner.padding(anchor_, 0) &&
            owner.transaction().rgba(anchor_, protocol::background, 0);
    }
    bool dirty() const noexcept {
        return active_ < 0 || revision_.get() != committed_ || pages_[active_]->dirty();
    }
    Result<void> prepare(Transaction<>& tx, std::uint32_t& ids, Transport& transport) {
        if (!dirty()) return {};
        if (active_ >= 0 && revision_.get() == committed_)
            return pages_[active_]->write_fragment(tx, ids);
        pending_ = active_ == 0 ? 1 : 0;
        prepared_revision_ = revision_.get();
        candidate_ = ids++;
        if (!candidate_ || !tx.create(candidate_, anchor_, protocol::box) ||
            !tx.fill(candidate_, protocol::width) || !tx.fill(candidate_, protocol::height) ||
            !tx.u8(candidate_, protocol::layout, protocol::stack))
            return std::unexpected(tx.error());
        pages_[pending_].emplace(transport, std::invoke(factory_));
        auto result = pages_[pending_]->build_fragment(tx, candidate_, ids);
        if (!result) return result;
        if (active_ >= 0 && !tx.remove(root_)) return std::unexpected(tx.error());
        return {};
    }
    void commit(std::uint32_t generation) noexcept {
        if (pending_ >= 0) {
            if (active_ >= 0) pages_[active_].reset();
            active_ = pending_; pending_ = -1;
            root_ = candidate_; committed_ = prepared_revision_;
        }
        if (active_ >= 0) pages_[active_]->activate_fragment(generation);
    }
    void rollback() noexcept {
        if (pending_ >= 0) { pages_[pending_].reset(); pending_ = -1; }
        else if (active_ >= 0) pages_[active_]->rollback_fragment();
    }
    bool handle(const Event& event) noexcept {
        return active_ >= 0 && pages_[active_]->handle_fragment(event);
    }
private:
    State<std::uint32_t>& revision_;
    [[no_unique_address]] Factory factory_;
    std::array<std::optional<Fragment>, 2> pages_;
    std::uint32_t anchor_ = 0, root_ = 0, candidate_ = 0;
    std::uint32_t committed_ = 0, prepared_revision_ = 0;
    int active_ = -1, pending_ = -1;
};
template<class Factory> requires ViewFactory<std::decay_t<Factory>>
auto Refresh(State<std::uint32_t>& revision, Factory&& factory) {
    return RefreshView<std::decay_t<Factory>>(revision, std::forward<Factory>(factory));
}

} // namespace pxa::ui

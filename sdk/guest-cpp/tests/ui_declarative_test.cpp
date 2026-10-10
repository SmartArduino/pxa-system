#include <pxa/app.hpp>
#include <pxa/ui_action.hpp>
#include <pxa/ui_component.hpp>
#include <pxa/ui_layout.hpp>
#include <pxa/ui_widgets.hpp>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

using namespace pxa;
using namespace pxa::ui;
using namespace pxa::ui::literals;
static unsigned allocations, submits, commits;
static bool reject_commit;
static std::array<std::byte, 65536> commands{};
static std::size_t used;
void* operator new(std::size_t n) { ++allocations; return std::malloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
extern "C" std::int32_t pxa_submit(const std::uint8_t* p, std::uint32_t n) {
    ++submits;
    if (wire::get16(reinterpret_cast<const std::byte*>(p)) == 3) {
        const auto op = wire::get16(reinterpret_cast<const std::byte*>(p) + 2);
        if (op == 2) {
            assert(n >= 24 && used + n - 24 <= commands.size());
            std::memcpy(commands.data() + used, p + 24, n - 24); used += n - 24;
        }
        if (op == 3) { ++commits; if (reject_commit) return std::int32_t(Error::busy); }
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }
static std::span<const std::byte> property(std::uint32_t node, std::uint16_t key) {
    std::span<const std::byte> result;
    for (std::size_t i = 0; i < used;) {
        const auto size = wire::get16(commands.data() + i + 2);
        assert(i + size + 4 <= used);
        if (commands[i] == std::byte{2} && wire::get32(commands.data() + i + 4) == node &&
            wire::get16(commands.data() + i + 8) == key)
            result = std::span{commands}.subspan(i + 10, size - 6);
        i += size + 4;
    }
    return result;
}
static Event click(std::uint32_t node, std::uint32_t generation) {
    static std::array<std::byte, 24> bytes{};
    wire::put32(bytes.data(), 1); wire::put32(bytes.data() + 4, node);
    wire::put32(bytes.data() + 8, generation); wire::put16(bytes.data() + 12, 1);
    return {3, 0x8001, 0, bytes};
}
static Event pointer(std::uint32_t node, std::uint32_t generation) {
    static std::array<std::byte, 36> bytes{};
    wire::put32(bytes.data(), 1); wire::put32(bytes.data() + 4, node);
    wire::put32(bytes.data() + 8, generation); wire::put16(bytes.data() + 12, 7);
    bytes[25] = std::byte{pointer_phase_up};
    return {3, 0x8001, 0, bytes};
}
// This is also how existing custom fragments render borrowed SDK widgets:
// the builders disappear before the first input event or State update.
template<class Click, class Pointer> struct TransientButtons {
    static constexpr Capacity capacity{9, 1, 5};
    Click& click; Pointer& pointer; State<std::string>& label;
    template<class Owner> bool render(Owner& owner, std::uint32_t parent) {
        return Column(Button("Plain").on_click_ref(click),
            Button("Styled", WidgetStyle{{0, 0, 80, 30}}).on_click_ref(click),
            Button("Pointer").on_pointer_ref(pointer),
            Button("Styled ptr", WidgetStyle{{0, 32, 80, 30}}).on_pointer_ref(pointer),
            Button(label).on_pointer_ref(pointer)).render(owner, parent);
    }
};
struct OwnedCounter {
    static inline unsigned destroyed;
    State<int> count{0};
    ~OwnedCounter() { ++destroyed; }
    auto view() {
        return Column(Text(count), Button("Local").on_click([this] {
            count.update([](int n) { return n + 1; });
        }));
    }
};
struct BorrowedLabel {
    State<int>& count;
    auto view() { return Text(count); }
};
static Task<void> task_failure() { co_return std::unexpected(Error::denied); }
static Task<void> suspended_task(unsigned& destroyed) {
    struct Guard { unsigned& count; ~Guard() { ++count; } } guard{destroyed};
    co_await std::suspend_always{};
    co_return Result<void>{};
}

int main() {
    Transport transport; transport.phase(Phase::event);
    State<int> count{1}, extra{2}; State<bool> enabled{true}, shown{true};
    State<std::string> button_label{"Add"};
    unsigned computations = 0;
    const auto before = allocations;
    {
        auto page = Page(transport, Column(
            Text(Computed([&](int a, int b) { ++computations; return a + b; }, count, extra))
                .width(80_dp).font(Font::headline),
            Button(button_label).on_click([&] { count.set(2); count.set(3); extra.set(4); })
                .height(36_dp).font(Font::label).color(Color::on_primary)
                .radius(8_dp).enabled(enabled).visible(shown)
        ).align(Align::center).justify(Justify::center).max_width(200_dp));
        static_assert(decltype(page)::binding_capacity == 4);
        assert(page.mount() && computations == 1);
        assert(wire::get16(property(5, protocol::font_role).data()) == std::uint16_t(Font::label));
        assert(wire::get32(property(4, protocol::height).data() + 4) == 36 * 64);
        assert(wire::get32(property(2, protocol::max_width).data() + 4) == 200 * 64);
        used = 0; const auto previous = submits;
        assert(page.handle(click(4, page.generation())));
        assert(page.flush() && computations == 2);
        assert(property(3, protocol::text_value).size() == 1 &&
               property(3, protocol::text_value)[0] == std::byte{'7'});
        assert(submits == previous + 3); // One BEGIN/WRITE/COMMIT, one computed output.
        const auto idle = submits;
        assert(page.flush() && submits == idle && computations == 2);
        button_label.set("More"); enabled.set(false); shown.set(false); used = 0;
        assert(page.flush());
        assert(property(5, protocol::text_value).size() == 4);
        assert(property(4, protocol::enabled)[0] == std::byte{0});
        assert(property(4, protocol::visible)[0] == std::byte{0});
        count.set(8); reject_commit = true; const auto generation = page.generation();
        assert(!page.flush() && page.dirty() && page.generation() == generation);
        reject_commit = false; assert(page.flush() && !page.dirty());
    }
    // Destroyed computed sources must leave no State subscription dangling.
    count.set(20); extra.set(30); enabled.set(true); shown.set(true);
    {
        auto page = Page(transport, Component<OwnedCounter>());
        assert(page.mount()); used = 0;
        assert(page.handle(click(4, page.generation())) && page.flush());
        assert(property(3, protocol::text_value)[0] == std::byte{'1'});
    }
    assert(OwnedCounter::destroyed == 1);
    {
        BorrowedLabel label{count};
        auto page = Page(transport, Component(label));
        assert(page.mount()); count.set(21); assert(page.flush());
    }
    {
        const auto destroyed = OwnedCounter::destroyed;
        State<bool> show{true};
        auto page = Page(transport, When(show, [] { return Component<OwnedCounter>(); },
                                              [] { return Text("Hidden"); }));
        assert(page.mount()); const auto generation = page.generation();
        show.set(false); reject_commit = true;
        assert(!page.flush() && page.generation() == generation);
        assert(OwnedCounter::destroyed == destroyed);
        assert(page.handle(click(6, generation))); // Live owned callback survives candidate rollback.
        reject_commit = false; assert(page.flush());
        assert(OwnedCounter::destroyed == destroyed + 1);
        assert(!page.handle(click(6, generation)));
        for (unsigned i = 0; i < 3; ++i) {
            show.set(true); assert(page.flush());
            show.set(false); assert(page.flush());
        }
        assert(OwnedCounter::destroyed == destroyed + 4);
    }
    {
        unsigned clicks = 0, pointers = 0;
        auto on_click = [&] { ++clicks; };
        auto on_pointer = [&](const CanvasPointer& event) { assert(event.phase == pointer_phase_up); ++pointers; };
        State<std::string> label{"Bound ptr"};
        auto page = Page(transport, TransientButtons{on_click, on_pointer, label});
        assert(page.mount());
        assert(page.handle(click(3, page.generation())) && page.handle(click(5, page.generation())));
        for (auto node : {6u, 8u, 9u}) {
            assert(page.handle(pointer(node, page.generation())));
            assert(!page.handle(click(node, page.generation())));
        }
        label.set("Updated ptr"); used = 0; assert(page.flush());
        assert(property(10, protocol::text_value).size() == 11);
        assert(page.handle(pointer(9, page.generation())) && clicks == 2 && pointers == 4);
    }
    {
        State<double> decimal{1.25}; State<std::uint64_t> wide{UINT64_MAX};
        auto page = Page(transport, Row(Text(decimal), Text(wide)));
        used = 0; assert(page.mount());
        assert(property(3, protocol::text_value).size() == 4);
        assert(property(4, protocol::text_value).size() == 20);
        decimal.set(2.5); used = 0; assert(page.flush());
        assert(property(3, protocol::text_value).size() == 3);
    }
    {
        State<DisplayMetrics> metrics{DisplayMetrics{240, 320, 65536, 65536, {3, 9, 7, 17}}};
        auto page = Page(transport, SafeArea(metrics, Text("Safe")).padding(8_dp));
        assert(page.mount());
        auto p = property(2, 268);
        assert(wire::get32(p.data()) == 11 * 64 && wire::get32(p.data() + 4) == 17 * 64);
        auto next = metrics.get(); next.width = next.height = 454; next.shape = 2;
        metrics.set(next); used = 0; assert(page.flush()); p = property(2, 268);
        assert(wire::get32(p.data()) == (67 + 8) * 64);
        next.safe.left = 454; metrics.set(next); used = 0; assert(page.flush());
        assert(property(2, protocol::visible)[0] == std::byte{0});
        next.safe.left = 17; next.density_q16 = 124928; // 305 / 160 DPI.
        metrics.set(next); used = 0; assert(page.flush());
        assert(property(2, protocol::visible)[0] == std::byte{1});
        assert(wire::get32(property(2, 268).data()) == 2762); // 67 physical px + 8 dp, inward rounding.
    }
    {
        TaskScope scope; unsigned errors = 0;
        scope.on_error(&errors, [](void* p, Error e) noexcept {
            assert(e == Error::denied || e == Error::io_error); ++*static_cast<unsigned*>(p);
        });
        auto synchronous = Action(scope, []() -> Result<void> { return std::unexpected(Error::io_error); });
        auto asynchronous = Action(scope, [] { return task_failure(); });
        synchronous(); asynchronous();
        assert(errors == 2 && task_pool_stats().active_slots == 0);
        auto page = Page(transport, Button("Owned", WidgetStyle{{0, 0, 120, 40}})
            .on_click(Action(scope, []() -> Result<void> { return std::unexpected(Error::io_error); })));
        assert(page.mount() && page.handle(click(2, page.generation())) && errors == 3);
    }
    {
        TaskScope scope; unsigned destroyed = 0, errors = 0;
        scope.on_error(&errors, [](void* p, Error e) noexcept {
            assert(e == Error::resource_limit); ++*static_cast<unsigned*>(p);
        });
        auto action = Action(scope, [&] { return suspended_task(destroyed); });
        for (unsigned i = 0; i < PXA_COROUTINE_SLOT_COUNT; ++i) action();
        assert(task_pool_stats().active_slots == PXA_COROUTINE_SLOT_COUNT && errors == 0);
        action(); assert(errors == 1);
        scope.cancel();
        assert(task_pool_stats().active_slots == 0 && destroyed == PXA_COROUTINE_SLOT_COUNT);
        action(); assert(task_pool_stats().active_slots == 1);
        scope.cancel(); assert(destroyed == PXA_COROUTINE_SLOT_COUNT + 1);
    }
    {
        unsigned attempts = 0;
        auto page = Page(transport, Text(Computed([&](int n) -> Result<int> {
            if (++attempts == 2) return std::unexpected(Error::unavailable);
            return n;
        }, count)));
        assert(page.mount()); count.set(22);
        assert(page.flush().error() == Error::unavailable && page.dirty());
        assert(page.flush() && !page.dirty());
    }
    count.set(23);
    {
        auto page = Page(transport, Text("Invalid").height(Dp{-1}));
        assert(page.mount().error() == Error::invalid_argument);
    }
    assert(allocations == before);
    std::printf("Declarative UI: coalesced bindings, rollback, component removal, transient borrowed callbacks, numeric text, safe area, action errors/cancellation/pool limits, zero allocations OK (%u commits)\n", commits);
}

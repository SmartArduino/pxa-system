#include <pxa/ui_component.hpp>
#include <pxa/ui_layout.hpp>
#include <pxa/ui_refresh.hpp>
#include <pxa/list.hpp>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

using namespace pxa;
using namespace pxa::ui;
using namespace pxa::ui::literals;
static unsigned allocations, submissions;
static bool reject_commit;
static std::array<std::byte, 65536> commands;
static std::size_t used;
void* operator new(std::size_t n) { ++allocations; return std::malloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
extern "C" std::int32_t pxa_submit(const std::uint8_t* bytes, std::uint32_t length) {
    ++submissions;
    const auto* p = reinterpret_cast<const std::byte*>(bytes);
    if (wire::get16(p) == 3) {
        const auto op = wire::get16(p + 2);
        if (op == 1) used = 0;
        if (op == 2) {
            assert(length >= 24 && used + length - 24 <= commands.size());
            std::memcpy(commands.data() + used, p + 24, length - 24);
            used += length - 24;
        }
        if (op == 3 && reject_commit) return std::int32_t(Error::busy);
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return -3; }
static std::span<const std::byte> property(std::uint32_t node, std::uint16_t key) {
    std::span<const std::byte> result;
    for (std::size_t at = 0; at < used;) {
        const auto size = wire::get16(commands.data() + at + 2);
        assert(at + size + 4 <= used);
        if (commands[at] == std::byte{2} && wire::get32(commands.data() + at + 4) == node &&
            wire::get16(commands.data() + at + 8) == key)
            result = std::span{commands}.subspan(at + 10, size - 6);
        at += size + 4;
    }
    return result;
}
static int flag(std::uint32_t node, std::uint16_t key) {
    auto bytes = property(node, key); assert(bytes.size() == 1);
    return std::to_integer<int>(bytes[0]);
}
template<class V> static void retry(Transport& transport, V view) {
    Page page(transport, std::move(view));
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        reject_commit = true;
        assert(page.mount().error() == Error::busy && !page.generation());
    }
    reject_commit = false;
    assert(page.mount() && page.generation());
    const auto idle = submissions;
    assert(page.flush() && submissions == idle);
}
struct MoveOnly {
    int value;
    MoveOnly(int n) : value(n) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& other) : value(std::exchange(other.value, 0)) {}
};
struct Counter {
    static inline unsigned constructed, destroyed;
    State<int> count;
    explicit Counter(MoveOnly arg) : count(arg.value) { ++constructed; }
    ~Counter() { ++destroyed; }
    auto view() { return Text(count); }
};
struct Panel {
    State<DisplayMetrics>& display;
    auto view() { return SafeArea(display, Text("Content")); }
};
int main() {
    Transport transport; transport.phase(Phase::event);
    const auto before = allocations;
    retry(transport, Text("Plain"));
    retry(transport, Scroll(Component<Counter>(MoveOnly{77})).height(100_dp));
    assert(Counter::constructed == 1 && Counter::destroyed == 1);
    State<bool> selected{true};
    retry(transport, When(selected, [] { return Component<Counter>(MoveOnly{9}); },
                                  [] { return Text("No"); }));
    assert(Counter::constructed == 4 && Counter::destroyed == 4);
    ListState items{2};
    retry(transport, KeyedList<2>(items, [](unsigned i) { return i; },
                [](unsigned) { return Component<Counter>(MoveOnly{8}); }).width(100_dp));
    State<std::uint32_t> revision{0};
    retry(transport, Refresh(revision, [] { return Text("Refreshed"); }).height(50_dp));
    {
        State<bool> first{false}, last{true};
        auto view = Button("Action").on_click([] {}).enabled(first).radius(4_dp).enabled(last);
        static_assert(capacity_of<decltype(view)>.bindings == 1);
        Page page(transport, std::move(view)); assert(page.mount());
        assert(flag(2, protocol::enabled) == 1);
        const auto idle = submissions;
        first.set(true); assert(!page.dirty() && page.flush() && submissions == idle);
        last.set(false); assert(page.flush() && flag(2, protocol::enabled) == 0);
    }
    {
        State<bool> first{false};
        auto view = Button("Action").on_click([] {}).enabled(first).enabled(false);
        static_assert(capacity_of<decltype(view)>.bindings == 0);
        Page page(transport, std::move(view)); assert(page.mount());
        const auto idle = submissions;
        first.set(true); assert(!page.dirty() && page.flush() && submissions == idle);
        assert(flag(2, protocol::enabled) == 0);
    }
    {
        State<bool> value{false}, enabled{false};
        Page page(transport, Toggle("Toggle", value).enabled(enabled).font(Font::headline));
        assert(page.mount());
        assert(property(2, protocol::enabled).empty() && flag(4, protocol::enabled) == 0);
        assert(wire::get16(property(3, protocol::font_role).data()) == std::uint16_t(Font::headline));
        enabled.set(true); assert(page.flush() && flag(4, protocol::enabled) == 1);
    }
    DisplayMetrics d; d.width = d.height = 200;
    {
        Page page(transport, Row(Text("Sides")).padding(Padding{1_dp, 2_dp, 3_dp, 4_dp}));
        assert(page.mount());
        const auto bytes = property(2, 268);
        assert(bytes.size() == 16);
        for (unsigned side = 0; side < 4; ++side)
            assert(wire::get32(bytes.data() + side * 4) == (side + 1) * 64);
    }
    State<DisplayMetrics> display{d};
    State<bool> shown{false};
    {
        auto view = SafeArea(display, Text("Content")).width(100_dp).visible(shown)
            .height(180_dp).padding(Padding{4_dp, 6_dp, 8_dp, 10_dp});
        static_assert(capacity_of<decltype(view)>.bindings == 1);
        Page page(transport, std::move(view)); assert(page.mount());
        assert(flag(2, protocol::visible) == 0);
        d.width = 240; display.set(d); assert(page.flush() && flag(2, protocol::visible) == 0);
        auto p = property(2, 268);
        assert(wire::get32(p.data()) == 4 * 64 && wire::get32(p.data() + 12) == 10 * 64);
        shown.set(true); assert(page.flush() && flag(2, protocol::visible) == 1);
        d.safe = {120, 0, 120, 0}; display.set(d);
        assert(page.flush() && flag(2, protocol::visible) == 0);
        shown.set(false); assert(page.flush() && flag(2, protocol::visible) == 0);
        d.safe = {}; display.set(d); assert(page.flush() && flag(2, protocol::visible) == 0);
        shown.set(true); reject_commit = true;
        assert(page.flush().error() == Error::busy && page.dirty());
        reject_commit = false; assert(page.flush() && flag(2, protocol::visible) == 1);
    }
    display.set(d); shown.set(false); // No dangling combined subscriptions.
    {
        Page page(transport, SafeArea(d, Text("Content")).padding(110_dp));
        assert(page.mount() && flag(2, protocol::visible) == 0);
    }
    {
        Page page(transport, Component(Panel{display}).height(100_dp).visible(false));
        assert(page.mount() && flag(2, protocol::visible) == 0);
        d.width = 320; display.set(d); assert(page.flush() && flag(2, protocol::visible) == 0);
    }
    {
        TextInputRef ref; State<std::string> value{"Draft"};
        Page page(transport, Scroll(When(selected,
            [&] { return TextInput(value, ref); }, [] { return Text("No"); })));
        reject_commit = true;
        assert(page.mount().error() == Error::busy && !ref.mounted());
        assert(ref.show_keyboard().error() == Error::bad_state);
        reject_commit = false; assert(page.mount() && ref.mounted());
        value.set("Again"); assert(page.flush());
    }
    {
        State<int> value{20};
        Page page(transport, Column(Component<Counter>(MoveOnly{2}), Slider(value, 0, 10)));
        assert(page.mount().error() == Error::invalid_argument);
        value.set(5); assert(page.mount());
    }
    assert(allocations == before);
    std::puts("UI lifecycle: initial mount retries, move-only args, partial builds, final modifiers, compound control targets, SafeArea composition/recovery, list/Refresh modifiers, zero heap OK");
}

#include <pxa/ui.hpp>

#include <cassert>
#include <cstdio>
#include <vector>

static std::vector<std::vector<std::byte>> packets;
static bool reject_commit;
static unsigned binding_writes;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t length) {
    auto bytes = reinterpret_cast<const std::byte*>(data);
    packets.emplace_back(bytes, bytes + length);
    if (reject_commit && pxa::wire::get16(bytes + 2) == 3)
        return static_cast<std::int32_t>(pxa::Error::resource_limit);
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    return -3;
}

static bool tracked_write(pxa::ui::Transaction<>& transaction,
                           std::uint32_t node, const int& value) noexcept {
    ++binding_writes;
    return pxa::ui::write_int_text(transaction, node, value);
}
struct ManyBindings {
    pxa::ui::State<int>& first;
    pxa::ui::State<int>& last;
    template<class Page> bool render(Page& page, std::uint32_t parent) {
        for (unsigned i = 0; i < 70; ++i) {
            auto& state = i < 64 ? first : last;
            const auto node = page.create(parent, pxa::ui::protocol::text);
            if (!node || !pxa::ui::write_int_text(page.transaction(), node, state.get()) ||
                !page.template bind<int, tracked_write>(state, node)) return false;
        }
        return true;
    }
};

int main() {
    using namespace pxa::ui;
    using namespace pxa::ui::literals;

    pxa::Transport transport;
    transport.phase(pxa::Phase::start);
    State<int> count{0};
    auto page = Page(transport, Column(
        Text("Counter").font(Font::title), Text(count),
        Button("Increment").on_click([&] { count.set(1); count.set(2); })
    ).gap(8_dp).padding(16_dp));
    using Inferred = decltype(page);
    using View = decltype(Column(Text("Counter"), Text(count),
        Button("Increment").on_click([] {})));
    static_assert(Inferred::binding_capacity == 1);
    static_assert(Inferred::handler_capacity == 1);
    static_assert(Inferred::dynamic_capacity == 0);
    static_assert(capacity_of<View>.nodes == 5);
    static_assert(sizeof(Page<View>) < sizeof(Page<View, 32, 32, 4>));
    std::printf("UI page storage: inferred=%zu fixed32=%zu\n",
                sizeof(Page<View>), sizeof(Page<View, 32, 32, 4>));
    auto box = Column();
    static_assert(std::same_as<decltype(box.gap(8_dp)), decltype(box)&>);
    static_assert(std::same_as<decltype(Column().gap(8_dp)), decltype(box)&&>);
    static_assert(std::same_as<decltype(Text("x").font(Font::body)), InlineTextView<2>&&>);
    constexpr auto description = Column(Text<"Static title">().font(Font::title),
                                       Text("Inline text")).gap(8_dp);
    static_assert(capacity_of<decltype(description)>.bindings == 0);
    static_assert(sizeof(StaticTextView<"A long shared readonly text descriptor">) < 8);

    assert(page.mount());
    assert(page.generation() == 1);
    std::size_t initial_packets = packets.size();
    assert(initial_packets >= 3);
    assert(pxa::wire::get16(packets.back().data() + 2) == 3);

    std::array<std::byte, 24> event_bytes{};
    pxa::wire::put32(event_bytes.data(), 1);
    pxa::wire::put32(event_bytes.data() + 4, 5);
    pxa::wire::put32(event_bytes.data() + 8, 1);
    pxa::wire::put16(event_bytes.data() + 12, 1);
    pxa::Event click{3, 0x8001, 0, event_bytes};
    assert(page.handle(click));
    assert(count.get() == 2 && page.dirty());
    assert(page.flush());
    assert(!page.dirty() && page.generation() == 2);
    assert(packets.size() == initial_packets + 3);
    assert(pxa::wire::get16(packets[initial_packets + 1].data() + 2) == 2);
    assert(!page.handle(click));
    assert(page.flush());
    assert(packets.size() == initial_packets + 3);

    State<int> first{0}, last{0};
    {
        Page<ManyBindings, 70, 0> many(transport, ManyBindings{first, last});
        assert(many.mount());
        const auto generation = many.generation();
        last.set(1);
        last.set(2);
        reject_commit = true;
        assert(!many.flush());
        assert(many.dirty() && many.generation() == generation && binding_writes == 6);
        reject_commit = false;
        assert(many.flush());
        assert(!many.dirty() && binding_writes == 12);
        const auto before_idle = packets.size();
        assert(many.flush() && packets.size() == before_idle);
        first.set(1);
        last.set(3);
        assert(many.flush() && binding_writes == 82);
        assert(!many.dirty());
    }
    const auto before_unmounted = packets.size();
    first.set(4);
    last.set(5);
    assert(packets.size() == before_unmounted);

    Ref<int> number{7};
    Ref<std::string> label{std::string("Ready")};
    assert(!number.mounted() && !label.mounted());
    assert(!number.set(8) && number.get() == 7);
    {
        auto refs = Page(transport, Column(Text(number), Text(label)));
        static_assert(decltype(refs)::binding_capacity == 2);
        static_assert(decltype(refs)::ref_capacity == 2);
        assert(refs.mount() && number.mounted() && label.mounted());
        assert(number.set(8) && label.set("Running") && refs.dirty());
        reject_commit = true;
        assert(!refs.flush() && refs.dirty());
        assert(number.mounted() && label.mounted());
        reject_commit = false;
        assert(refs.flush() && !refs.dirty());
    }
    assert(!number.mounted() && !label.mounted());
    assert(!number.set(9) && !label.set("Stopped"));

    Ref<int> duplicate{1};
    auto invalid = Page(transport, Column(Text(duplicate), Text(duplicate)));
    assert(!invalid.mount() && !duplicate.mounted());

    Page<decltype(Text(number)), 1, 0, 0, 0> no_ref_slots(
        transport, Text(number));
    auto exhausted = no_ref_slots.mount();
    assert(!exhausted && exhausted.error() == pxa::Error::resource_limit);
    assert(!number.mounted());
}

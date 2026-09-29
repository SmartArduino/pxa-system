#include <pxa/list.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

static unsigned allocations;
void* operator new(std::size_t size) {
    ++allocations;
    if (auto* pointer = std::malloc(size)) return pointer;
    std::abort();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

using namespace pxa;
using namespace pxa::ui;

struct Node {
    std::uint32_t parent = 0;
    std::uint32_t order = 0;
    std::uint32_t count = 0;
    bool present = false;
};
static std::array<Node, 1024> nodes, candidate;
static std::array<std::byte, 16384> stream;
static std::size_t stream_bytes;
static std::uint32_t ordering;
static bool reject_commit;
static unsigned submissions;

static void apply() {
    for (std::size_t offset = 0; offset < stream_bytes;) {
        const auto command = std::to_integer<unsigned>(stream[offset]);
        const auto size = wire::get16(stream.data() + offset + 2);
        assert(offset + 4 + size <= stream_bytes && size >= 4);
        const auto* data = stream.data() + offset + 4;
        const auto id = wire::get32(data);
        assert(id && id < nodes.size());
        if (command == protocol::create) {
            assert(size == 16 && !candidate[id].present);
            const auto parent = wire::get32(data + 4);
            assert(!parent || candidate[parent].present);
            candidate[id] = {parent, ++ordering, 0, true};
        } else if (command == protocol::set_property) {
            assert(candidate[id].present && size >= 6);
            if (wire::get16(data + 4) == protocol::item_count)
                candidate[id].count = wire::get32(data + 6);
        } else if (command == protocol::move) {
            assert(size == 12 && candidate[id].present);
            candidate[id].parent = wire::get32(data + 4);
            candidate[id].order = ++ordering;
        } else {
            assert(command == protocol::remove && size == 4 && candidate[id].present);
            for (std::size_t child = 1; child < candidate.size(); ++child) {
                for (auto parent = static_cast<std::uint32_t>(child); parent;
                     parent = candidate[parent].parent) {
                    if (parent == id) { candidate[child].present = false; break; }
                }
            }
        }
        offset += 4 + size;
    }
}
extern "C" std::int32_t pxa_submit(const std::uint8_t* bytes, std::uint32_t size) {
    ++submissions;
    const auto* data = reinterpret_cast<const std::byte*>(bytes);
    assert(wire::get16(data) == protocol::service);
    const auto opcode = wire::get16(data + 2);
    if (opcode == protocol::begin) {
        candidate = data[36] == std::byte{protocol::replace_surface}
                    ? std::array<Node, 1024>{} : nodes;
        stream_bytes = 0;
    } else if (opcode == protocol::write) {
        assert(stream_bytes + size - 24 <= stream.size());
        std::memcpy(stream.data() + stream_bytes, data + 24, size - 24);
        stream_bytes += size - 24;
    } else if (opcode == protocol::commit) {
        apply();
        if (reject_commit) return static_cast<std::int32_t>(Error::resource_limit);
        nodes = candidate;
    } else {
        assert(opcode == protocol::cancel);
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) { return -3; }

static Event event(std::uint32_t node, std::uint32_t generation,
                    std::uint16_t kind, std::span<std::byte> payload) {
    payload[0] = std::byte{1};
    wire::put32(payload.data() + 4, node);
    wire::put32(payload.data() + 8, generation);
    wire::put16(payload.data() + 12, kind);
    return {3, 0x8001, 0, payload};
}
static unsigned node_count() {
    unsigned count = 0;
    for (auto& node : nodes) if (node.present) ++count;
    return count;
}

int main() {
    Transport transport;
    transport.phase(Phase::event);
    ListState source{2};
    std::array<unsigned, 2> keys{100, 200};
    unsigned selected = 0, constructed = 0;
    std::array<std::byte, 24> click{};
    const auto baseline = allocations;
    {
        auto page = Page(transport, KeyedList<4, 0, 1>(source,
            [&](std::uint32_t index) { return keys[index]; },
            [&](unsigned key) {
                ++constructed;
                return Button("Select").on_click([&, key] { selected = key; });
            }));
        assert(page.mount() && constructed == 2 && node_count() == 8);
        assert(page.handle(event(4, page.generation(), 1, click)) && selected == 100);
        keys = {200, 100};
        source.invalidate();
        assert(page.flush() && constructed == 2);
        assert(nodes[6].order < nodes[3].order);
        assert(page.handle(event(4, page.generation(), 1, click)) && selected == 100);

        keys = {100, 100};
        source.invalidate();
        auto duplicate = page.flush();
        assert(!duplicate && duplicate.error() == Error::invalid_argument);
        assert(node_count() == 8 && constructed == 2 && page.dirty());

        keys = {300, 400};
        source.invalidate();
        reject_commit = true;
        const auto old_generation = page.generation();
        auto failed = page.flush();
        assert(!failed && failed.error() == Error::resource_limit);
        assert(page.generation() == old_generation && node_count() == 8);
        assert(page.handle(event(4, old_generation, 1, click)) && selected == 100);
        reject_commit = false;
        assert(page.flush() && constructed == 6 && node_count() == 8);
        assert(!nodes[3].present && !nodes[6].present);
        assert(!page.handle(event(4, page.generation(), 1, click)));
        source.set_count(0);
        assert(page.flush() && node_count() == 2);
        const auto idle = submissions;
        assert(page.flush() && submissions == idle);
    }
    source.invalidate();
    assert(allocations == baseline);

    ListState virtual_source{10000};
    bool duplicate_keys = false;
    constructed = 0;
    {
        auto page = Page(transport, VirtualList<4, 0, 1>(virtual_source, Dp{40},
            [&](std::uint32_t index) { return duplicate_keys ? 1u : index + 1; },
            [&](unsigned key) {
                ++constructed;
                return Button("Select").on_click([&, key] { selected = key; });
            }));
        assert(page.mount() && constructed == 0 && node_count() == 2);
        assert(nodes[2].count == 10000);
        std::array<std::byte, 32> range{};
        wire::put32(range.data() + 24, 10);
        wire::put32(range.data() + 28, 4);
        assert(page.handle(event(2, page.generation(), 9, range)));
        assert(page.flush() && constructed == 4 && node_count() == 14);
        const auto previous_generation = page.generation();
        wire::put32(range.data() + 24, 12);
        assert(page.handle(event(2, page.generation(), 9, range)));
        assert(page.flush() && constructed == 6 && node_count() == 14);
        assert(nodes[9].present && !nodes[3].present && !nodes[6].present);
        assert(!page.handle(event(10, previous_generation, 1, click)));
        assert(page.handle(event(10, page.generation(), 1, click)) && selected == 13);
        assert(!page.handle(event(4, page.generation(), 1, click)));

        wire::put32(range.data() + 24, 50);
        reject_commit = true;
        assert(page.handle(event(2, page.generation(), 9, range)));
        assert(!page.flush() && node_count() == 14);
        assert(page.handle(event(10, page.generation(), 1, click)) && selected == 13);
        reject_commit = false;
        assert(page.flush() && constructed == 14 && node_count() == 14);
        duplicate_keys = true;
        virtual_source.invalidate();
        assert(!page.flush() && constructed == 14);
        duplicate_keys = false;
        assert(page.flush() && constructed == 14);
        wire::put32(range.data() + 28, 5);
        assert(page.handle(event(2, page.generation(), 9, range)));
        auto capacity = page.flush();
        assert(!capacity && capacity.error() == Error::resource_limit);
        assert(node_count() == 14 && constructed == 14);
        virtual_source.set_count(5);
        assert(page.flush() && node_count() == 2 && nodes[2].count == 5);
        const auto idle = submissions;
        assert(page.flush() && submissions == idle);
    }
    virtual_source.invalidate();
    assert(allocations == baseline);

    ListState bound_source{2};
    State<int> values[2]{State<int>{1}, State<int>{2}};
    {
        auto page = Page(transport, KeyedList<2, 1, 0>(bound_source,
            [](std::uint32_t index) { return index; },
            [&](unsigned key) { return Text(values[key]); }));
        assert(page.mount() && !page.dirty());
        values[1].set(3);
        assert(page.dirty());
        reject_commit = true;
        assert(!page.flush() && page.dirty());
        reject_commit = false;
        assert(page.flush() && !page.dirty());
        bound_source.set_count(1);
        assert(page.flush());
        values[1].set(4);
        assert(!page.dirty());
        values[0].set(5);
        assert(page.dirty() && page.flush() && !page.dirty());
    }
    values[0].set(6);
    bound_source.invalidate();
    assert(allocations == baseline);

    {
        auto owned = [] {
            char text[] = "A long locally owned title that cannot fit in string SSO";
            return Text(text);
        }();
        auto page = Page(transport, Column(std::move(owned),
            Text<"A long shared readonly title that never allocates a string">()));
        assert(page.mount());
        static_assert(decltype(page)::binding_capacity == 0);
        static_assert(decltype(page)::handler_capacity == 0);
        const auto idle = submissions;
        assert(page.flush() && submissions == idle);
    }
    assert(allocations == baseline);
}

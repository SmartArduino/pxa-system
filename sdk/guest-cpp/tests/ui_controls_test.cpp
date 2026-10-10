#include <pxa/ui.hpp>

#include <array>
#include <cassert>
#include <vector>

static std::vector<std::vector<std::byte>> packets;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t length) {
    auto* bytes = reinterpret_cast<const std::byte*>(data);
    packets.emplace_back(bytes, bytes + length);
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    return -3;
}

static pxa::Event event(std::uint32_t node, std::uint32_t generation,
                        std::uint16_t kind,
                        std::span<const std::byte> value) {
    static std::array<std::byte, 24> header{};
    pxa::wire::put32(header.data(), 1);
    pxa::wire::put32(header.data() + 4, node);
    pxa::wire::put32(header.data() + 8, generation);
    pxa::wire::put16(header.data() + 12, kind);
    static std::array<std::byte, 88> payload{};
    for (std::size_t i = 0; i < header.size(); ++i)
        payload[i] = header[i];
    for (std::size_t i = 0; i < value.size(); ++i)
        payload[24 + i] = value[i];
    return {3, 0x8001, 0, {payload.data(), 24 + value.size()}};
}

int main() {
    using namespace pxa::ui;
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    State<bool> enabled{false};
    State<int> level{3};
    State<std::string> name{std::string("Ada")};
    auto page = Page(transport, Column(
        Toggle("Enabled", enabled), Slider(level, 0, 10),
        Progress(level, 0, 10), TextInput(name),
        Image("assets/picture.png"), Scroll(Text("End"))));
    assert(page.mount());
    const auto initial_count = packets.size();
    assert(initial_count >= 3);

    std::array<std::byte, 4> value{};
    pxa::wire::put32(value.data(), 7);
    assert(page.handle(event(6, page.generation(), 2, value)));
    assert(level.get() == 7 && page.dirty());
    assert(page.flush());
    assert(packets.size() == initial_count + 3);
    const auto& writes = packets[initial_count + 1];
    assert(pxa::wire::get16(writes.data() + 2) == 2);
    assert(writes.size() == 20 + 4 + 14 * 2);

    pxa::wire::put32(value.data(), 1);
    assert(page.handle(event(5, page.generation(), 2, value)));
    assert(enabled.get());
    assert(page.flush());

    const std::array<std::byte, 3> text{
        std::byte{'B'}, std::byte{'o'}, std::byte{'b'}};
    assert(page.handle(event(8, page.generation(), 6, text)));
    assert(name.get() == "Bob");
    assert(page.flush());
    assert(!page.handle(event(8, page.generation() - 1, 6, text)));

    State<int> invalid{20};
    auto rejected = Page(transport, Slider(invalid, 0, 10));
    auto result = rejected.mount();
    assert(!result && result.error() == pxa::Error::invalid_argument);

    Ref<bool> toggle_ref{false};
    Ref<int> slider_ref{2};
    Ref<int> progress_ref{4};
    Ref<std::string> input_ref{std::string("Ready")};
    {
        auto refs = Page(transport, Column(
            Toggle("Toggle", toggle_ref), Slider(slider_ref, 0, 10),
            Progress(progress_ref), TextInput(input_ref)));
        static_assert(decltype(refs)::ref_capacity == 4);
        assert(refs.mount());
        assert(toggle_ref.mounted() && slider_ref.mounted() &&
               progress_ref.mounted() && input_ref.mounted());
        assert(toggle_ref.set(true) && progress_ref.set(6));
        assert(refs.flush());
        pxa::wire::put32(value.data(), 8);
        assert(refs.handle(event(6, refs.generation(), 2, value)));
        assert(slider_ref.get() == 8 && refs.flush());
        assert(refs.handle(event(8, refs.generation(), 6, text)));
        assert(input_ref.get() == "Bob" && refs.flush());
    }
    assert(!toggle_ref.mounted() && !slider_ref.mounted() &&
           !progress_ref.mounted() && !input_ref.mounted());
    assert(!toggle_ref.set(false) && !slider_ref.set(1) &&
           !progress_ref.set(1) && !input_ref.set("Gone"));

    TextInputRef editor;
    assert(!editor.mounted() && !editor.show_keyboard());
    static_assert(sizeof(TextInput(name)) == sizeof(void*));
    unsigned submitted = 0;
    {
        auto inputs = Page(transport, TextInput(name, editor).on_submit(
            [&submitted] { ++submitted; }));
        static_assert(decltype(inputs)::ref_capacity == 1);
        static_assert(decltype(inputs)::handler_capacity == 2);
        assert(inputs.mount() && editor.mounted());
        assert(editor.show_keyboard());
        auto focus = packets.back();
        assert(pxa::wire::get16(focus.data()) == 3);
        assert(pxa::wire::get16(focus.data() + 2) == 12);
        assert(focus.size() == pxa::wire::header_bytes + 12);
        const auto* payload = focus.data() + pxa::wire::header_bytes;
        assert(pxa::wire::get32(payload) == 1);
        assert(pxa::wire::get32(payload + 4) == 2 && payload[8] == std::byte{1});
        assert(editor.hide_keyboard());
        assert(packets.back()[pxa::wire::header_bytes + 8] == std::byte{0});
        auto conflicting = Page(transport, TextInput(name, editor));
        assert(!conflicting.mount() && editor.mounted());
        assert(inputs.handle(event(2, inputs.generation(), 6, text)));
        assert(name.get() == "Bob");
        assert(inputs.handle(event(2, inputs.generation(), 6, {})));
        assert(name.get().empty());
        assert(inputs.handle(event(2, inputs.generation(), 1, {})) && submitted == 1);
    }
    assert(!editor.mounted() && !editor.hide_keyboard());
    {
        auto dynamic = Page(transport, TextInput(name, editor).single_line().max_bytes(2048)
            .on_submit([&submitted] { ++submitted; }));
        assert(dynamic.mount());
        std::string long_text;
        for (unsigned i = 0; i < 300; ++i) long_text += "中文🙂";
        assert(long_text.size() == 3000);
        long_text.resize(2000); // An exact UTF-8 boundary.
        auto bytes = std::as_bytes(std::span{long_text.data(), long_text.size()});
        assert(dynamic.handle(event(2, dynamic.generation(), 6, bytes)));
        assert(name.get() == long_text);
        assert(dynamic.flush()); // Large properties stream through small WRITE packets.
        assert(dynamic.handle(event(2, dynamic.generation(), 1, {})) && submitted == 2);
        auto malformed = std::array{std::byte{0xe4}, std::byte{0xb8}};
        assert(!dynamic.handle(event(2, dynamic.generation(), 6, malformed)));
        assert(name.get() == long_text);
        assert(dynamic.handle(event(2, dynamic.generation(), 6, {})));
        assert(name.get().empty() && dynamic.flush());
        const std::string url = "https://example.org/catalog.json?token=" + std::string(200, 'a');
        assert(dynamic.handle(event(2, dynamic.generation(), 6,
            std::as_bytes(std::span{url.data(), url.size()}))));
        assert(name.get() == url && dynamic.flush());
        name.set("");
    }
    {
        auto invalid = Page(transport, TextInput(name).max_bytes(0));
        assert(!invalid.mount());
        auto oversized = Page(transport, TextInput(name).max_bytes(max_text_bytes + 1));
        assert(!oversized.mount());
    }
    {
        auto duplicate = Page(transport, Column(TextInput(name, editor),
                                                TextInput(name, editor)));
        assert(!duplicate.mount() && !editor.mounted());
    }
}

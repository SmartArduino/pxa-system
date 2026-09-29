#include <pxa/ui_wire.hpp>

#include <cassert>
#include <string>
#include <vector>

static std::vector<std::vector<std::byte>> packets;
static bool fail_write = false;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t length) {
    auto bytes = reinterpret_cast<const std::byte*>(data);
    if (fail_write && pxa::wire::get16(bytes + 2) == 2) return -11;
    packets.emplace_back(bytes, bytes + length);
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    return -3;
}

int main() {
    pxa::Transport transport;
    transport.phase(pxa::Phase::start);
    {
        pxa::ui::Transaction tx(transport, 1, 1,
                                 pxa::ui::protocol::replace_surface);
        assert(tx.valid());
        assert(tx.create(1, 0, pxa::ui::protocol::root));
        assert(tx.create(2, 1, pxa::ui::protocol::text));
        assert(tx.text(2, "Hello"));
        assert(tx.commit());
    }
    assert(packets.size() == 3);
    assert(pxa::wire::get16(packets[0].data() + 2) == 1);
    assert(pxa::wire::get16(packets[1].data() + 2) == 2);
    assert(pxa::wire::get16(packets[2].data() + 2) == 3);
    assert(pxa::wire::get32(packets[1].data() + 20) == 1);
    auto stream = packets[1].data() + 24;
    assert(std::to_integer<unsigned>(stream[0]) == 1);
    assert(pxa::wire::get16(stream + 2) == 16);
    assert(pxa::wire::get32(stream + 4) == 1);

    packets.clear();
    std::string large(1300, 'x');
    {
        pxa::ui::Transaction tx(transport, 2, 1, pxa::ui::protocol::patch);
        assert(tx.text(2, large));
        assert(tx.commit());
    }
    assert(packets.size() > 4);
    std::vector<std::byte> joined;
    for (std::size_t i = 1; i + 1 < packets.size(); ++i) {
        assert(pxa::wire::get16(packets[i].data() + 2) == 2);
        joined.insert(joined.end(), packets[i].begin() + 24,
                      packets[i].end());
    }
    assert(joined.size() == 4 + 6 + large.size());
    assert(std::to_integer<unsigned>(joined[0]) == 2);
    assert(pxa::wire::get16(joined.data() + 2) == 6 + large.size());
    assert(pxa::wire::get32(joined.data() + 4) == 2);
    assert(pxa::wire::get16(joined.data() + 8) == 768);
    for (std::size_t i = 10; i < joined.size(); ++i)
        assert(joined[i] == std::byte{'x'});

    packets.clear();
    fail_write = true;
    {
        pxa::ui::Transaction tx(transport, 3, 1, pxa::ui::protocol::patch);
        assert(tx.text(2, "fail"));
        auto result = tx.commit();
        assert(!result && result.error() == pxa::Error::internal);
    }
    assert(pxa::wire::get16(packets.back().data() + 2) == 4);
}

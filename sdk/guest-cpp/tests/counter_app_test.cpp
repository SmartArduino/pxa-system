#include <pxa/app.hpp>

#include <array>
#include <cassert>
#include <vector>

static std::vector<std::vector<std::byte>> packets;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t length) {
    auto bytes = reinterpret_cast<const std::byte*>(data);
    packets.emplace_back(bytes, bytes + length);
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    return -3;
}

#include "../examples/counter/main.cpp"

int main() {
    assert(pxa_app_start(nullptr, 0) == 0);
    assert(packets.size() >= 3);
    auto start_count = packets.size();

    std::array<std::byte, 44> bytes{};
    pxa::wire::put16(bytes.data(), 3);
    pxa::wire::put16(bytes.data() + 2, 0x8001);
    pxa::wire::put32(bytes.data() + 12, 24);
    pxa::wire::put32(bytes.data() + 20, 1);
    pxa::wire::put32(bytes.data() + 24, 5);
    pxa::wire::put32(bytes.data() + 28, 1);
    pxa::wire::put16(bytes.data() + 32, 1);

    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(bytes.data()),
               bytes.size()) == 1);
    assert(packets.size() == start_count + 3);
    assert(pxa::wire::get16(packets.back().data() + 2) == 3);
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(bytes.data()),
               bytes.size()) == 0);
    assert(packets.size() == start_count + 3);
    pxa_app_stop(0);
}

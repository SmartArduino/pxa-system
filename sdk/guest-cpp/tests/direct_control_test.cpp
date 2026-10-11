#include <pxa/task.hpp>

#include <cassert>

static const std::uint8_t* borrowed;
static std::uint32_t length, route;
static std::uint64_t token;
static std::int32_t status;
static unsigned calls;

extern "C" std::int32_t pxa_call(std::uint32_t next_route,
    std::uint64_t next_token, const std::uint8_t* bytes, std::uint32_t size) {
    ++calls;
    route = next_route;
    token = next_token;
    borrowed = bytes;
    length = size;
    return status;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
    std::uint8_t*, std::uint32_t) { return 0; }

int main() {
    static_assert(sizeof(pxa::Transport) == 16);
    static_assert(PXA_REQUEST_CAPACITY == 32);
    pxa::Transport transport;
    std::array<std::byte, pxa::wire::max_control_bytes> packet{};
    auto payload = std::span(packet).first(500);
    assert(!transport.send(7, 2, 9, payload));
    transport.phase(pxa::Phase::start);
    assert(transport.send(7, 2, UINT64_C(0x1122334455667788), payload));
    assert(route == (7 | (2 << 16)) && token == UINT64_C(0x1122334455667788));
    assert(borrowed == reinterpret_cast<const std::uint8_t*>(payload.data()));
    assert(length == payload.size());
    assert(transport.send(7, 2, 9, std::span(packet).first(
        pxa::wire::max_control_bytes - pxa::wire::header_bytes)));
    assert(length == 4076);
    packet.fill(std::byte{0xcd});
    assert(transport.send(2, 12, 0, std::span(packet).subspan(20, 12)));
    assert(route == (2 | (12 << 16)) && token == 0 && length == 12);
    assert(borrowed == reinterpret_cast<const std::uint8_t*>(packet.data() + 20));
    for (auto byte : packet) assert(byte == std::byte{0xcd});
    status = static_cast<std::int32_t>(pxa::Error::busy);
    assert(transport.send(7, 2, 10).error() == pxa::Error::busy);
    const auto previous = calls;
    assert(!transport.send(0, 2, 10));
    assert(!transport.send(7, 0, 10));
    assert(!transport.send(7, 2, 10, packet));
    transport.phase(pxa::Phase::stopped);
    assert(!transport.send(7, 2, 10));
    assert(calls == previous);

    pxa::RequestTable requests;
    int complete = 0;
    auto callback = +[](void* value, const pxa::Event&) {
        ++*static_cast<int*>(value);
    };
    for (std::uint64_t i = 0; i < 32; ++i)
        assert(requests.add((i << 32) | 1, &complete, callback, 7, 2));
    assert(requests.add(1, &complete, callback, 9, 3).error() == pxa::Error::bad_state);
    assert(requests.add(33, &complete, callback).error() == pxa::Error::resource_limit);
    for (std::uint64_t i = 0; i < 32; ++i)
        assert(requests.dispatch({7, 2, (i << 32) | 1, {}}));
    assert(complete == 32);
    assert(requests.add(1, &complete, callback, 7, 2));
    assert(requests.add(1, &complete, callback, 8, 0x8002));
    assert(requests.dispatch({8, 0x8002, 1, {}}));
    assert(requests.dispatch({7, 2, 1, {}}));
    assert(complete == 34);
    assert(requests.add(2, &complete, callback));
    assert(requests.add(2, &complete, callback, 8, 0x8002));
    assert(requests.dispatch({8, 0x8002, 2, {}}));
    assert(requests.dispatch({7, 2, 2, {}}));
    assert(complete == 36);
}

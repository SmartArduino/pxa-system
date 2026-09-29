#include <pxa/permission.hpp>

#include <cassert>

static std::uint64_t token;
static std::uint16_t opcode;
static unsigned submits;
static unsigned closes;
static bool complete;
static std::array<std::byte, 1024> scope_bytes;
static std::array<std::byte, 1200> packet;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t size) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    if (pxa::wire::get16(bytes) == 1) {
        assert(pxa::wire::get16(bytes + 2) == 2);
        ++closes;
        return 0;
    }
    assert(pxa::wire::get16(bytes) == 11);
    opcode = pxa::wire::get16(bytes + 2);
    token = pxa::wire::get64(bytes + 4);
    assert(token);
    if (++submits == 1) {
        assert(opcode == 1 && bytes == packet.data());
        assert(size == 20 + 4 + 12 + 4 + 1024);
    } else {
        assert(submits == 2 && opcode == 2);
    }
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    assert(false);
    return -1;
}

pxa::Task<void> run(pxa::PermissionService& service) {
    auto invalid = co_await service.check("", "media");
    assert(!invalid && invalid.error() == pxa::Error::invalid_argument);
    auto too_large = co_await service.check("sensor.scope", scope_bytes);
    assert(!too_large && too_large.error() == pxa::Error::resource_limit);
    auto allowed = co_await service.check("sensor.scope", scope_bytes, packet);
    assert(allowed && !*allowed);
    auto grant = co_await service.acquire("audio.playback", "media");
    assert(!grant && grant.error() == pxa::Error::denied);
    complete = true;
    co_return pxa::Result<void>{};
}

int main() {
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::RequestTable requests;
    pxa::PermissionService service(transport, requests);
    pxa::TaskScope tasks;
    assert(tasks.start(run(service)));
    assert(submits == 1);
    const std::array<std::byte, 5> denied_decision{};
    assert(requests.dispatch({11, opcode, token, denied_decision}));
    assert(submits == 2);
    std::array<std::byte, 4> denied{};
    pxa::wire::put32(denied.data(), static_cast<std::uint32_t>(pxa::Error::denied));
    assert(requests.dispatch({11, opcode, token, denied}));
    tasks.reap();
    assert(complete && closes == 0);

    std::array<std::byte, 27> revoked{};
    pxa::wire::put16(revoked.data(), 1);
    pxa::wire::put16(revoked.data() + 2, 14);
    for (std::size_t i = 0; i < 14; ++i)
        revoked[4 + i] = std::byte("audio.playback"[i]);
    pxa::wire::put16(revoked.data() + 18, 2);
    pxa::wire::put16(revoked.data() + 20, 5);
    for (std::size_t i = 0; i < 5; ++i)
        revoked[22 + i] = std::byte("media"[i]);
    auto event = pxa::decode_permission_revoked({11, 0x8001, 0, revoked});
    assert(event && event->name == "audio.playback" && event->scope.size() == 5);
    assert(!pxa::decode_permission_revoked({11, 0x8001, 1, revoked}));
    revoked[2] = std::byte{96};
    assert(!pxa::decode_permission_revoked({11, 0x8001, 0, revoked}));
}

#include <pxa/app.hpp>
#include <algorithm>
#include <cassert>
#include <cstdio>

static std::uint64_t token;
static std::uint16_t opcode;
static unsigned submits;
static bool finished;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t size) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    assert(pxa::wire::get16(bytes) == 6);
    opcode = pxa::wire::get16(bytes + 2);
    token = pxa::wire::get64(bytes + 4);
    assert(token && pxa::wire::get32(bytes + 12) == size - 20);
    const auto key_size = pxa::wire::get16(bytes + 22);
    const std::string_view key(reinterpret_cast<const char*>(bytes + 24), key_size);
    ++submits;
    assert(pxa::task_pool_stats().active_slots == 2); // App task + one service task.
    if (submits == 1) assert(opcode == 1 && key == "owned.get");
    else if (submits == 2) {
        assert(opcode == 2 && key == "owned.set");
        const auto* record = bytes + 24 + key_size;
        assert(pxa::wire::get16(record) == 2 && pxa::wire::get16(record + 2) == 4);
        assert(record[4] == std::byte{0} && record[5] == std::byte{0} &&
               record[6] == std::byte{0xa0} && record[7] == std::byte{0x3f}); // 1.25f.
        assert(size == 20 + 8 + key_size + 4);
    } else if (submits <= 5) assert(opcode == 1);
    else if (submits == 6) assert(opcode == 2);
    else {
        assert(submits == 7 && opcode == 1 && key == std::string(64, 'a'));
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) {
    assert(false); return -1;
}

pxa::Task<void> run(pxa::Context& context) {
    auto pending_get = [&] {
        std::string key = "owned.get";
        return context.storage().get_value<std::int32_t>(key);
    }(); // Temporary key and service facade have already been destroyed.
    auto read = co_await std::move(pending_get);
    assert(read && *read == -123);
    auto pending_set = [&] {
        std::string key = "owned.set";
        float value = 1.25f;
        auto task = context.storage().set_value(key, value);
        value = 9;
        key.assign("changed");
        return task;
    }();
    assert(co_await std::move(pending_set));
    auto bad_bool = co_await context.storage().get_value<bool>("bool");
    assert(!bad_bool && bad_bool.error() == pxa::Error::protocol_error);
    auto bad_size = co_await context.storage().get_value<std::uint32_t>("short");
    assert(!bad_size && bad_size.error() == pxa::Error::protocol_error);
    auto absent = co_await context.storage().get_value<std::uint32_t>("missing");
    assert(!absent && absent.error() == pxa::Error::not_found);
    auto denied = co_await context.storage().set_value("denied", true);
    assert(!denied && denied.error() == pxa::Error::denied);
    const auto before = submits;
    auto invalid = co_await context.storage().get_value<int>("bad/key");
    assert(!invalid && invalid.error() == pxa::Error::invalid_argument);
    auto too_long = co_await context.storage().set_value(std::string(65,'a'), 1);
    assert(!too_long && too_long.error() == pxa::Error::invalid_argument && submits == before);
    auto largest = co_await context.storage().get_value<std::uint64_t>(std::string(64,'a'));
    assert(largest && *largest == UINT64_MAX);
    finished = true;
    co_return pxa::Result<void>{};
}
struct App {
    pxa::Result<void> on_start(pxa::Context& context) { return context.tasks().start(run(context)); }
};
PXA_APPLICATION(App)

static void deliver(std::span<const std::byte> body) {
    std::array<std::byte, 128> packet{};
    pxa::wire::put16(packet.data(), 6);
    pxa::wire::put16(packet.data() + 2, opcode);
    pxa::wire::put64(packet.data() + 4, token);
    pxa::wire::put32(packet.data() + 12, body.size());
    std::copy(body.begin(), body.end(), packet.begin() + 20);
    assert(pxa_app_on_event(reinterpret_cast<const std::uint8_t*>(packet.data()),20+body.size()) == 1);
}
template<pxa::binary::Scalar T> static void deliver_value(T value) {
    std::array<std::byte, 8 + sizeof(T)> body{};
    pxa::wire::put16(body.data() + 4, 2);
    pxa::wire::put16(body.data() + 6, sizeof(T));
    assert(pxa::binary::write(body, value, 8));
    deliver(body);
}
int main() {
    const auto reserved = pxa::task_pool_stats().reserved_bytes;
    assert(pxa_app_start(nullptr, 0) == 0 && submits == 1);
    deliver_value(std::int32_t{-123});
    const std::array<std::byte, 4> success{};
    deliver(success);
    deliver_value(std::uint8_t{2}); // Non-canonical bool.
    deliver_value(std::uint16_t{1}); // Wrong scalar length.
    deliver(pxa::binary::encode(std::int32_t{-5}));
    deliver(pxa::binary::encode(std::int32_t{-4}));
    deliver_value(UINT64_MAX);
    assert(finished && submits == 7);
    pxa_app_stop(0);
    const auto stats = pxa::task_pool_stats();
    assert(stats.active_slots == 0 && stats.peak_slots == 2 && stats.allocation_failures == 0);
    assert(stats.reserved_bytes == reserved);
    std::printf("Typed storage: wire compatible, owned keys/values, malformed results, peak %u task slots, pool %zu B unchanged OK\n", stats.peak_slots, reserved);
}

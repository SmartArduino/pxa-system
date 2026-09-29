#include <pxa/app.hpp>

#include <array>
#include <cassert>

static std::array<std::byte, 2100> packet;
static std::array<std::byte, 2048> value;
static std::array<std::byte, 16> read_buffer;
static std::array<pxa::StorageKey, 14> keys;
static std::uint64_t token;
static std::uint16_t opcode;
static unsigned submits;
static bool finished;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data,
                                      std::uint32_t size) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    assert(pxa::wire::get16(bytes) == 6);
    opcode = pxa::wire::get16(bytes + 2);
    token = pxa::wire::get64(bytes + 4);
    assert(token && pxa::wire::get32(bytes + 12) == size - 20);
    if (++submits == 1) {
        assert(opcode == 2 && bytes == packet.data());
        assert(size == 20 + 8 + 4 + value.size());
        assert(pxa::wire::get16(bytes + 20) == 1);
        assert(pxa::wire::get16(bytes + 22) == 4);
        assert(pxa::wire::get16(bytes + 28) == 2);
        assert(pxa::wire::get16(bytes + 30) == value.size());
        assert(bytes[32] == value[0] && bytes[size - 1] == value.back());
    } else if (submits == 2) {
        assert(opcode == 1);
    } else if (submits == 3) {
        assert(opcode == 4 && size == 20);
    } else if (submits == 4) {
        assert(opcode == 3);
    } else if (submits == 5) {
        assert(opcode == 2);
        assert(size == 20 + 8 + 4 + 3);
    } else {
        assert(false);
    }
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) {
    assert(false);
    return -1;
}

pxa::Task<void> run(pxa::Context& context) {
    auto stored = co_await context.storage().set("save", value, packet);
    assert(stored);
    auto read = co_await context.storage().get("save", read_buffer);
    assert(read && *read == 3);
    assert(read_buffer[0] == std::byte{'a'});
    auto listed = co_await context.storage().list({}, keys);
    assert(listed && *listed == 2);
    assert(keys[0].view() == "alpha");
    assert(keys[1].view() == "save");
    auto removed = co_await context.storage().remove("save");
    assert(removed);
    const std::array<std::byte, 3> small{
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    auto small_set = co_await context.storage().set("save", small);
    assert(small_set);
    finished = true;
    co_return pxa::Result<void>{};
}

struct StorageApp {
    pxa::Result<void> on_start(pxa::Context& context) {
        return context.tasks().start(run(context));
    }
};

PXA_APPLICATION(StorageApp)

static void deliver(std::span<const std::byte> result) {
    std::array<std::byte, 128> event{};
    pxa::wire::put16(event.data(), 6);
    pxa::wire::put16(event.data() + 2, opcode);
    pxa::wire::put64(event.data() + 4, token);
    pxa::wire::put32(event.data() + 12,
                     static_cast<std::uint32_t>(result.size()));
    for (std::size_t i = 0; i < result.size(); ++i)
        event[20 + i] = result[i];
    assert(pxa_app_on_event(
               reinterpret_cast<const std::uint8_t*>(event.data()),
               20 + result.size()) == 1);
}

int main() {
    value.fill(std::byte{0x4a});
    assert(pxa_app_start(nullptr, 0) == 0);
    assert(submits == 1);
    const std::array<std::byte, 4> success{};
    deliver(success);
    assert(submits == 2);

    std::array<std::byte, 11> read{};
    pxa::wire::put16(read.data() + 4, 2);
    pxa::wire::put16(read.data() + 6, 3);
    read[8] = std::byte{'a'};
    read[9] = std::byte{'b'};
    read[10] = std::byte{'c'};
    deliver(read);
    assert(submits == 3);

    std::array<std::byte, 21> listed{};
    pxa::wire::put16(listed.data() + 4, 1);
    pxa::wire::put16(listed.data() + 6, 5);
    for (std::size_t i = 0; i < 5; ++i)
        listed[8 + i] = std::byte("alpha"[i]);
    pxa::wire::put16(listed.data() + 13, 1);
    pxa::wire::put16(listed.data() + 15, 4);
    for (std::size_t i = 0; i < 4; ++i)
        listed[17 + i] = std::byte("save"[i]);
    deliver(listed);
    assert(submits == 4);
    deliver(success);
    assert(submits == 5);
    deliver(success);
    assert(finished);
    pxa_app_stop(0);
}

#include <pxa/storage.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>

static std::string_view expected_key;
static std::span<const std::byte> expected_value;
static const std::byte* expected_packet;
static std::uint16_t expected_opcode;
static std::uint64_t token;
static unsigned submits, cancels, completed;
static std::int32_t submit_result;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t size) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    if (pxa::wire::get16(bytes) == 1) {
        assert(pxa::wire::get16(bytes + 2) == 1 && size == 28);
        ++cancels;
        return 0;
    }
    assert(pxa::wire::get16(bytes) == 6);
    assert(pxa::wire::get16(bytes + 2) == expected_opcode);
    token = pxa::wire::get64(bytes + 4);
    assert(token && pxa::wire::get32(bytes + 12) == size - 20);
    assert(pxa::task_pool_stats().active_slots == 2);
    if (expected_packet) assert(bytes == expected_packet);
    const auto key_bytes = expected_key.empty() ? 0u : 4u + expected_key.size();
    if (key_bytes) {
        assert(pxa::wire::get16(bytes + 20) == 1);
        assert(pxa::wire::get16(bytes + 22) == expected_key.size());
        assert(std::string_view(reinterpret_cast<const char*>(bytes + 24),
                                expected_key.size()) == expected_key);
    }
    if (expected_opcode == 2) {
        const auto* record = bytes + 20 + key_bytes;
        assert(pxa::wire::get16(record) == 2);
        assert(pxa::wire::get16(record + 2) == expected_value.size());
        assert(size == 24 + key_bytes + expected_value.size());
        assert(std::equal(expected_value.begin(), expected_value.end(), record + 4));
    } else assert(size == 20 + key_bytes);
    ++submits;
    return submit_result;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) {
    assert(false); return -1;
}

template<class T>
static pxa::Task<void> consume(pxa::Task<T> task, pxa::Result<T> expected) {
    auto result = co_await std::move(task);
    assert(bool(result) == bool(expected));
    if (!result) assert(result.error() == expected.error());
    else if constexpr (!std::is_void_v<T>) assert(*result == *expected);
    ++completed;
    co_return pxa::Result<void>{};
}

int main() {
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::RequestTable requests;
    pxa::StorageService storage(transport, requests);
    pxa::TaskScope tasks;
    const auto reserved = pxa::task_pool_stats().reserved_bytes;
    const std::array<std::byte, 4> success{};
    std::array<std::byte, 8> output;
    std::array<pxa::StorageKey, 14> keys;
    std::array<std::byte, 2200> packet;

    auto finish = [&](std::span<const std::byte> body) {
        const auto before = completed;
        assert(requests.dispatch({6, expected_opcode, token, body}));
        tasks.reap();
        assert(completed == before + 1 && pxa::task_pool_stats().active_slots == 0);
    };
    auto value_body = [](std::span<const std::byte> value) {
        std::array<std::byte, 2056> body{};
        pxa::wire::put16(body.data() + 4, 2);
        pxa::wire::put16(body.data() + 6, value.size());
        std::copy(value.begin(), value.end(), body.begin() + 8);
        return body;
    };

    expected_key = "owned.get.key.long.enough.to.leave.the.sso";
    expected_opcode = 1;
    auto get = [&] {
        std::string key(expected_key);
        return pxa::StorageService(transport, requests).get(key, output);
    }();
    assert(tasks.start(consume(std::move(get), pxa::Result<std::size_t>{3})));
    const std::array<std::byte, 3> abc{std::byte{'a'},std::byte{'b'},std::byte{'c'}};
    auto body = value_body(abc);
    finish(std::span{body}.first(11));
    assert(std::equal(abc.begin(), abc.end(), output.begin()));

    expected_key = "owned.set"; expected_opcode = 2; expected_value = abc;
    auto set = [&] {
        std::string key(expected_key);
        auto value = abc;
        auto pending = pxa::StorageService(transport, requests).set(key, value);
        key.assign("changed"); value.fill(std::byte{0xff});
        return pending;
    }();
    assert(tasks.start(consume(std::move(set), pxa::Result<void>{})));
    finish(success);

    expected_key = "external.set"; expected_packet = packet.data();
    auto external = [&] {
        std::string key(expected_key);
        auto value = abc;
        auto pending = pxa::StorageService(transport, requests).set(key, value, packet);
        key.assign("changed"); value.fill(std::byte{0xff});
        return pending;
    }();
    assert(tasks.start(consume(std::move(external), pxa::Result<void>{})));
    finish(success);

    // Both inputs overlap the external packet, and the value moves right.
    expected_key = "alias";
    std::memcpy(packet.data(), expected_key.data(), expected_key.size());
    std::copy(abc.begin(), abc.end(), packet.begin() + 5);
    auto alias = storage.set({reinterpret_cast<const char*>(packet.data()),5},
                             std::span{packet}.subspan(5,3), packet);
    assert(tasks.start(consume(std::move(alias), pxa::Result<void>{})));
    finish(success); expected_packet = nullptr;

    expected_key = "owned.remove.key.long.enough.to.leave.sso"; expected_opcode = 3;
    auto remove = [&] {
        std::string key(expected_key);
        return pxa::StorageService(transport, requests).remove(key);
    }();
    assert(tasks.start(consume(std::move(remove), pxa::Result<void>{})));
    finish(success);

    expected_key = "alpha"; expected_opcode = 4;
    auto list = [&] {
        std::string cursor(expected_key);
        return pxa::StorageService(transport, requests).list(cursor, keys);
    }();
    assert(tasks.start(consume(std::move(list), pxa::Result<std::size_t>{1})));
    const std::array<std::byte, 12> listed{std::byte{},std::byte{},std::byte{},std::byte{},
        std::byte{1},std::byte{},std::byte{4},std::byte{},
        std::byte{'s'},std::byte{'a'},std::byte{'v'},std::byte{'e'}};
    finish(listed); assert(keys[0].view() == "save");

    // Inclusive/backwards pagination, duplicates and insufficient output are
    // rejected before overwriting any previously returned keys.
    expected_key = "save";
    assert(tasks.start(consume(storage.list(expected_key, keys),
                              pxa::Result<std::size_t>{std::unexpected(pxa::Error::protocol_error)})));
    finish(listed); assert(keys[0].view() == "save");
    expected_key = {};
    auto duplicate = std::array<std::byte, 20>{};
    std::copy(listed.begin(), listed.end(), duplicate.begin());
    std::copy(listed.begin()+4, listed.end(), duplicate.begin()+12);
    assert(tasks.start(consume(storage.list({}, keys),
                              pxa::Result<std::size_t>{std::unexpected(pxa::Error::protocol_error)})));
    finish(duplicate); assert(keys[0].view() == "save");
    assert(tasks.start(consume(storage.list({}, {}),
                              pxa::Result<std::size_t>{std::unexpected(pxa::Error::resource_limit)})));
    finish(listed);
    assert(tasks.start(consume(storage.list({}, keys), pxa::Result<std::size_t>{0})));
    finish(success);

    expected_key = "save"; expected_opcode = 1;
    output.fill(std::byte{0x55});
    assert(tasks.start(consume(storage.get(expected_key, std::span{output}.first(2)),
                              pxa::Result<std::size_t>{std::unexpected(pxa::Error::resource_limit)})));
    finish(std::span{body}.first(11)); assert(output[0] == std::byte{0x55});
    assert(tasks.start(consume(storage.get(expected_key, output),
                              pxa::Result<std::size_t>{std::unexpected(pxa::Error::protocol_error)})));
    body[6] = std::byte{4};
    finish(std::span{body}.first(11)); assert(output[0] == std::byte{0x55});
    assert(tasks.start(consume(storage.get(expected_key, output), pxa::Result<std::size_t>{0})));
    body = value_body({}); finish(std::span{body}.first(8));

    // Default packet's exact boundary and maximum external value/key.
    std::array<std::byte, 2049> large;
    large.fill(std::byte{0xaa});
    expected_key = "a"; expected_opcode = 2; expected_value = std::span{large}.first(483);
    assert(tasks.start(consume(storage.set(expected_key, expected_value), pxa::Result<void>{})));
    finish(success);
    const std::string max_key(64,'a');
    expected_key = max_key; expected_value = std::span{large}.first(2048);
    expected_packet = packet.data();
    assert(tasks.start(consume(storage.set(expected_key, expected_value, packet), pxa::Result<void>{})));
    finish(success); expected_packet = nullptr;

    // Immediate failures allocate no service coroutine and leave packet intact.
    auto failed = [&](auto task, pxa::Error error) {
        assert(pxa::task_pool_stats().active_slots == 0 && !task.valid() && task.failure() == error);
    };
    packet.fill(std::byte{0x55});
    failed(storage.set("a", std::span{large}.first(484)), pxa::Error::resource_limit);
    failed(storage.set("a", large, packet), pxa::Error::invalid_argument);
    failed(storage.set("bad/key", abc, packet), pxa::Error::invalid_argument);
    failed(storage.set("a", abc, std::span{packet}.first(31)), pxa::Error::resource_limit);
    failed(storage.get("", output), pxa::Error::invalid_argument);
    failed(storage.remove(std::string(65,'a')), pxa::Error::invalid_argument);
    failed(storage.list("bad/key", keys), pxa::Error::invalid_argument);
    assert(std::all_of(packet.begin(),packet.end(),[](auto b){return b==std::byte{0x55};}));

    expected_key = "denied"; expected_value = abc;
    submit_result = static_cast<int>(pxa::Error::denied);
    assert(tasks.start(consume(storage.set(expected_key, abc),
                              pxa::Result<void>{std::unexpected(pxa::Error::denied)})));
    assert(pxa::task_pool_stats().active_slots == 0); submit_result = 0;

    assert(tasks.start(consume(storage.set(expected_key, abc), pxa::Result<void>{})));
    tasks.cancel();
    assert(cancels == 1 && pxa::task_pool_stats().active_slots == 0);
    assert(requests.dispatch({6,2,token,success})); // Late reply consumes tombstone safely.
    assert(!requests.dispatch({6,2,token,success}));
    const auto stats = pxa::task_pool_stats();
    assert(stats.peak_slots == 2 && stats.allocation_failures == 0 && stats.reserved_bytes == reserved);
    std::printf("Storage ownership/boundaries/aliasing/cancel OK: %u submits, peak %u slots, reserved %zu B\n",
                submits, stats.peak_slots, reserved);
}

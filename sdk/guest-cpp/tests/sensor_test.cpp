#include <pxa/sensor.hpp>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <type_traits>

static std::uint64_t token;
static std::uint16_t opcode;
static unsigned submits, closes, cancels;
static pxa::Error failure;
static bool done;
static pxa::SensorSubscription subscription;
static std::array<pxa::SensorDescriptor, 2> descriptors;
static unsigned allocations;
void* operator new(std::size_t size) {
    ++allocations;
    if (auto* pointer = std::malloc(size)) return pointer;
    std::abort();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t size) {
    const auto bytes = reinterpret_cast<const std::byte*>(data);
    const auto service = pxa::wire::get16(bytes);
    if (service == 1) {
        if (pxa::wire::get16(bytes + 2) == 1) ++cancels;
        else ++closes;
        return 0;
    }
    assert(service == 8);
    ++submits;
    opcode = pxa::wire::get16(bytes + 2);
    token = pxa::wire::get64(bytes + 4);
    if (opcode == 1) assert(size == 20);
    else {
        assert(opcode == 2 && size == 46);
        assert(pxa::wire::get16(bytes + 24) == 1);
        assert(pxa::wire::get32(bytes + 30) == 100);
        assert(pxa::wire::get64(bytes + 38) == 0x100000003);
    }
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
    std::uint8_t*, std::uint32_t) { assert(false); return -1; }

static std::size_t descriptor_bytes(std::span<std::byte> output,
    std::uint16_t id, std::string_view semantic, std::uint8_t dimensions = 1) {
    std::array<std::byte, 120> nested{};
    pxa::wire::Writer writer(nested);
    std::array<std::byte, 4> value{};
    pxa::wire::put16(value.data(), id);
    assert(pxa::wire::record(writer, 1, std::span(value).first(2)));
    assert(pxa::wire::record(writer, 2, std::as_bytes(std::span(semantic.data(), semantic.size()))));
    pxa::wire::put16(value.data(), 1);
    assert(pxa::wire::record(writer, 3, std::span(value).first(2)));
    value[0] = std::byte{dimensions};
    assert(pxa::wire::record(writer, 4, std::span(value).first(1)));
    pxa::wire::put32(value.data(), 50);
    assert(pxa::wire::record(writer, 5, value));
    pxa::wire::put32(value.data(), 1000);
    assert(pxa::wire::record(writer, 6, value));
    pxa::wire::Writer outer(output);
    assert(pxa::wire::record(outer, 1, std::span(nested).first(writer.size())));
    return outer.size();
}
static pxa::Task<void> list(pxa::SensorService service) {
    auto result = co_await service.list(descriptors);
    assert(result && *result == 1);
    done = true;
    co_return pxa::Result<void>{};
}
static pxa::Task<void> subscribe(pxa::Task<pxa::SensorSubscription> task) {
    auto result = co_await std::move(task);
    if (result) subscription = std::move(*result);
    else failure = result.error();
    done = true;
    co_return pxa::Result<void>{};
}
static std::size_t sample_bytes(std::span<std::byte> output,
    std::uint64_t handle, std::uint8_t dimensions = 1, std::uint16_t count = 1) {
    pxa::wire::Writer writer(output);
    std::array<std::byte, 12> value{};
    pxa::wire::put64(value.data(), handle);
    assert(pxa::wire::record(writer, 4, std::span(value).first(8)));
    pxa::wire::put64(value.data(), 987654);
    assert(pxa::wire::record(writer, 2, std::span(value).first(8)));
    pxa::wire::put16(value.data(), count);
    assert(pxa::wire::record(writer, 3, std::span(value).first(2)));
    pxa::wire::put32(value.data(), static_cast<std::uint32_t>(-25000));
    pxa::wire::put32(value.data() + 4, INT32_MIN);
    pxa::wire::put32(value.data() + 8, INT32_MAX);
    assert(pxa::wire::record(writer, 4, std::span(value).first(4 * dimensions)));
    return writer.size();
}

int main() {
    static_assert(!std::is_copy_constructible_v<pxa::SensorSubscription>);
    static_assert(std::is_nothrow_move_constructible_v<pxa::SensorSubscription>);
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::RequestTable requests;
    pxa::TaskScope tasks;
    std::array<std::byte, 512> bytes{};
    auto size = 4 + descriptor_bytes(std::span(bytes).subspan(4), 1, "ambient.temperature");
    assert(tasks.start(list(pxa::SensorService(transport, requests))));
    assert(submits == 1 && !done);
    assert(requests.dispatch({8, 1, token, std::span(bytes).first(size)}));
    tasks.reap();
    assert(done && descriptors[0].semantic.view() == "ambient.temperature");
    auto original = bytes;
    for (std::size_t i = 5; i < size; ++i)
        assert(!pxa::decode_sensor_list(std::span(bytes).first(i), descriptors));
    auto limited = pxa::decode_sensor_list(std::span(bytes).first(size), {});
    assert(!limited && limited.error() == pxa::Error::resource_limit);
    descriptors[0].id = 99;
    auto duplicate_size = size + descriptor_bytes(std::span(bytes).subspan(size), 1, "other.sensor");
    assert(!pxa::decode_sensor_list(std::span(bytes).first(duplicate_size), descriptors));
    assert(descriptors[0].id == 99);
    duplicate_size = size + descriptor_bytes(std::span(bytes).subspan(size), 2, "ambient.temperature");
    assert(!pxa::decode_sensor_list(std::span(bytes).first(duplicate_size), descriptors));
    assert(descriptors[0].id == 99);
    auto bad_size = 4 + descriptor_bytes(std::span(bytes).subspan(4), 1, "Bad.Sensor");
    assert(!pxa::decode_sensor_list(std::span(bytes).first(bad_size), descriptors));
    bad_size = 4 + descriptor_bytes(std::span(bytes).subspan(4), 1, "ambient.temperature", 4);
    assert(!pxa::decode_sensor_list(std::span(bytes).first(bad_size), descriptors));
    bytes = original;
    assert(pxa::decode_sensor_list(std::span(bytes).first(size), descriptors));
    bytes.fill(std::byte{});
    assert(descriptors[0].semantic.view() == "ambient.temperature");

    pxa::Permission permission(transport, 0x100000003);
    done = false;
    auto invalid = pxa::SensorService(transport, requests).subscribe(descriptors[0], 49, permission);
    assert(tasks.start(subscribe(std::move(invalid))));
    assert(done && failure == pxa::Error::invalid_argument && submits == 1);
    auto lazy = pxa::SensorService(transport, requests).subscribe(descriptors[0], 100, permission);
    descriptors[0].id = 99;
    done = false;
    assert(tasks.start(subscribe(std::move(lazy))));
    assert(submits == 2 && !done);
    std::array<std::byte, 12> result{};
    pxa::wire::put64(result.data() + 4, 0x200000009);
    assert(requests.dispatch({8, 2, token, result}));
    tasks.reap();
    assert(done && subscription.handle() == 0x200000009);
    size = sample_bytes(bytes, subscription.handle());
    pxa::Event event{8, 0x8001, 0, std::span(bytes).first(size)};
    auto sample = subscription.sample(event);
    assert(sample && sample->values[0] == -25000 && sample->timestamp_us == 987654);
    for (std::size_t i = 0; i < size; ++i) {
        event.payload = std::span(bytes).first(i);
        assert(!subscription.sample(event));
    }
    size = sample_bytes(bytes, subscription.handle(), 3);
    event.payload = std::span(bytes).first(size);
    assert(!subscription.sample(event));
    sample = pxa::decode_sensor_sample(event);
    assert(sample && sample->values[1] == INT32_MIN && sample->values[2] == INT32_MAX);
    size = sample_bytes(bytes, subscription.handle(), 1, 2);
    event.payload = std::span(bytes).first(size);
    assert(!pxa::decode_sensor_sample(event));
    size = sample_bytes(bytes, 0x100000004);
    event.payload = std::span(bytes).first(size);
    auto mismatch = subscription.sample(event);
    assert(!mismatch && mismatch.error() == pxa::Error::not_found);
    auto moved = std::move(subscription);
    assert(!subscription && moved);
    moved.reset();
    assert(closes == 1);

    descriptors[0].id = 1;
    done = false;
    assert(tasks.start(subscribe(pxa::SensorService(transport, requests).subscribe(
        descriptors[0], 100, permission))));
    const auto late_token = token;
    tasks.cancel();
    assert(cancels == 1 && !done);
    assert(requests.dispatch({8, 2, late_token, result}));
    assert(closes == 2 && !requests.dispatch({8, 2, late_token, result}));
    assert(tasks.start(subscribe(pxa::SensorService(transport, requests).subscribe(
        descriptors[0], 100, permission))));
    const auto stopped_token = token;
    transport.phase(pxa::Phase::stopped);
    tasks.cancel();
    permission.reset();
    assert(cancels == 1 && closes == 2);
    assert(!requests.dispatch({8, 2, stopped_token, result}));
    assert(allocations == 0);
}

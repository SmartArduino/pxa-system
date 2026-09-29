#include <pxa/device.hpp>
#include "device_runtime_info_golden.hpp"

#include <cassert>
#include <algorithm>

static std::uint64_t token;
static std::uint16_t opcode;
static unsigned submits, closes;
static std::int32_t submit_status;
static pxa::DeviceRuntimeInfo info;
static pxa::DeviceMac mac{pxa::MacKind::wifi_station_hardware};
static pxa::Error failure;
static bool done;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t size) {
    auto bytes = reinterpret_cast<const std::byte*>(data);
    if (pxa::wire::get16(bytes) == 1) {
        assert(pxa::wire::get16(bytes + 2) == 2);
        ++closes;
        return 0;
    }
    assert(pxa::wire::get16(bytes) == 15);
    opcode = pxa::wire::get16(bytes + 2);
    token = pxa::wire::get64(bytes + 4);
    ++submits;
    if (opcode == 1) {
        assert(size == 38);
        assert(pxa::wire::get16(bytes + 24) == 1);
        assert(pxa::wire::get64(bytes + 30) == 0x100000007);
    } else assert(opcode == 2 && size == 20);
    return submit_status;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
    std::uint8_t*, std::uint32_t) { assert(false); return -1; }

static std::span<const std::byte> text(std::string_view value) {
    return std::as_bytes(std::span(value.data(), value.size()));
}
static pxa::Task<void> capture(pxa::Task<pxa::DeviceRuntimeInfo> task) {
    auto result = co_await std::move(task);
    if (result) info = *result;
    else failure = result.error();
    done = true;
    co_return pxa::Result<void>{};
}
static pxa::Task<void> capture_mac(pxa::Task<pxa::DeviceMac> task) {
    auto result = co_await std::move(task);
    if (result) mac = *result;
    else failure = result.error();
    done = true;
    co_return pxa::Result<void>{};
}

int main() {
    auto golden = pxa::decode_device_runtime_info(device_info_golden);
    assert(golden && golden->target.view() == "linux" &&
           golden->architecture.view() == "x86_64" && golden->engine.view() == "wamr" &&
           golden->engine_abi.view() == "abi" && golden->formats == 3);
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::RequestTable requests;
    pxa::TaskScope tasks;
    // The facade is gone before this lazy task starts.
    auto lazy = pxa::DeviceService(transport, requests).runtime_info();
    assert(tasks.start(capture(std::move(lazy))));
    assert(submits == 1 && !done);
    std::array<std::byte, 256> bytes{};
    pxa::wire::Writer writer(std::span(bytes).subspan(4));
    assert(pxa::wire::record(writer, 1, text("linux-x86_64")));
    assert(pxa::wire::record(writer, 2, text("x86_64")));
    assert(pxa::wire::record(writer, 3, text("wamr")));
    assert(pxa::wire::record(writer, 4, text("wamr-aot-v8")));
    const std::array<std::byte, 4> formats{std::byte{3}};
    assert(pxa::wire::record(writer, 5, formats));
    const auto payload = std::span(bytes).first(4 + writer.size());
    assert(requests.dispatch({15, 2, token, payload}));
    tasks.reap();
    assert(done && info.target.view() == "linux-x86_64" && info.formats == 3);
    assert(info.engine_abi.view() == "wamr-aot-v8");

    const auto original = bytes;
    for (std::size_t size = 0; size < payload.size(); ++size)
        assert(!pxa::decode_device_runtime_info(std::span(bytes).first(size)));
    bytes[8] = std::byte{0};
    assert(!pxa::decode_device_runtime_info(payload));
    bytes = original;
    bytes[8] = std::byte{0xc0};
    bytes[9] = std::byte{0x80};
    assert(!pxa::decode_device_runtime_info(payload));
    bytes = original;
    bytes[6] = std::byte{32};
    assert(!pxa::decode_device_runtime_info(payload));
    bytes.fill(std::byte{0xff});
    assert(info.target.view() == "linux-x86_64");
    assert(!pxa::decode_device_runtime_info(std::span(bytes).first(4)));

    pxa::Permission permission(transport, 0x100000007);
    auto pending = pxa::DeviceService(transport, requests).mac(
        pxa::MacKind::wifi_station_hardware, permission);
    // Permission identity is captured immediately; Host still verifies authority.
    permission.reset();
    done = false;
    assert(tasks.start(capture_mac(std::move(pending))));
    assert(submits == 2 && opcode == 1 && !done);
    bytes.fill(std::byte{});
    pxa::wire::Writer mac_writer(std::span(bytes).subspan(4));
    const std::array<std::byte, 2> kind{std::byte{1}};
    const std::array<std::byte, 6> address{
        std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}};
    const std::array<std::byte, 4> flags{std::byte{5}};
    assert(pxa::wire::record(mac_writer, 1, kind));
    assert(pxa::wire::record(mac_writer, 2, address));
    assert(pxa::wire::record(mac_writer, 3, flags));
    assert(requests.dispatch({15, 1, token, std::span(bytes).first(4 + mac_writer.size())}));
    tasks.reap();
    assert(done && mac.address == address && mac.has(pxa::MacFlags::hardware));
    assert(mac.has(pxa::MacFlags::locally_administered));
    done = false;
    assert(tasks.start(capture_mac(pxa::DeviceService(transport, requests).mac(
        static_cast<pxa::MacKind>(0), permission))));
    assert(done && failure == pxa::Error::invalid_argument && submits == 2);

    submit_status = static_cast<std::int32_t>(pxa::Error::unavailable);
    done = false;
    assert(tasks.start(capture(pxa::DeviceService(transport, requests).runtime_info())));
    assert(done && failure == pxa::Error::unavailable);
    transport.phase(pxa::Phase::stopped);
    const auto before = submits;
    assert(tasks.start(capture(pxa::DeviceService(transport, requests).runtime_info())));
    assert(failure == pxa::Error::bad_state && before == submits && closes == 1);
}

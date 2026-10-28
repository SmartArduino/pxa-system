#include <pxa/app.hpp>
#include <pxa/image_frames.hpp>
#include <pxa/ime.hpp>
#include <pxa/ui_transport.hpp>
#include <pxa/events.hpp>
#include "../../guest-c/include/pxa_audio.h"
#include "../../guest-c/include/pxa_canvas.h"
#include "../../guest-c/include/pxa_clock.h"
#include "../../guest-c/include/pxa_http.h"
#include "../../guest-c/include/pxa_image_decode.h"
#include "../../guest-c/include/pxa_ime.h"
#include "../../guest-c/include/pxa_lease.h"
#include "../../guest-c/include/pxa_store_installer.h"
#include "../../guest-c/include/pxa_surface.h"
#include "../../guest-c/include/pxa_ui.h"
#include "../../guest-c/include/pxa_window.h"

#include <cassert>
#include <string>
#include <vector>

static constexpr std::uint64_t handle = 0xaabbccdd00000007ull;
static std::uint16_t service, opcode;
static std::uint64_t token;
static std::vector<std::byte> payload, io_packet;
static std::vector<std::uint64_t> closed;
static std::uint32_t io_operation;
static std::int32_t call_status, io_status;
static unsigned calls, io_calls;
static bool event_wait;

extern "C" std::int32_t pxa_call(std::uint32_t route, std::uint64_t request,
    const std::uint8_t* input, std::uint32_t size) {
    ++calls; service = route & 0xffff; opcode = route >> 16; token = request;
    payload.resize(size);
    if (size) std::memcpy(payload.data(), input, size);
    if (service == 1 && opcode == 2) closed.push_back(pxa::wire::get64(payload.data()));
    return call_status;
}
extern "C" std::int32_t pxa_io(std::uint64_t resource, std::uint32_t operation,
    std::uint8_t* data, std::uint32_t size) {
    assert(resource == handle);
    ++io_calls; io_operation = operation;
    io_packet.resize(size);
    if (size) std::memcpy(io_packet.data(), data, size);
    if (event_wait && operation == 0x201 && io_status >= 0) {
        assert(size >= 28);
        std::memset(data, 0, 28);
        pxa_store_u16(data, 4); pxa_store_u16(data + 2, 0x8001);
        pxa_store_u32(data + 12, 8); pxa_store_u64(data + 20, 123456);
        return 28;
    }
    return io_status;
}

template<class T>
static pxa::Task<void> collect(pxa::Task<T> task, std::optional<pxa::Result<T>>& output) {
    output.emplace(co_await std::move(task));
    co_return pxa::Result<void>{};
}
template<class T> struct Pending {
    std::optional<pxa::Result<T>> result;
    pxa::TaskScope scope;
    explicit Pending(pxa::Task<T> task) { assert(scope.start(collect(std::move(task), result))); }
};
template<class T>
static void complete(pxa::RequestTable& requests, Pending<T>& pending,
    std::span<const std::byte> bytes) {
    assert(requests.dispatch(pxa::Event{service, opcode, token, bytes}));
    pending.scope.reap();
    assert(pending.result);
}
static void compare(const std::uint8_t* packet, std::uint32_t size) {
    pxa_event_t event;
    assert(pxa_parse_event(packet, size, &event));
    assert(event.service == service && event.opcode == opcode && event.token == token);
    assert(event.payload_size == payload.size());
    assert(payload.empty() || std::memcmp(event.payload, payload.data(), payload.size()) == 0);
}
static auto error_payload(pxa::Error error) {
    std::array<std::byte, 4> bytes;
    pxa::wire::put32(bytes.data(), static_cast<std::uint32_t>(error));
    return bytes;
}

static void test_lease(pxa::Transport& transport, pxa::RequestTable& requests) {
    std::array<std::uint8_t, 64> c{}; std::uint32_t size;
    pxa::LeaseService leases(transport, requests);
    Pending pending(leases.acquire(pxa::LeaseKind::network_transfer, 1200));
    assert(pxa_lease_build_acquire(c.data(), c.size(), token, 4, 1200, &size)); compare(c.data(), size);
    std::array<std::byte, 16> result{};
    pxa::wire::put16(result.data() + 4, 4); pxa::wire::put16(result.data() + 6, 8);
    pxa::wire::put64(result.data() + 8, handle);
    complete(requests, pending, result);
    assert(*pending.result && pending.result->value().handle() == handle);
    assert(pending.result->value().close());

    // Lease replies use a record: cancellation must close at offset 8, not 4.
    Pending abandoned(leases.acquire(pxa::LeaseKind::foreground));
    const auto late_token = token;
    abandoned.scope.cancel();
    const auto before = closed.size();
    assert(requests.dispatch({1, 3, late_token, result}));
    assert(closed.size() == before + 1 && closed.back() == handle);
    Pending malformed(leases.acquire(pxa::LeaseKind::foreground));
    std::array<std::byte, 17> trailing{};
    std::copy(result.begin(), result.end(), trailing.begin());
    complete(requests, malformed, trailing);
    assert(!*malformed.result && malformed.result->error() == pxa::Error::protocol_error);
    assert(closed.back() == handle);
    std::array<std::byte, 12> notice{};
    pxa::wire::put64(notice.data(), handle); pxa::wire::put32(notice.data() + 8, -10);
    pxa::Event event{1, 0x8003, 0, notice};
    assert(event.is<pxa::LeaseRevoked>() && pxa::decode_lease_revoked(event)->handle == handle);
    event.token = 1; assert(!pxa::decode_lease_revoked(event));
}

static void test_resources(pxa::Transport& transport, pxa::RequestTable& requests) {
    pxa::ResourceService resources(transport, requests);
    Pending wait(resources.wait(handle, pxa::Ready::read | pxa::Ready::headers));
    auto saved = payload;
    assert(pxa_resource_wait(token, handle, 5) == 0 && payload == saved);
    std::array<std::byte, 8> ready{}; pxa::wire::put32(ready.data() + 4, 8);
    complete(requests, wait, ready); assert(*wait.result && **wait.result == pxa::Ready::end);
    io_status = 0; assert(resources.poll(handle) && std::to_underlying(*resources.poll(handle)) == 0);
    io_status = 16; assert(resources.poll(handle).error() == pxa::Error::protocol_error);

    pxa::EventPortService ports(transport, requests);
    Pending opened(ports.open());
    std::array<std::byte, 12> granted{}; pxa::wire::put64(granted.data() + 4, handle);
    complete(requests, opened, granted); assert(*opened.result);
    auto port = std::move(opened.result->value());
    std::array<std::byte, 64> buffer{};
    event_wait = true; io_status = 0;
    auto event = port.wait(buffer, 123);
    assert(event && event->is<pxa::ClockTick>() && pxa::decode_clock_tick(*event)->timestamp_us == 123456);
    assert(io_operation == 0x201 && pxa::wire::get64(io_packet.data()) == 123);
    const auto before = io_calls;
    assert(port.wait(buffer, 60'000'001).error() == pxa::Error::limit_exceeded && io_calls == before);
    io_status = -12;
    assert(port.wait(buffer).error() == pxa::Error::timed_out);
    event_wait = false; assert(port.close());
}

static void test_assets(pxa::Transport& transport, pxa::RequestTable& requests) {
    pxa::AssetService assets(transport, requests);
    auto task = [&] { std::string path = "assets/a.pxr"; return assets.prefetch(pxa::AssetKind::image, path); }();
    Pending prefetched(std::move(task));
    std::array<std::uint8_t, 300> c{}; std::uint32_t size;
    assert(pxa_assets_build(c.data(), c.size(), 3, token, "assets/a.pxr", 4, &size)); compare(c.data(), size);
    std::array<std::byte, 24> descriptor{}; descriptor[4] = std::byte{4};
    complete(requests, prefetched, descriptor); assert(*prefetched.result);
    Pending status(assets.status("assets/a.pxr"));
    assert(pxa_assets_build(c.data(), c.size(), 4, token, "assets/a.pxr", 0, &size)); compare(c.data(), size);
    std::array<std::byte, 12> cache{}; cache[4] = std::byte{4}; pxa::wire::put32(cache.data() + 8, -14);
    complete(requests, status, cache);
    assert(*status.result && status.result->value().state == pxa::AssetState::failed && status.result->value().load_status == -14);
    Pending blob(assets.query("assets/map.bin")); descriptor[4] = std::byte{5};
    complete(requests, blob, descriptor); assert(*blob.result && blob.result->value().kind == pxa::AssetKind::blob);
    Pending bad(assets.status("assets/a.pxr")); cache[5] = std::byte{1};
    complete(requests, bad, cache); assert(bad.result->error() == pxa::Error::protocol_error);
    Pending wrong_kind(assets.prefetch(pxa::AssetKind::image, "assets/a.pxr"));
    complete(requests, wrong_kind, descriptor);
    assert(wrong_kind.result->error() == pxa::Error::protocol_error);

    struct Source {
        pxa::Transport* transport;
        pxa::ReadableResource readable() const noexcept { return {transport, handle, 3}; }
    };
    Pending image(assets.images().decode(Source{&transport}));
    pxa_image_animation_limits_t animation{};
    auto options = pxa_image_decode_defaults();
    assert(pxa_image_decode_build(c.data(), c.size(), token, handle, 3, options, &animation, &size)); compare(c.data(), size);
    std::array<std::byte, 52> frame{}; pxa::wire::put64(frame.data() + 4, handle);
    frame[12] = std::byte{4}; pxa::wire::put16(frame.data() + 16, 16); pxa::wire::put16(frame.data() + 18, 16);
    pxa::wire::put16(frame.data() + 32, 100); pxa::wire::put16(frame.data() + 34, 100);
    pxa::wire::put32(frame.data() + 36, 4096);
    complete(requests, image, frame); assert(*image.result);
    Pending next(image.result->value().advance());
    assert(pxa_image_next_frame(token, handle) == 0);
    frame[48] = std::byte{1};
    complete(requests, next, frame); assert(*next.result && **next.result);
    assert(image.result->value().frame_info().last);
}

static void test_http_ime(pxa::Transport& transport, pxa::RequestTable& requests) {
    std::array<std::uint8_t, 4096> c{}; std::uint32_t size;
    pxa::Permission permission(transport, handle);
    const std::string_view url = "https://example.test/upload";
    pxa::NetHeaderView field{"X-Test", "ok"};
    const std::array<std::string_view, 2> wanted{"ETag", "Content-Type"};
    auto request = pxa::HttpRequest{.url = url, .method = pxa::HttpMethod::post,
        .headers = {&field, 1}, .wanted_headers = wanted,
        .body = std::as_bytes(std::span("hi", 2)), .options = {.max_body_bytes = 1ull << 40}};
    auto encoded = pxa::encode_http_request(request, permission); assert(encoded);
    auto cr = pxa_http_request_defaults();
    pxa_net_header_t header{"X-Test", 6, reinterpret_cast<const std::uint8_t*>("ok"), 2};
    const char* wanted_names[] = {"ETag", "Content-Type"}; const std::uint16_t wanted_sizes[] = {4, 12};
    cr.url = url.data(); cr.url_size = url.size(); cr.method = 3; cr.permission_handle = handle;
    cr.headers = &header; cr.header_count = 1; cr.wanted_headers = wanted_names;
    cr.wanted_header_sizes = wanted_sizes; cr.wanted_header_count = 2;
    cr.body = reinterpret_cast<const std::uint8_t*>("hi"); cr.body_size = 2; cr.max_body_bytes = 1ull << 40;
    assert(pxa_http_build_open(c.data(), c.size(), 1, &cr, &size));
    assert(encoded->size == size - 20 && std::memcmp(encoded->storage.get(), c.data() + 20, size - 20) == 0);

    std::array<std::byte, 26> input{};
    pxa::wire::put64(input.data(), handle); pxa::wire::put32(input.data() + 8, 7);
    pxa::wire::put16(input.data() + 12, 2); input[24] = std::byte{'n'}; input[25] = std::byte{'i'};
    auto value = pxa::ime::decode_input({22, 0x8001, 0, input}); assert(value);
    std::array<std::byte, 64> cpp_storage{};
    pxa::ime::Frame frame(cpp_storage, *value);
    assert(frame.composition("ni") && frame.candidate("你") && frame.spelling("ni"));
    frame.page(3, true); assert(frame.present(transport));
    auto expected = payload;
    pxa_ime_input_t ci{}; ci.session = handle; ci.sequence = 7;
    pxa_ime_frame_t cf{};
    assert(pxa_ime_frame_begin(&cf, c.data(), c.size(), &ci, "ni", 2, 0, nullptr, 0) == 0);
    assert(pxa_ime_frame_candidate(&cf, "你", 3) == 0 && pxa_ime_frame_spelling(&cf, "ni", 2) == 0);
    assert(pxa_ime_frame_present(&cf, 3, 1, 0) == 0 && payload == expected);

    pxa::AudioSession audio(transport, requests, handle, {16000, 1, 20});
    std::array<pxa::Tone, 2> tones{{{440, 80, -256, pxa::Waveform::sine, 2, 5, 0},
        {880, 40, -512, pxa::Waveform::square, 1, 3, 20}}};
    io_status = 32;
    assert(audio.effect(tones, pxa::EffectTrack::track2));
    auto effect = io_packet;
    pxa_audio_tone_t ct[] = {{440, 80, -256, 0, 2, 5, 0}, {880, 40, -512, 1, 1, 3, 20}};
    assert(pxa_audio_play_effect(handle, ct, 2, 2) == 32 && io_packet == effect);
    io_status = 4;
    assert(audio.stop_effect(pxa::EffectTrack::track2)); effect = io_packet;
    assert(pxa_audio_stop_effect(handle, 2) == 4 && io_packet == effect);
}

static void test_store(pxa::Transport& transport, pxa::RequestTable& requests) {
    pxa::StoreInstallerService store(transport, requests);
    constexpr auto digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    auto task = [&] {
        std::string app = "test.app", ticket = "ticket";
        return store.download({app, ticket, digest, 2048});
    }();
    Pending download(std::move(task));
    auto expected = payload;
    std::array<std::uint8_t, 400> c{};
    assert(pxa_store_download(token, "test.app", "ticket", digest, 2048, c.data(), c.size()) && payload == expected);
    std::array<std::byte, 23> filename{};
    std::memcpy(filename.data() + 4, ".store-1234abcd.pxa", 19);
    complete(requests, download, filename);
    assert(*download.result && download.result->value().view() == ".store-1234abcd.pxa");
    Pending install(store.install({"test.app", "ticket", digest, 2048}));
    expected = payload;
    assert(pxa_store_install(token, "test.app", "ticket", digest, 2048, c.data(), c.size()) && payload == expected);
    std::array<std::byte, 4> ok{}; complete(requests, install, ok); assert(*install.result);
    Pending denied(store.uninstall("test.app")); auto error = error_payload(pxa::Error::denied);
    complete(requests, denied, error); assert(denied.result->error() == pxa::Error::denied);
    std::array<pxa::StoreInstalledApp, 1> apps;
    Pending listed(store.installed(apps));
    std::array<std::byte, 20> list{}; list[4] = std::byte{1};
    list[5] = std::byte{1}; list[6] = std::byte{1}; list[7] = std::byte{1};
    pxa::wire::put64(list.data() + 8, 1ull << 40); list[16] = std::byte{1};
    list[17] = std::byte{'a'}; list[18] = std::byte{'v'}; list[19] = std::byte{'i'};
    complete(requests, listed, list); assert(*listed.result && **listed.result == 1);
    assert(apps[0].app_id.view() == "a" && apps[0].sequence == (1ull << 40) && apps[0].uninstallable);
    std::array<pxa::StoreDownloadEntry, 1> entries;
    Pending downloads(store.downloads(entries));
    std::array<std::byte, 26> entry{}; entry[4] = std::byte{1}; entry[5] = std::byte{1};
    std::memcpy(entry.data() + 6, ".store-1234abcd.pxa", 19); entry[25] = std::byte{'a'};
    complete(requests, downloads, entry); assert(*downloads.result && entries[0].app_id.view() == "a");
    Pending too_small(store.installed({}));
    complete(requests, too_small, list); assert(too_small.result->error() == pxa::Error::resource_limit);
    Pending install_file(store.install_file(".store-1234abcd.pxa"));
    complete(requests, install_file, ok); assert(*install_file.result);
    Pending launch(store.launch("test.app")); complete(requests, launch, ok); assert(*launch.result);
    const std::string full="17982acd08493944059713aee9a56135dd4a080ecaaf7aa77df11fdf3102d171:pxa-nes";
    Pending exact_launch(store.launch_identity(full));
    expected=payload;
    assert(pxa_store_manage(PXA_STORE_LAUNCH_IDENTITY_REQUEST,token,full.c_str(),c.data(),c.size()) && payload==expected);
    complete(requests,exact_launch,ok);assert(*exact_launch.result);
    Pending exact_remove(store.uninstall_identity(full));
    expected=payload;
    assert(pxa_store_manage(PXA_STORE_UNINSTALL_IDENTITY_REQUEST,token,full.c_str(),c.data(),c.size()) && payload==expected);
    complete(requests,exact_remove,ok);assert(*exact_remove.result);
    Pending bound_install(store.install_identity(full,{"pxa-nes","ticket",digest,2048}));
    expected=payload;
    assert(pxa_store_install_identity_status(token,full.c_str(),"ticket",digest,2048,c.data(),c.size())==PXA_STATUS_OK && payload==expected);
    complete(requests,bound_install,ok);assert(*bound_install.result);
    std::array<std::byte,180> proof{};
    Pending bind(store.register_namespace(proof));expected=payload;
    assert(pxa_store_register_namespace_status(token,reinterpret_cast<const uint8_t *>(proof.data()),proof.size(),c.data(),c.size())==PXA_STATUS_OK && payload==expected);
    complete(requests,bind,ok);assert(*bind.result);
    Pending invalid_identity(store.launch_identity("pxa-nes"));
    assert(invalid_identity.result && invalid_identity.result->error()==pxa::Error::invalid_argument);
    Pending remove(store.remove_download(".store-1234abcd.pxa")); complete(requests, remove, ok); assert(*remove.result);
    const auto before = calls;
    Pending invalid(store.install_file("../bad"));
    assert(invalid.result && invalid.result->error() == pxa::Error::invalid_argument && calls == before);
    Pending maximum(store.install({std::string(64, 'a'), std::string(255, 't'), digest, UINT64_C(0x7fffffff)}));
    assert(payload.size() == 362);
    complete(requests, maximum, ok); assert(*maximum.result);
    Pending excessive(store.install({"app", "ticket", digest, UINT64_C(0x80000000)}));
    assert(excessive.result && excessive.result->error() == pxa::Error::limit_exceeded);
    Pending capabilities(store.capabilities());
    std::array<std::byte, 20> capability_bytes{};
    pxa::wire::put16(capability_bytes.data() + 6, 8);
    pxa::wire::put32(capability_bytes.data() + 8, 3);
    pxa::wire::put64(capability_bytes.data() + 12, UINT64_C(0x7fffffff));
    complete(requests, capabilities, capability_bytes);
    assert(*capabilities.result && (**capabilities.result).minor == 8 && (**capabilities.result).features == 3);
    Pending old_capabilities(store.capabilities());
    auto unsupported_capabilities = error_payload(pxa::Error::unsupported);
    complete(requests, old_capabilities, unsupported_capabilities);
    assert(old_capabilities.result->error() == pxa::Error::unsupported);
    std::array<std::byte, 24> progress{};
    pxa::wire::put64(progress.data(), 1ull << 40); pxa::wire::put64(progress.data() + 16, 100);
    assert(pxa::decode_store_download_progress({20, 0x8001, 0, progress})->request_id == (1ull << 40));
}

static void test_surface_window(pxa::Transport& transport, pxa::RequestTable& requests) {
    std::array<std::uint8_t, 128> c{}; std::uint32_t size;
    pxa::SurfaceService surfaces(transport, requests);
    Pending surface(surfaces.create_stream({.width = 2, .height = 2,
        .format = pxa::SurfaceFormat::argb8888_premultiplied}));
    assert(pxa_surface_build_create(c.data(), c.size(), token, {2, 2, 2, 2, 1}, &size)); compare(c.data(), size);
    std::array<std::byte, 24> created{}; pxa::wire::put64(created.data() + 4, handle);
    pxa::wire::put32(created.data() + 12, 8); pxa::wire::put32(created.data() + 16, 16); created[20] = std::byte{2};
    complete(requests, surface, created); assert(*surface.result);
    auto& stream = surface.result->value();
    std::array<std::byte, 16> pixels{}; io_status = 16;
    assert(stream.write(pixels) && io_operation == 2);
    assert(stream.present(1));
    assert(pxa_surface_build_queue_frame(c.data(), c.size(), handle, 1, nullptr, 0, &size)); compare(c.data(), size);
    std::array<pxa::SurfaceRect, 1> rects{{{0, 0, 1, 1}}};
    auto task = stream.opaque_ui_regions(rects); rects[0].width = 0;
    Pending regions(std::move(task)); pxa_surface_rect_t region{0, 0, 1, 1};
    assert(pxa_surface_build_opaque_ui_regions(c.data(), c.size(), token, handle, &region, 1, &size)); compare(c.data(), size);
    std::array<std::byte, 4> ok{}; complete(requests, regions, ok); assert(*regions.result);
    io_status = -7; assert(stream.write(pixels).error() == pxa::Error::would_block);
    assert(stream.close()); assert(stream.write(pixels).error() == pxa::Error::bad_state);
    pxa::WindowService window(transport, requests);
    assert(window.configure({.edge_to_edge = true, .status_bar_mode = pxa::WindowBarMode::transient,
        .status_bar_color = 0xff112233}));
    pxa_window_config_t config{1, 2, 0, 0, 0, 0xff112233, 0};
    assert(pxa_window_build_configure(c.data(), c.size(), &config, &size)); compare(c.data(), size);
    for (bool enabled : {false, true}) {
        assert(window.keep_screen_on(enabled));
        auto wanted = payload;
        assert(service == 2 && opcode == 1 && !token && payload.size() == 5);
        assert(pxa::wire::get16(payload.data()) == 8 && pxa::wire::get16(payload.data() + 2) == 1);
        assert(payload[4] == std::byte(enabled));
        assert(pxa_window_keep_screen_on(enabled) == 0 && payload == wanted);
    }
    assert(pxa_window_keep_screen_on(2) < 0);
    assert(window.request_back()); assert(pxa_window_request_back() == 0);
    pxa::ui::TransportService ui(transport);
    assert(ui.open_surface(7, pxa::ui::SurfaceRole::overlay));
    auto expected = payload; assert(pxa_ui_surface_open(7, 3) && payload == expected);
    assert(ui.open_canvas_stream(9, 1, 2));
    expected = payload; assert(pxa_canvas_stream_open(9, 2) && payload == expected);
}

int main() {
    pxa::Transport transport; transport.phase(pxa::Phase::event);
    pxa::RequestTable requests;
    assert(pxa::cancel(0x1122334455667788ull));
    assert(service == 1 && opcode == 1 && !token && pxa::wire::get64(payload.data()) == 0x1122334455667788ull);
    // Exercise native full-width handles without allocating a Context.
    test_lease(transport, requests); test_resources(transport, requests);
    test_assets(transport, requests); test_http_ime(transport, requests);
    test_store(transport, requests); test_surface_window(transport, requests);
    assert(pxa::task_pool_stats().active_slots == 0);
}

#include <pxa/events.hpp>
#include <pxa/audio.hpp>
#include <pxa/ipc.hpp>
#include <pxa/sensor.hpp>
#include <pxa/surface.hpp>
#include <pxa/ui_input.hpp>
#include <pxa/work.hpp>

#include <cassert>
#include <cstring>
#include <cstdio>

extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { assert(false); return -1; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { assert(false); return -1; }

template<class T, class Decode>
static void verify(pxa::Event event, Decode decode, std::uint64_t bad_token=1) {
    assert(event.is<T>() && decode(event));
    auto changed=event; changed.service=65535; assert(!changed.is<T>() && !decode(changed));
    changed=event; changed.opcode=2; assert(!changed.is<T>() && !decode(changed));
    changed=event; changed.token=bad_token;
    assert(changed.is<T>() && !decode(changed)); // Route corrupt matching messages to the decoder.
    changed=event; changed.payload=event.payload.first(event.payload.size()-1);
    assert(changed.is<T>() && !decode(changed));
}

struct TestContract { using Request=int; };
struct EventLayoutBefore { std::uint16_t service,opcode; std::uint64_t token; std::span<const std::byte> payload; };
static_assert(sizeof(pxa::Event)==sizeof(EventLayoutBefore));
static_assert(alignof(pxa::Event)==alignof(EventLayoutBefore));
static_assert(pxa::Event{8,0x8001,0,{}}.is<pxa::SensorSample>());
static_assert(!pxa::Event{8,1,1,{}}.is<pxa::SensorSample>());
template<class T> concept Routable=requires(pxa::Event event) { event.template is<T>(); };
static_assert(!Routable<int>);

int main() {
    const auto handle=0x1234567800000001ull;
    std::array<std::byte,38> sensor{};
    auto record=[](std::byte* p,std::uint16_t tag,std::uint16_t size) {
        pxa::wire::put16(p,tag); pxa::wire::put16(p+2,size);
    };
    record(sensor.data(),4,8); pxa::wire::put64(sensor.data()+4,handle);
    record(sensor.data()+12,2,8); pxa::wire::put64(sensor.data()+16,100);
    record(sensor.data()+24,3,2); pxa::wire::put16(sensor.data()+28,1);
    record(sensor.data()+30,4,4); pxa::wire::put32(sensor.data()+34,42);
    verify<pxa::SensorSample>({8,0x8001,0,sensor},pxa::decode_sensor_sample);

    std::array<std::byte,20> revoked{};
    record(revoked.data(),1,7); std::memcpy(revoked.data()+4,"example",7);
    record(revoked.data()+11,2,5); std::memcpy(revoked.data()+15,"media",5);
    verify<pxa::PermissionRevoked>({11,0x8001,0,revoked},pxa::decode_permission_revoked);

    std::array<std::byte,24> playback{};
    pxa::wire::put64(playback.data(),handle); pxa::wire::put64(playback.data()+8,1);
    playback[16]=std::byte{1};
    verify<pxa::PlaybackEvent>({10,0x8001,0,playback},pxa::decode_playback);

    std::array<std::byte,20> release{};
    pxa::wire::put64(release.data(),handle); pxa::wire::put64(release.data()+12,1);
    verify<pxa::SurfaceRelease>({16,0x8001,0,release},pxa::decode_surface_release);

    std::array<std::byte,12> stop{};
    pxa::wire::put32(stop.data(),1); pxa::wire::put64(stop.data()+4,100);
    verify<pxa::WorkStopRequested>({13,0x8001,0,stop},pxa::decode_work_stop_requested);

    std::array<std::byte,36> pointer{};
    pxa::wire::put32(pointer.data(),1); pxa::wire::put32(pointer.data()+4,2);
    pxa::wire::put32(pointer.data()+8,3); pxa::wire::put16(pointer.data()+12,7);
    verify<pxa::ui::CanvasPointer>({3,0x8001,0,pointer},pxa::ui::decode_pointer);

    std::array<std::byte,11> ipc{};
    record(ipc.data(),1,7); std::memcpy(ipc.data()+4,"example",7);
    const pxa::Event request{7,0x8001,42,ipc};
    verify<pxa::IpcRequest>(request,[](const auto& event){return pxa::decode_ipc_request(event);},0);
    assert(request.is<pxa::TypedIpcRequest<TestContract>>());
    // An IPC kind match leaves endpoint/schema matching to decode_ipc_request<Contract>.
    assert((!pxa::Event{13,0x8001,0,{}}.is<pxa::TypedIpcRequest<TestContract>>()));
    std::puts("Typed event routing: 7 valid decoder paths, malformed token/payload retained, Event layout unchanged OK");
}

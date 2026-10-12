#include "../../testing/pxa_control_capture.h"
#include <pxa/ime.hpp>
#include <cassert>
static std::array<std::byte,512> packet;
static std::uint32_t packet_size;
extern "C" std::int32_t pxa_test_control(const std::uint8_t* bytes,std::uint32_t size) {
    assert(size<=packet.size()); std::memcpy(packet.data(),bytes,size); packet_size=size; return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t,std::uint32_t,std::uint8_t*,std::uint32_t) { return -3; }
int main() {
    std::array<std::byte,26> input{};
    pxa::wire::put64(input.data(),42); pxa::wire::put32(input.data()+8,7);
    pxa::wire::put16(input.data()+12,2); input[24]=std::byte{'n'}; input[25]=std::byte{'i'};
    pxa::Event event{22,0x8001,0,input};
    auto decoded=pxa::ime::decode_input(event); assert(decoded && decoded->text=="ni");
    event.token=1; assert(!pxa::ime::decode_input(event)); event.token=0;
    input[18]=std::byte{1}; assert(!pxa::ime::decode_input(event)); input[18]=std::byte{};
    input[24]=std::byte{0xff}; assert(!pxa::ime::decode_input(event)); input[24]=std::byte{'n'};
    std::array<std::byte,64> storage;
    pxa::ime::Frame frame(storage,*decoded);
    assert(!frame.candidate("你")); assert(frame.composition("ni"));
    assert(!frame.composition("ni")); assert(frame.candidate("你")); assert(frame.spelling("ni"));
    assert(!frame.candidate("尼")); frame.page(3,true);
    pxa::Transport transport; transport.phase(pxa::Phase::event);
    assert(frame.present(transport)); assert(packet_size==63);
    assert(pxa::wire::get16(packet.data())==22 && pxa::wire::get16(packet.data()+2)==1);
    const auto* body=packet.data()+20;
    assert(pxa::wire::get64(body)==42 && pxa::wire::get32(body+8)==7);
    assert(pxa::wire::get16(body+12)==1 && pxa::wire::get32(body+16)==3);
    assert(pxa::wire::get16(body+24)==1 && pxa::wire::get16(body+26)==1);
    std::array<std::byte,31> tiny;
    pxa::ime::Frame small(tiny,*decoded); assert(!small.composition(""));
    assert(!small.present(transport));
    // Mirror Host admission boundaries; reject locally without making I/O.
    std::array<std::byte,pxa::wire::max_call_payload_bytes+1> oversized{};
    std::copy(input.begin(),input.end(),oversized.begin());
    std::fill(oversized.begin()+26,oversized.end(),std::byte{'a'});
    event.payload=oversized; assert(!pxa::ime::decode_input(event));
    pxa::ime::Frame bounded(storage,*decoded); assert(bounded.composition(""));
    assert(bounded.candidate("你")); bounded.page(UINT32_MAX,false);
    auto previous_size=packet_size;
    auto overflow=bounded.present(transport);
    assert(!overflow && overflow.error()==pxa::Error::invalid_argument && packet_size==previous_size);
    bounded.page(UINT32_MAX-1,false); assert(bounded.present(transport));
    auto empty=bounded.candidate("");
    assert(!empty && empty.error()==pxa::Error::invalid_argument);
}

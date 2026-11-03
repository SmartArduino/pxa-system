// SPDX-License-Identifier: MIT
#include "../../testing/pxa_control_capture.h"
#include <pxa/font.hpp>
#include "../../guest-c/include/pxa_font.h"
#include <cassert>
#include <optional>
static std::uint64_t token;
static unsigned sends,cancels,completed;
static std::optional<pxa::FontGlyph> saved;
extern "C" std::int32_t pxa_test_control(const std::uint8_t* data,std::uint32_t size) {
    const auto* p=reinterpret_cast<const std::byte*>(data);
    if(pxa::wire::get16(p)==1){++cancels;return 0;}
    assert(size==28 && pxa::wire::get16(p)==3 && pxa::wire::get16(p+2)==13);
    assert(pxa::wire::get32(p+20)==U'中' && pxa::wire::get16(p+24)==12 && !pxa::wire::get16(p+26));
    token=pxa::wire::get64(p+4);++sends;return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t,std::uint32_t,std::uint8_t*,std::uint32_t){return -3;}
static pxa::Task<void> run(pxa::FontService font,std::optional<pxa::Error> error={}) {
    auto glyph=co_await font.glyph(U'中',12);
    if(error)assert(!glyph && glyph.error()==*error);
    else {assert(glyph);saved=std::move(*glyph);}
    ++completed;co_return pxa::Result<void>{};
}
int main() {
    std::array<std::byte,26> event{};
    auto* p=event.data()+4;
    pxa::wire::put16(p,12);pxa::wire::put16(p+2,3);pxa::wire::put16(p+4,5);
    pxa::wire::put16(p+6,2);pxa::wire::put16(p+8,3);pxa::wire::put16(p+10,std::uint16_t(-1));
    for(unsigned i=0;i<6;++i)p[16+i]=std::byte(i*40);
    pxa::Transport transport;transport.phase(pxa::Phase::event);
    pxa::RequestTable requests;pxa::TaskScope scope;pxa::FontService font(transport,requests);
    assert(scope.start(run(font)));assert(requests.dispatch({3,13,token,event}));scope.reap();
    assert(saved && saved->line_height==12 && saved->offset_x==-1 && saved->coverage[5]==std::byte{200});
    pxa_font_glyph_t borrowed{};
    pxa_event_t raw={3,13,token,reinterpret_cast<const std::uint8_t*>(event.data()),26};
    assert(pxa_font_parse_glyph(&raw,token,&borrowed) && borrowed.advance==5 && borrowed.coverage[5]==200);
    assert(!pxa_font_parse_glyph(&raw,token+1,&borrowed));
    for(unsigned n=0;n<26;++n){raw.payload_size=n;assert(!pxa_font_parse_glyph(&raw,token,&borrowed));}
    raw.payload_size=26;
    pxa::wire::put16(p+6,65);
    assert(!pxa::decode_font_glyph(std::span{event}.subspan(4)));
    assert(!pxa_font_parse_glyph(&raw,token,&borrowed));pxa::wire::put16(p+6,2);
    assert(scope.start(run(font,pxa::Error::protocol_error)));
    assert(requests.dispatch({3,13,token,std::span{event}.first(25)}));scope.reap();
    pxa::wire::put32(event.data(),std::uint32_t(pxa::Error::unsupported));
    assert(scope.start(run(font,pxa::Error::unsupported)));
    assert(requests.dispatch({3,13,token,std::span{event}.first(4)}));scope.reap();
    assert(scope.start(run(font)));scope.cancel();assert(cancels==1 && completed==3 && sends==4);
    event.fill(std::byte{});assert(saved->coverage[5]==std::byte{200});
    assert(pxa_font_request_glyph(0,'A',12)==PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_font_request_glyph(1,0xd800,12)==PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_font_request_glyph(1,'A',49)==PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa::task_pool_stats().active_slots==0);
}

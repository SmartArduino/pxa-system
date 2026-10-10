#include <pxa/ipc.hpp>
#include <pxa/net.hpp>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <new>
#include <memory>
using namespace pxa;
extern "C" int pxa_submit(const std::uint8_t*,std::uint32_t){std::abort();}
extern "C" int pxa_io(std::uint64_t,std::uint32_t,std::uint8_t*,std::uint32_t){std::abort();}
int main(){
    Transport transport;
    Permission permission(transport,0x100000017);
    alignas(NetRequest) std::array<std::byte,512> packet{};
    auto text=[&](std::size_t at,std::string_view value){
        std::memcpy(packet.data()+at,value.data(),value.size());
        return std::string_view(reinterpret_cast<const char*>(packet.data()+at),value.size());
    };
    auto rejected=[&](const NetRequest& request){
        const auto before=packet;
        auto result=encode_net_request(request,permission,packet);
        assert(!result&&result.error()==Error::invalid_argument&&packet==before);
    };
    rejected({.url=text(20,"https://example.test")});
    rejected({.url="https://example.test",.method=HttpMethod::post,.body=std::span(packet).subspan(20,16)});
    std::array headers{NetHeaderView{text(24,"etag"),"valid"}};
    rejected({.url="https://example.test",.headers=headers});
    headers[0]={"etag",text(24,"value")};
    rejected({.url="https://example.test",.headers=headers});
    std::array wanted{text(24,"etag")};
    rejected({.url="https://example.test",.wanted_headers=wanted});
    auto* views=std::construct_at(reinterpret_cast<NetHeaderView*>(packet.data()),NetHeaderView{"etag","value"});
    rejected({.url="https://example.test",.headers={views,1}});
    std::destroy_at(views);
    auto* request=std::construct_at(reinterpret_cast<NetRequest*>(packet.data()),NetRequest{.url="https://example.test"});
    rejected(*request);
    std::destroy_at(request);
    packet.fill(std::byte{});
    auto endpoint=text(20,"test.echo");
    auto before=packet;
    auto result=encode_ipc_call(endpoint,{},packet);
    assert(!result&&result.error()==Error::invalid_argument&&packet==before);
    result=encode_ipc_call("test.echo",std::span(packet).first(12),packet);
    assert(!result&&result.error()==Error::invalid_argument&&packet==before);
    result=encode_ipc_reply(1,0,std::span(packet).subspan(20,12),packet);
    assert(!result&&result.error()==Error::invalid_argument&&packet==before);
    result=encode_ipc_call_in_place(endpoint,0,packet);
    assert(result&&wire::get16(packet.data()+20)==1&&wire::get16(packet.data()+22)==9);
    assert(std::memcmp(packet.data()+24,"test.echo",9)==0);
#if !defined(PXA_BASELINE)
    assert(!wire::overlaps({},packet));
    assert(!wire::overlaps(std::span(packet).first(4),std::span(packet).subspan(4)));
    assert(wire::overlaps(std::span(packet).first(5),std::span(packet).subspan(4)));
#endif
}

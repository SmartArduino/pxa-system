#include <pxa/game_upload.hpp>
#include <pxa/ui_controller.hpp>
#include <pxa/ui_geometry.hpp>
#include <pxa/raster.h>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>
static int allocations=0,io_calls=0;static uint32_t upload_size=0,draw_size=0;
static std::array<uint8_t,2048> captured{};
void* operator new(size_t n){++allocations;if(auto p=std::malloc(n))return p;std::abort();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
extern "C" int32_t pxa_submit(const uint8_t*,uint32_t){return 0;}
extern "C" int32_t pxa_io(uint64_t handle,uint32_t opcode,uint8_t* bytes,uint32_t size){
    assert(handle==77&&size<=captured.size());++io_calls;std::memcpy(captured.data(),bytes,size);
    if(opcode==0x100)upload_size=size;else{assert(opcode==0x101);draw_size=size;}return size;
}
int main(){
    pxa::Transport transport;transport.phase(pxa::Phase::event);
    pxa::game::Renderer renderer(transport,77,PXA_RASTER_CAP_KNOWN_MASK);
    std::array<std::byte,1044> scratch{};pxa::game::Upload uploader(renderer,scratch);
    for(unsigned i=0;i<64;++i)scratch[i]=std::byte(i);
    // Overlapping input/scratch is explicitly supported, not just exact alias.
    assert(uploader.texture({2},8,8,std::span{scratch}.first(64)));
    pxa_raster_upload_view_t upload{};
    assert(pxa_raster_decode_upload(captured.data(),upload_size,&upload)==PXA_STATUS_OK);
    assert(upload.kind==PXA_RASTER_UPLOAD_TEXTURE_INDEX8&&upload.slot==2);
    for(unsigned i=0;i<64;++i)assert(upload.payload[i]==i);
    const int before=io_calls;
    assert(!uploader.texture({48},8,8,std::span{scratch}.first(64)));
    assert(!uploader.texture({0},8,8,std::span{scratch}.first(63)));
    std::array<uint16_t,512> colors{};colors[256+7]=0x07e0;
    assert(!uploader.palette(colors,3));assert(io_calls==before);
    assert(uploader.palette(colors,2));
    assert(pxa_raster_decode_upload(captured.data(),upload_size,&upload)==PXA_STATUS_OK);
    assert(upload.kind==PXA_RASTER_UPLOAD_LIT_PALETTE_RGB565&&upload.height==2);
    assert(pxa::wire::get16(reinterpret_cast<const std::byte*>(upload.payload)+2*(256+7))==0x07e0);
    pxa::game::DrawBuffer<512> buffer;
    const std::array<pxa::game::Vertex,3> triangle{{{.x_q4=0,.y_q4=0,.light=1,.depth_q8=0},
        {.x_q4=112,.y_q4=0,.light=1,.depth_q8=0},{.x_q4=0,.y_q4=112,.light=1,.depth_q8=0}}};
    auto frame=renderer.frame(buffer);frame.palette_triangles(triangle,7);assert(frame.submit());
    std::array<uint16_t,64> pixels{};pxa_raster_resources_t resources{};
    resources.palette=colors.data();resources.palette_light_levels=2;resources.capabilities=renderer.capabilities();
    pxa_raster_target_t target{};target.pixels=pixels.data();target.width=target.height=8;target.stride_pixels=8;target.scratch_mode=PXA_RASTER_SCRATCH_NONE;
    pxa_raster_draw_list_view_t view{};
    assert(pxa_raster_validate_draw_list(captured.data(),draw_size,&target,&resources,&view)==PXA_STATUS_OK);
    assert(!view.uses_depth&&view.uses_palette);pxa_raster_execute_draw_list(captured.data(),&view,&target,&resources,nullptr);
    assert(pixels[1*8+1]==0x07e0);
    // Fast opaque sprite clipping and row-split execution equal the general
    // transparent path when all texels are nonzero, including partial rows.
    std::array<uint8_t,32> texels{};texels.fill(7);resources.textures[2]={texels.data(),8,4};
    colors[7]=0xf800;
    const pxa::game::Sprite sprite{-2,-1,8,9,0,0,8,4};
    std::array<uint16_t,64> opaque{},generic{},split{};
    auto render_sprite=[&](bool transparent,auto& destination,bool rows){
        auto next=renderer.frame(buffer);next.sprites({2},{&sprite,1},{.transparent_index0=transparent});assert(next.submit());
        target.pixels=destination.data();assert(pxa_raster_validate_draw_list(captured.data(),draw_size,&target,&resources,&view)==PXA_STATUS_OK);
        if(rows){pxa_raster_execute_draw_list_rows(captured.data(),&view,&target,&resources,0,3,nullptr);
            pxa_raster_execute_draw_list_rows(captured.data(),&view,&target,&resources,3,8,nullptr);}
        else pxa_raster_execute_draw_list(captured.data(),&view,&target,&resources,nullptr);
    };
    render_sprite(false,opaque,false);render_sprite(true,generic,false);render_sprite(false,split,true);
    assert(opaque==generic&&opaque==split&&opaque[0]==0xf800&&opaque[7]==0);
    // Repeated opaque tiles use the destination itself as the repeat source.
    // Verify against individual draws, including row splits and right clipping.
    std::array<pxa::game::Sprite,4> repeats{{{0,-2,3,9,0,0,3,4},
        {3,-2,3,9,0,0,3,4},{6,-2,3,9,0,0,3,4},{9,-2,3,9,0,0,3,4}}};
    for(unsigned i=0;i<texels.size();++i)texels[i]=uint8_t(1+i%7);
    for(unsigned i=1;i<8;++i)colors[i]=uint16_t(i*1234);
    opaque.fill(0);generic.fill(0);split.fill(0);
    auto repeated=renderer.frame(buffer);repeated.sprites({2},repeats,{.transparent_index0=false});assert(repeated.submit());
    target.pixels=opaque.data();assert(pxa_raster_validate_draw_list(captured.data(),draw_size,&target,&resources,&view)==PXA_STATUS_OK);
    pxa_raster_execute_draw_list(captured.data(),&view,&target,&resources,nullptr);
    target.pixels=split.data();pxa_raster_execute_draw_list_rows(captured.data(),&view,&target,&resources,0,4,nullptr);
    pxa_raster_execute_draw_list_rows(captured.data(),&view,&target,&resources,4,8,nullptr);
    auto individual=renderer.frame(buffer);for(const auto& tile:repeats)individual.sprites({2},{&tile,1},{.transparent_index0=false});assert(individual.submit());
    target.pixels=generic.data();assert(pxa_raster_validate_draw_list(captured.data(),draw_size,&target,&resources,&view)==PXA_STATUS_OK);
    pxa_raster_execute_draw_list(captured.data(),&view,&target,&resources,nullptr);
    assert(opaque==generic&&opaque==split);
    // Safe rectangles keep every corner inside circular/rounded panel outlines.
    for(auto size:std::array<std::array<uint32_t,2>,5>{{{176,176},{296,240},{480,480},{800,480},{480,800}}}){
        pxa::ui::DisplayMetrics d;d.width=size[0];d.height=size[1];d.density_q16=124928;d.shape=2;
        auto r=pxa::ui::safe_rectangle(d);const double radius=std::min(d.width,d.height)/2.;
        for(int x:{r.x,r.x+r.width})for(int y:{r.y,r.y+r.height}){
            double dx=x-d.width/2.,dy=y-d.height/2.;assert(dx*dx+dy*dy<=radius*radius);}
        auto scaled=pxa::ui::scale_display(d,d.width/2,d.height/2);
        assert(scaled.density_q16==d.density_q16/2);
    }
    std::array<std::byte,32> bytes{};pxa::wire::put32(bytes.data(),1);pxa::wire::put32(bytes.data()+4,2);pxa::wire::put32(bytes.data()+8,3);
    pxa::wire::put16(bytes.data()+12,10);pxa::wire::put16(bytes.data()+14,8);bytes[25]=std::byte{1};
    pxa::wire::put32(bytes.data()+28,pxa::ui::controller_a|pxa::ui::controller_left);
    pxa::Event event{};event.service=3;event.opcode=0x8001;event.payload=bytes;
    auto controller=pxa::ui::decode_controller(event);assert(controller&&controller->buttons==20);
    bytes[25]=std::byte{};assert(!pxa::ui::decode_controller(event));
    bytes[25]=std::byte{1};bytes[26]=std::byte{1};assert(!pxa::ui::decode_controller(event));
    assert(allocations==0);
}

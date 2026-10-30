/* SPDX-License-Identifier: MIT
 * Host-owned double buffering; the LVGL image points at the current frame.
 * Rendering never retains Guest pointers or copies an entire Guest heap. */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "services.h"
#include "pxa/surface.h"
#include "pxa/game_render.h"

#define LAYERS 4
#define BUDGET (24u*1024u*1024u)
typedef struct {
    uint32_t id,bytes,stride;
    uint8_t *front,*back,*scratch,*presentation_pixels;
    lv_obj_t *image;
    lv_image_dsc_t descriptor,presentation;
    pxa_surface_layer_t layer;
    pxa_surface_state_t state;
    pxa_raster_bindings_t bindings;
    pxa_raster_telemetry_t telemetry;
    uint64_t frame_id;
    uint8_t scratch_mode,pending,buffer_count,raster;
    uint8_t *registered;
    uint32_t registered_bytes;
    uint8_t owned[3],release_head,release_count;
    pxa_surface_release_t releases[3];
} layer_t;
static layer_t layers[LAYERS];
static pxa_surface_service_t *surface_service;
static uint32_t serial;
static size_t allocated;
static uint16_t display_width,display_height;
static void *allocate(void *c,size_t size){
    (void)c;if(size>BUDGET-allocated)return NULL;
    size_t *p=calloc(1,16+size);if(!p)return NULL;*p=size;allocated+=size;return (uint8_t *)p+16;
}
static void release(void *c,void *memory){(void)c;if(memory){size_t *p=(size_t *)((uint8_t *)memory-16);allocated-=*p;free(p);}}
static layer_t *find(uint64_t id){for(unsigned i=0;i<LAYERS;i++)if(layers[i].id&&layers[i].id==id)return &layers[i];return NULL;}
EM_JS(void,retire_buffers,(uint8_t *p),{(Module.browserSurfaceRetired||(Module.browserSurfaceRetired=[])).push(p);});
static void close_layer(void *c,uint64_t id){
    (void)c;layer_t *l=find(id);if(!l)return;
    if(l->image)lv_obj_delete(l->image);
    pxa_raster_bindings_release(&l->bindings);
    if(l->registered){allocated-=l->registered_bytes;retire_buffers(l->registered);}
    release(NULL,l->front);release(NULL,l->back);release(NULL,l->scratch);release(NULL,l->presentation_pixels);memset(l,0,sizeof(*l));
}
static pxa_status_t configure(void *c,uint64_t id,const pxa_surface_layer_t *v);
static pxa_status_t create(void *c,const pxa_surface_desc_t *d,uint64_t *id,uint32_t *stride){
    (void)c;
    uint64_t bytes=(uint64_t)d->width*d->height*(d->format==PXA_SURFACE_FORMAT_RGB565?2:4);
    if(!bytes||bytes>BUDGET/2)return PXA_STATUS_RESOURCE_LIMIT;
    layer_t *l=NULL;for(unsigned i=0;i<LAYERS;i++)if(!layers[i].id){l=&layers[i];break;}
    if(!l)return PXA_STATUS_RESOURCE_LIMIT;
    memset(l,0,sizeof(*l));do{l->id=++serial;}while(!l->id);
    l->bytes=(uint32_t)bytes;l->stride=l->bytes/d->height;l->buffer_count=d->buffer_count;
    l->front=allocate(NULL,l->bytes);l->back=allocate(NULL,l->bytes);
    if(!l->front||!l->back){close_layer(NULL,l->id);return PXA_STATUS_RESOURCE_LIMIT;}
    l->image=lv_image_create(lv_screen_active());if(!l->image){close_layer(NULL,l->id);return PXA_STATUS_RESOURCE_LIMIT;}
    lv_obj_set_clickable(l->image,false);lv_obj_set_scrollable(l->image,false);
    l->descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;
    l->descriptor.header.cf=d->format==PXA_SURFACE_FORMAT_RGB565?LV_COLOR_FORMAT_RGB565:LV_COLOR_FORMAT_ARGB8888;
    l->descriptor.header.flags=d->format==PXA_SURFACE_FORMAT_ARGB8888_PREMULTIPLIED?LV_IMAGE_FLAGS_PREMULTIPLIED:0;
    l->descriptor.header.w=d->width;l->descriptor.header.h=d->height;l->descriptor.header.stride=l->stride;
    l->descriptor.data_size=l->bytes;l->descriptor.data=l->front;
    lv_image_set_src(l->image,&l->descriptor);lv_image_set_antialias(l->image,false);lv_image_set_pivot(l->image,0,0);
    l->layer=(pxa_surface_layer_t){.width=d->width,.height=d->height,.visible=1};
    pxa_status_t configured=configure(c,l->id,&l->layer);
    if(configured){close_layer(c,l->id);return configured;}
    lv_obj_move_background(l->image);*id=l->id;*stride=l->stride;return PXA_STATUS_OK;
}
static void update_image(layer_t *l){
    if(l->presentation_pixels){
        const uint32_t w=l->presentation.header.w,h=l->presentation.header.h;
        const uint32_t sw=l->descriptor.header.w,sh=l->descriptor.header.h;
        uint16_t *out=(uint16_t *)l->presentation_pixels;
        const uint16_t *in=(const uint16_t *)l->descriptor.data;
        for(uint32_t y=0;y<h;y++){
            const uint32_t source_y=y*sh/h;
            if(y&&source_y==(y-1)*sh/h){memcpy(out+y*w,out+(y-1)*w,w*2u);continue;}
            for(uint32_t x=0;x<w;x++)out[y*w+x]=in[source_y*sw+x*sw/w];
        }
        lv_image_set_src(l->image,&l->presentation);
    }else lv_image_set_src(l->image,&l->descriptor);
    lv_obj_invalidate(l->image);
}
static pxa_status_t write_frame(void *c,uint64_t id,const uint8_t *data,size_t size){
    (void)c;layer_t *l=find(id);if(!l)return PXA_STATUS_NOT_FOUND;if(size!=l->bytes)return PXA_STATUS_INVALID_ARGUMENT;
    memcpy(l->back,data,size);return PXA_STATUS_OK;
}
static pxa_status_t queue_frame(void *c,uint64_t id,uint64_t frame,const pxa_surface_damage_rect_t *damage,uint8_t count){
    (void)c;(void)damage;(void)count;layer_t *l=find(id);if(!l)return PXA_STATUS_NOT_FOUND;
    if(frame<=l->frame_id)return PXA_STATUS_BAD_STATE;
    if(l->pending){l->state.dropped_frames++;l->state.replaced_frames++;}
    uint8_t *old=l->front;l->front=l->back;l->back=old;l->descriptor.data=l->front;
    update_image(l);
    l->state.submitted_frames++;l->frame_id=frame;l->pending=1;return PXA_STATUS_OK;
}
static pxa_status_t configure(void *c,uint64_t id,const pxa_surface_layer_t *v){
    (void)c;layer_t *l=find(id);if(!l)return PXA_STATUS_NOT_FOUND;l->layer=*v;
    int32_t x=v->x,y=v->y;uint32_t w=v->width,h=v->height;
    if(!l->raster&&l->descriptor.header.cf==LV_COLOR_FORMAT_RGB565){
        // Match the desktop/device Surface presenter and its input geometry.
        unsigned scale=1;
        for(unsigned next=2;next<=4;next*=2)
            if(w*next<=display_width&&h*next<=display_height)scale=next;
        w*=scale;h*=scale;
        if(!x&&!y){x=w<display_width?(display_width-w)/2:0;y=h<display_height?(display_height-h)/2:0;}
    }
    lv_obj_set_pos(l->image,x,y);
    if(l->descriptor.header.cf==LV_COLOR_FORMAT_RGB565){
        // Scale exact pixel cells once on frame submission. LVGL's geometric
        // image transform clips the final source cell at integer zoom factors.
        // A reusable buffer also avoids transforming again on UI redraws.
        const int scaled=w!=l->descriptor.header.w||h!=l->descriptor.header.h;
        if(scaled&&(!l->presentation_pixels||l->presentation.header.w!=w||l->presentation.header.h!=h)){
            uint8_t *pixels=allocate(NULL,(size_t)w*h*2u);if(!pixels)return PXA_STATUS_RESOURCE_LIMIT;
            release(NULL,l->presentation_pixels);l->presentation_pixels=pixels;
            l->presentation=l->descriptor;l->presentation.header.w=w;l->presentation.header.h=h;
            l->presentation.header.stride=w*2u;l->presentation.data_size=w*h*2u;l->presentation.data=pixels;
        }else if(!scaled){release(NULL,l->presentation_pixels);l->presentation_pixels=NULL;}
        lv_image_set_scale(l->image,256);update_image(l);
    }else{
        lv_image_set_scale_x(l->image,w*256/l->descriptor.header.w);
        lv_image_set_scale_y(l->image,h*256/l->descriptor.header.h);
    }
    lv_obj_set_hidden(l->image,!v->visible);
    if(v->z>=0)lv_obj_move_foreground(l->image);else lv_obj_move_background(l->image);
    return PXA_STATUS_OK;
}
static pxa_status_t query(void *c,uint64_t id,pxa_surface_state_t *out){
    (void)c;layer_t *l=find(id);if(!l)return PXA_STATUS_NOT_FOUND;*out=l->state;
    /* Two physical buffers. A pending front buffer does not occupy the writable back. */
    out->free_buffers=1;
    if(l->registered){out->free_buffers=0;for(unsigned i=0;i<l->buffer_count;i++)out->free_buffers+=!l->owned[i];}
    out->flags=PXA_SURFACE_STATE_FLAG_SUPPORTS_ALPHA_COMPOSITING|PXA_SURFACE_STATE_FLAG_SUPPORTS_GUEST_MAPPED;return PXA_STATUS_OK;
}
static pxa_status_t register_buffers(void *c,uint64_t id,uint8_t *data,size_t bytes){
    (void)c;layer_t *l=find(id);if(!l)return PXA_STATUS_NOT_FOUND;
    if(l->registered)return PXA_STATUS_BAD_STATE;
    if(bytes!=(size_t)l->bytes*l->buffer_count||bytes>BUDGET-allocated)return PXA_STATUS_RESOURCE_LIMIT;
    l->registered=data;l->registered_bytes=bytes;allocated+=bytes;return PXA_STATUS_OK;
}
EMSCRIPTEN_KEEPALIVE uint32_t browser_surface_frame_bytes(uint8_t *registered){
    for(unsigned i=0;i<LAYERS;i++)if(layers[i].registered==registered&&registered)return layers[i].bytes;
    return 0;
}
EMSCRIPTEN_KEEPALIVE int browser_surface_can_present(uint8_t *registered,uint32_t index,uint64_t frame){
    for(unsigned i=0;i<LAYERS;i++){
        layer_t *l=&layers[i];
        if(l->registered==registered&&registered)return index<l->buffer_count&&l->owned[index]==1&&frame>l->frame_id&&l->release_count<3;
    }
    return 0;
}
static pxa_status_t acquire_buffer(void *c,uint64_t id,uint8_t *index){
    (void)c;layer_t *l=find(id);if(!l||!l->registered)return PXA_STATUS_BAD_STATE;
    for(unsigned i=0;i<l->buffer_count;i++)if(!l->owned[i]){l->owned[i]=1;*index=i;return PXA_STATUS_OK;}
    return PXA_STATUS_WOULD_BLOCK;
}
static pxa_status_t present_buffer(void *c,uint64_t id,uint8_t index,uint64_t frame){
    (void)c;layer_t *l=find(id);if(!l||!l->registered)return PXA_STATUS_BAD_STATE;
    if(index>=l->buffer_count||l->owned[index]!=1||frame<=l->frame_id||l->release_count>=3)return PXA_STATUS_BAD_STATE;
    if(l->pending){l->state.dropped_frames++;l->state.replaced_frames++;}
    l->descriptor.data=l->registered+(size_t)index*l->bytes;update_image(l);
    l->pending=1;l->frame_id=frame;l->state.submitted_frames++;l->owned[index]=2;
    l->releases[(l->release_head+l->release_count)%3]=(pxa_surface_release_t){.buffer_index=index,.frame_id=frame};
    l->release_count++;return PXA_STATUS_OK;
}
static pxa_status_t peek_release(void *c,uint64_t id,pxa_surface_release_t *out){
    (void)c;layer_t *l=find(id);if(!l)return PXA_STATUS_NOT_FOUND;
    if(!l->release_count)return PXA_STATUS_WOULD_BLOCK;
    *out=l->releases[l->release_head];return PXA_STATUS_OK;
}
static void consume_release(void *c,uint64_t id){
    (void)c;layer_t *l=find(id);if(!l||!l->release_count)return;
    l->owned[l->releases[l->release_head].buffer_index]=0;l->release_head=(l->release_head+1)%3;
    l->release_count--;l->state.released_frames++;
}
void browser_graphics_poll(void){if(surface_service)pxa_surface_service_flush_releases(surface_service);}
static pxa_status_t raster_create(void *c,const pxa_game_render_desc_t *d,uint64_t *id,uint32_t *caps){
    pxa_surface_desc_t surface={d->width,d->height,PXA_SURFACE_FORMAT_RGB565,d->buffer_count,0};uint32_t stride;
    pxa_status_t s=create(c,&surface,id,&stride);if(s)return s;layer_t *l=find(*id);l->raster=1;l->scratch_mode=d->scratch_mode;
    size_t bytes=d->scratch_mode==PXA_RASTER_SCRATCH_DEPTH16?l->bytes:
        d->scratch_mode==PXA_RASTER_SCRATCH_COVERAGE_2BIT?((d->width+7u)/8)*d->height*2u:0;
    if(bytes&&!(l->scratch=allocate(NULL,bytes))){close_layer(c,*id);return PXA_STATUS_RESOURCE_LIMIT;}
    uint32_t scale_x=(uint32_t)display_width*256/d->width,scale_y=(uint32_t)display_height*256/d->height;
    uint32_t scale=scale_x<scale_y?scale_x:scale_y;
    pxa_surface_layer_t v={.width=d->width*scale/256,.height=d->height*scale/256,.visible=1};
    v.x=(display_width-v.width)/2;v.y=(display_height-v.height)/2;s=configure(c,*id,&v);
    if(s){close_layer(c,*id);return s;}
    *caps=PXA_RASTER_CAP_KNOWN_MASK;return PXA_STATUS_OK;
}
static pxa_status_t upload(void *c,uint64_t id,const uint8_t *data,size_t size){
    (void)c;layer_t *l=find(id);if(!l)return PXA_STATUS_NOT_FOUND;
    pxa_raster_asset_t *asset;pxa_status_t s=pxa_raster_asset_from_upload(data,size,allocate,release,NULL,&asset);
    if(s)return s;pxa_raster_asset_release(pxa_raster_bindings_replace(&l->bindings,data[9],asset));
    pxa_raster_asset_release(asset);return PXA_STATUS_OK;
}
static pxa_status_t bind_assets(void *c,uint64_t id,const pxa_raster_bindings_t *replacement,uint64_t mask,uint8_t palette){
    (void)c;layer_t *l=find(id);if(!l)return PXA_STATUS_NOT_FOUND;pxa_raster_bindings_t old={0};
    pxa_raster_bindings_update(&l->bindings,replacement,mask,palette,&old);pxa_raster_bindings_release(&old);return PXA_STATUS_OK;
}
static pxa_status_t submit(void *c,uint64_t id,const uint8_t *data,size_t size){
    layer_t *l=find(id);if(!l)return PXA_STATUS_NOT_FOUND;
    pxa_raster_resources_t resources;pxa_raster_bindings_view(&l->bindings,PXA_RASTER_CAP_KNOWN_MASK,&resources);
    pxa_raster_target_t target={.pixels=(uint16_t *)l->back,.depth_pixels=(uint16_t *)l->scratch,
        .stride_pixels=l->stride/2,.depth_stride_pixels=l->descriptor.header.w,
        .width=l->descriptor.header.w,.height=l->descriptor.header.h,.scratch_mode=l->scratch_mode};
    pxa_raster_draw_list_view_t list;pxa_status_t s=pxa_raster_validate_draw_list(data,size,&target,&resources,&list);
    if(s||list.frame_id<=l->frame_id){l->telemetry.rejected_lists++;return s?s:PXA_STATUS_BAD_STATE;}
    double begin=emscripten_get_now();pxa_raster_execute_draw_list(data,&list,&target,&resources,&l->telemetry);
    l->telemetry.last_host_raster_us=(uint32_t)((emscripten_get_now()-begin)*1000);
    l->telemetry.host_raster_us+=l->telemetry.last_host_raster_us;l->telemetry.submitted_frames++;l->telemetry.rendered_frames++;
    return queue_frame(c,id,list.frame_id,NULL,0);
}
static pxa_status_t raster_query(void *c,uint64_t id,pxa_raster_telemetry_t *out){
    (void)c;layer_t *l=find(id);if(!l)return PXA_STATUS_NOT_FOUND;*out=l->telemetry;
    out->dropped_frames=l->state.dropped_frames;return PXA_STATUS_OK;
}
static uint64_t now(void *c){(void)c;return (uint64_t)(emscripten_get_now()*1000);}
void browser_graphics_presented(void){
    for(unsigned i=0;i<LAYERS;i++){layer_t *l=&layers[i];if(l->id&&l->pending&&l->layer.visible){l->state.presented_frames++;l->pending=0;}}
}
void browser_graphics_order(void){
    layer_t *ordered[LAYERS];unsigned count=0,negative=0;
    for(unsigned i=0;i<LAYERS;i++)if(layers[i].id){
        unsigned at=count++;while(at&&ordered[at-1]->layer.z>layers[i].layer.z){ordered[at]=ordered[at-1];at--;}
        ordered[at]=&layers[i];if(layers[i].layer.z<0)negative++;
    }
    unsigned children=lv_obj_get_child_count(lv_screen_active());
    for(unsigned i=0;i<count;i++){
        int32_t at=i<negative?i:children-(count-i);
        if(lv_obj_get_index(ordered[i]->image)!=at)lv_obj_move_to_index(ordered[i]->image,at);
    }
}
pxa_status_t browser_graphics_init(pxa_runtime_t *rt,uint16_t width,uint16_t height){
    display_width=width;display_height=height;
    pxa_surface_config_t c={.struct_size=sizeof(c),.max_surfaces=4,.max_surfaces_per_component=4,
        .max_width=UINT16_MAX,.max_height=UINT16_MAX,
        .max_frame_bytes=BUDGET/2,.min_buffer_count=2,.max_buffer_count=3,
        .backend={.struct_size=sizeof(pxa_surface_backend_t),.create=create,.write=write_frame,
            .queue=queue_frame,.configure=configure,.query=query,.close=close_layer,
            .register_buffers=register_buffers,.acquire_buffer=acquire_buffer,.present_buffer=present_buffer,
            .peek_release=peek_release,.consume_release=consume_release}};
    pxa_surface_service_t *surface;size_t bytes=pxa_surface_service_workspace_size(&c);
    pxa_status_t s=pxa_surface_service_init(calloc(1,bytes),bytes,rt,&c,&surface);if(s)return s;
    surface_service=surface;
    s=pxa_surface_service_register(surface);if(s)return s;
    pxa_game_render_config_t g={.struct_size=sizeof(g),.max_contexts=4,.max_contexts_per_component=4,
        .min_buffer_count=2,.max_buffer_count=3,.auto_target_profile={width,height,15,1},.now_us=now,
        .backend={.struct_size=sizeof(pxa_game_render_backend_t),.create=raster_create,.upload=upload,
            .submit=submit,.query=raster_query,.close=close_layer,.bind_assets=bind_assets}};
    pxa_game_render_service_t *game;bytes=pxa_game_render_service_workspace_size(&g);
    s=pxa_game_render_service_init(calloc(1,bytes),bytes,rt,&g,&game);return s?s:pxa_game_render_service_register(game);
}

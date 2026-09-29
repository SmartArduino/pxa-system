#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pxa_image_set.h"

static uint64_t pending[4],live[64],next_handle=UINT64_C(0xf001000000000000);
static unsigned loads,cancels,closes;
static int32_t reject;
int32_t pxa_submit(const uint8_t *data,uint32_t size) {
    pxa_event_t e;assert(pxa_parse_event(data,size,&e));
    if(e.service==PXA_ASSETS_SERVICE) {
        assert(e.opcode==PXA_ASSETS_LOAD && e.payload[2]==PXA_ASSET_IMAGE);
        assert(e.token>UINT32_MAX);
        for(unsigned i=0;i<4;++i)assert(pending[i]!=e.token);
        if(reject)return reject;
        for(unsigned i=0;i<4;++i)if(!pending[i]){pending[i]=e.token;++loads;return 0;}
        assert(0);
    }
    assert(e.service==PXA_CORE_SERVICE && e.payload_size==8);
    uint64_t value=pxa_load_u64(e.payload);
    if(e.opcode==PXA_CORE_CANCEL_REQUEST) {
        for(unsigned i=0;i<4;++i)if(pending[i]==value){++cancels;return 0;}
        assert(0);
    }
    assert(e.opcode==PXA_CORE_CLOSE_HANDLE);
    for(unsigned i=0;i<64;++i)if(live[i]==value){live[i]=0;++closes;return 0;}
    assert(0);return PXA_STATUS_INTERNAL;
}
static void complete(pxa_image_set_t *set,int32_t status,uint8_t kind) {
    uint8_t data[32]={0};uint64_t token=set->pending_token;
    assert(token);
    unsigned slot=0;while(slot<4 && pending[slot]!=token)++slot;
    assert(slot<4);pending[slot]=0;
    pxa_store_u32(data,(uint32_t)status);
    if(!status) {
        unsigned i=0;while(i<64 && live[i])++i;assert(i<64);
        live[i]=++next_handle;pxa_store_u64(data+4,live[i]);
        data[12]=kind;data[13]=PXA_ASSET_ENCODING_BGRA8888;
        pxa_store_u16(data+14,1);pxa_store_u16(data+16,2);pxa_store_u16(data+18,2);
        pxa_store_u32(data+20,48);pxa_store_u32(data+24,16);pxa_store_u32(data+28,80);
    }
    pxa_event_t e={PXA_ASSETS_SERVICE,PXA_ASSETS_LOAD,token,data,status?4:32};
    assert(pxa_image_set_on_event(set,&e));assert(!pxa_image_set_on_event(set,&e));
}
int main(void) {
    const char *paths[64];for(unsigned i=0;i<64;++i)paths[i]="assets/icon.pxr";
    const char *oversized_paths[257]={0};uint64_t oversized_handles[257]={0};
    pxa_image_set_t oversized=PXA_IMAGE_SET_INIT(oversized_paths,oversized_handles,1);
    assert(pxa_image_set_select(&oversized,1)==PXA_STATUS_INVALID_ARGUMENT);
    uint64_t handles[64]={0},other_handles[64]={0};
    pxa_image_set_t set=PXA_IMAGE_SET_INIT(paths,handles,UINT64_C(0xaaaa000000000000));
    pxa_image_set_t other=PXA_IMAGE_SET_INIT(paths,other_handles,UINT64_C(0xbbbb000000000000));
    const uint64_t high=UINT64_C(1)<<63;
    assert(!pxa_image_set_select(&set,1|high));pxa_image_set_pump(&set);
    pxa_image_set_pump(&set);assert(loads==1);
    complete(&set,0,PXA_ASSET_IMAGE);assert(!set.pending_token && !pxa_image_set_ready(&set));
    pxa_image_set_pump(&set);assert(set.pending_image==63);
    complete(&set,0,PXA_ASSET_IMAGE);assert(pxa_image_set_ready(&set) && handles[0] && handles[63]);
    assert(!pxa_image_set_select(&set,high));assert(!handles[0] && handles[63]);
    assert(!pxa_image_set_select(&set,high|2));pxa_image_set_pump(&set);
    assert(!pxa_image_set_select(&other,1));pxa_image_set_pump(&other);
    pxa_event_t unrelated={PXA_ASSETS_SERVICE,PXA_ASSETS_LOAD,other.pending_token,NULL,0};
    assert(!pxa_image_set_on_event(&set,&unrelated));
    pxa_image_set_pause(&set,1);pxa_image_set_pause(&set,1);assert(cancels==1);
    complete(&set,0,PXA_ASSET_IMAGE);assert(!handles[1] && handles[63]);
    complete(&other,0,PXA_ASSET_IMAGE);assert(pxa_image_set_ready(&other));
    pxa_image_set_pause(&set,0);pxa_image_set_pump(&set);
    assert(!pxa_image_set_select(&set,high));assert(cancels==2);
    assert(!pxa_image_set_select(&set,high|2));
    complete(&set,PXA_STATUS_CANCELLED,PXA_ASSET_IMAGE);assert(!set.error);
    pxa_image_set_pump(&set);complete(&set,PXA_STATUS_IO_ERROR,PXA_ASSET_IMAGE);
    unsigned before=loads;pxa_image_set_pump(&set);assert(loads==before && handles[63]);
    pxa_image_set_retry(&set);reject=PXA_STATUS_WOULD_BLOCK;pxa_image_set_pump(&set);
    assert(!set.pending_token && set.error==reject);reject=0;
    pxa_image_set_retry(&set);pxa_image_set_pump(&set);complete(&set,0,PXA_ASSET_TEXTURE);
    assert(set.error==PXA_STATUS_PROTOCOL_ERROR && !handles[1] && handles[63]);
    pxa_image_set_retry(&set);pxa_image_set_pump(&set);
    pxa_image_set_stop(&set);complete(&set,0,PXA_ASSET_IMAGE);
    before=loads;pxa_image_set_retry(&set);pxa_image_set_pause(&set,0);pxa_image_set_pump(&set);
    assert(loads==before && pxa_image_set_select(&set,1)==PXA_STATUS_BAD_STATE);
    pxa_image_set_stop(&other);
    pxa_image_set_t exhausted=PXA_IMAGE_SET_INIT(paths,handles,UINT64_MAX);
    assert(!pxa_image_set_select(&exhausted,1));pxa_image_set_pump(&exhausted);
    assert(exhausted.error==PXA_STATUS_RESOURCE_LIMIT && !exhausted.pending_token);
    for(unsigned i=0;i<64;++i)assert(!handles[i] && !other_handles[i] && !live[i]);
    for(unsigned i=0;i<4;++i)assert(!pending[i]);
    printf("Image set: loads=%u closes=%u cancels=%u; bit63, independent sets, cancellation race, retry and stop passed\n",loads,closes,cancels);
}

#include "pxa/asset_stream.h"
#include <string.h>
pxa_status_t pxa_asset_stream_init(pxa_asset_stream_t *s,const pxa_asset_block_map_t *map,
    void *context,pxa_status_t (*load)(void *,const pxa_asset_block_t *,uint8_t *,size_t),int (*cancelled)(void *)) {
    if (!s) return PXA_STATUS_INVALID_ARGUMENT;
    memset(s,0,sizeof(*s)); s->status=PXA_STATUS_BAD_STATE;
    if (!map || !load) return PXA_STATUS_INVALID_ARGUMENT;
    s->map=*map; s->context=context; s->load=load; s->cancelled=cancelled; s->status=0;
    return PXA_STATUS_OK;
}
static pxa_status_t check(pxa_asset_stream_t *s) {
    if (!s || !s->load) return PXA_STATUS_BAD_STATE;
    if (!s->status && s->cancelled && s->cancelled(s->context)) s->status=PXA_STATUS_CANCELLED;
    return s->status;
}
pxa_status_t pxa_asset_stream_read(pxa_asset_stream_t *s,uint8_t *out,size_t capacity,size_t *bytes) {
    if (!bytes || (!out && capacity)) return PXA_STATUS_INVALID_ARGUMENT;
    *bytes=0; pxa_status_t status=check(s); if(status) return status;
    if (!capacity || s->position==s->map.info.stored_bytes) return PXA_STATUS_OK;
    pxa_asset_block_t range;
    status=pxa_asset_block_map_get(&s->map,s->position,&range);
    if(status) return s->status=status;
    if(range.bytes>capacity) range.bytes=(uint32_t)capacity;
    status=s->load(s->context,&range,out,range.bytes);
    if(status) return s->status=status;
    status=check(s); if(status) return status;
    s->position+=range.bytes; s->read_bytes+=range.bytes; ++s->read_calls;
    *bytes=range.bytes; return PXA_STATUS_OK;
}
pxa_status_t pxa_asset_stream_seek(pxa_asset_stream_t *s,uint32_t position) {
    pxa_status_t status=check(s); if(status) return status;
    if(position>s->map.info.stored_bytes) return PXA_STATUS_INVALID_ARGUMENT;
    s->position=position; return PXA_STATUS_OK;
}

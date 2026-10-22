#include "pxa/install_transfer.h"
#include "pxa/container.h"
#include <string.h>

static int nonzero(const uint8_t *p,size_t n) {uint8_t value=0;for(size_t i=0;i<n;++i)value|=p[i];return value!=0;}
static int valid(const pxa_install_transfer_t *transfer) {
    if(!transfer || !transfer->revision || transfer->phase<PXA_INSTALL_TRANSFER_COPYING ||
        transfer->phase>PXA_INSTALL_TRANSFER_ABORTED || !nonzero(transfer->transfer_id,16) ||
        !nonzero(transfer->target_app_volume.bytes,16) || transfer->source.generation==UINT64_MAX ||
        transfer->kind>PXA_INSTALL_TRANSFER_KIND_LEGACY_ADOPT)return 0;
    const int same_volume=!memcmp(transfer->target_app_volume.bytes,transfer->source.app_volume.bytes,16);
    if(transfer->kind==PXA_INSTALL_TRANSFER_KIND_MOVE) {if(same_volume)return 0;}
    else if(!same_volume || transfer->source.generation!=1 || transfer->source.data_generation!=1 ||
        (transfer->source.flags&(PXA_INSTALL_RECORD_ARCHIVED|PXA_INSTALL_RECORD_MANAGED_MEDIA)) ||
        nonzero(transfer->source.media_set_sha256,32) ||
        memcmp(transfer->target_media_volume.bytes,transfer->source.media_volume.bytes,16))return 0;
    return !!(transfer->source.flags&PXA_INSTALL_RECORD_HAS_MEDIA)==nonzero(transfer->target_media_volume.bytes,16);
}
pxa_status_t pxa_install_transfer_encode(const pxa_install_transfer_t *transfer,uint8_t output[PXA_INSTALL_TRANSFER_BYTES]) {
    if(!output)return PXA_STATUS_INVALID_ARGUMENT;
    memset(output,0,PXA_INSTALL_TRANSFER_BYTES);
    if(!valid(transfer))return PXA_STATUS_INVALID_ARGUMENT;
    pxa_status_t status=pxa_install_record_encode(&transfer->source,output+72);
    if(status!=PXA_STATUS_OK) {memset(output,0,PXA_INSTALL_TRANSFER_BYTES);return status;}
    memcpy(output,"PXMT",4);pxa_write_u16(output+4,transfer->kind?2:1);pxa_write_u16(output+6,PXA_INSTALL_TRANSFER_BYTES);
    pxa_write_u64(output+8,transfer->revision);output[16]=transfer->phase;output[17]=transfer->kind;
    memcpy(output+24,transfer->transfer_id,16);memcpy(output+40,transfer->target_app_volume.bytes,16);
    memcpy(output+56,transfer->target_media_volume.bytes,16);
    pxa_write_u32(output+PXA_INSTALL_TRANSFER_BYTES-4,pxa_container_crc32(output,PXA_INSTALL_TRANSFER_BYTES-4));
    return PXA_STATUS_OK;
}
pxa_status_t pxa_install_transfer_decode(pxa_bytes_t bytes,const pxa_volume_id_t *expected,pxa_install_transfer_t *output) {
    if(!output)return PXA_STATUS_INVALID_ARGUMENT;
    memset(output,0,sizeof(*output));
    if(!bytes.data || bytes.size!=PXA_INSTALL_TRANSFER_BYTES)return PXA_STATUS_DENIED;
    const uint8_t *p=bytes.data;
    const uint16_t version=pxa_read_u16(p+4);
    if(memcmp(p,"PXMT",4) || (version!=1 && version!=2) || pxa_read_u16(p+6)!=bytes.size ||
        pxa_read_u32(p+bytes.size-4)!=pxa_container_crc32(p,bytes.size-4))return PXA_STATUS_DENIED;
    if((version==1 && p[17]) || (version==2 && p[17]!=PXA_INSTALL_TRANSFER_KIND_LEGACY_ADOPT))return PXA_STATUS_DENIED;
    for(size_t i=18;i<24;++i)if(p[i])return PXA_STATUS_DENIED;
    output->revision=pxa_read_u64(p+8);output->phase=p[16];output->kind=p[17];memcpy(output->transfer_id,p+24,16);
    memcpy(output->target_app_volume.bytes,p+40,16);memcpy(output->target_media_volume.bytes,p+56,16);
    pxa_volume_id_t source_volume;memcpy(source_volume.bytes,p+72+72,16);
    /* This checks the nested canonical record, not source ownership. The
     * endpoint MAC/actual-volume binding remains mandatory for the caller. */
    pxa_status_t status=pxa_install_record_decode((pxa_bytes_t){p+72,PXA_INSTALL_RECORD_BYTES},&source_volume,&output->source);
    if(status!=PXA_STATUS_OK || !valid(output) || (expected && memcmp(expected->bytes,output->target_app_volume.bytes,16))) {
        memset(output,0,sizeof(*output));return PXA_STATUS_DENIED;
    }
    return PXA_STATUS_OK;
}
static int same(const pxa_install_transfer_t *a,const pxa_install_transfer_t *b) {
    const pxa_install_record_t *x=&a->source,*y=&b->source;
    return a->kind==b->kind && !memcmp(a->transfer_id,b->transfer_id,16) && !memcmp(a->target_app_volume.bytes,b->target_app_volume.bytes,16) &&
        !memcmp(a->target_media_volume.bytes,b->target_media_volume.bytes,16) &&
        x->generation==y->generation && x->release_sequence==y->release_sequence && x->flags==y->flags &&
        x->data_generation==y->data_generation && !strcmp(x->app_id,y->app_id) &&
        !memcmp(x->install_id,y->install_id,16) && !memcmp(x->publisher_root,y->publisher_root,32) &&
        !memcmp(x->app_volume.bytes,y->app_volume.bytes,16) && !memcmp(x->media_volume.bytes,y->media_volume.bytes,16) &&
        !memcmp(x->manifest_sha256,y->manifest_sha256,32) && !memcmp(x->media_set_sha256,y->media_set_sha256,32);
}
static int forward(uint8_t old,uint8_t next) {
    if(old==next)return 1;
    if(old==PXA_INSTALL_TRANSFER_COMMITTED || old==PXA_INSTALL_TRANSFER_ABORTED)return 0;
    return next==PXA_INSTALL_TRANSFER_ABORTED || (next>old && next<=PXA_INSTALL_TRANSFER_COMMITTED);
}
pxa_status_t pxa_install_transfer_select(pxa_bytes_t first,pxa_bytes_t second,const pxa_volume_id_t *expected,
    pxa_install_transfer_t *output,uint8_t *slot) {
    if(!output || !slot)return PXA_STATUS_INVALID_ARGUMENT;
    memset(output,0,sizeof(*output));*slot=0;
    if(!first.size && !second.size)return PXA_STATUS_NOT_FOUND;
    pxa_install_transfer_t other;
    pxa_status_t a=pxa_install_transfer_decode(first,expected,output),b=pxa_install_transfer_decode(second,expected,&other);
    if(a!=PXA_STATUS_OK && b!=PXA_STATUS_OK)return PXA_STATUS_DENIED;
    if(a!=PXA_STATUS_OK) {*output=other;*slot=1;return PXA_STATUS_OK;}
    if(b!=PXA_STATUS_OK)return PXA_STATUS_OK;
    int second_newer=other.revision>output->revision;
    const pxa_install_transfer_t *old=second_newer?output:&other,*latest=second_newer?&other:output;
    if(!same(output,&other) || (other.revision==output->revision && other.phase!=output->phase) ||
        !forward(old->phase,latest->phase)) {memset(output,0,sizeof(*output));return PXA_STATUS_DENIED;}
    if(second_newer) {*output=other;*slot=1;}
    return PXA_STATUS_OK;
}
pxa_status_t pxa_install_transfer_advance(const pxa_install_transfer_t *current,uint8_t phase,pxa_install_transfer_t *output) {
    if(!output)return PXA_STATUS_INVALID_ARGUMENT;
    uint8_t wire[PXA_INSTALL_TRANSFER_BYTES];pxa_status_t status=pxa_install_transfer_encode(current,wire);
    if(status!=PXA_STATUS_OK)return status;
    if(phase==current->phase) {*output=*current;return PXA_STATUS_OK;}
    if(!forward(current->phase,phase) || (phase!=PXA_INSTALL_TRANSFER_ABORTED && phase!=current->phase+1))return PXA_STATUS_BAD_STATE;
    if(current->revision==UINT64_MAX)return PXA_STATUS_LIMIT_EXCEEDED;
    *output=*current;output->phase=phase;++output->revision;return PXA_STATUS_OK;
}
pxa_status_t pxa_install_transfer_target(const pxa_install_transfer_t *transfer,pxa_install_record_t *output) {
    if(!output)return PXA_STATUS_INVALID_ARGUMENT;
    uint8_t wire[PXA_INSTALL_TRANSFER_BYTES];pxa_status_t status=pxa_install_transfer_encode(transfer,wire);
    if(status!=PXA_STATUS_OK)return status;
    *output=transfer->source;output->app_volume=transfer->target_app_volume;
    output->media_volume=transfer->target_media_volume;
    if(transfer->kind==PXA_INSTALL_TRANSFER_KIND_MOVE)++output->generation;
    return PXA_STATUS_OK;
}

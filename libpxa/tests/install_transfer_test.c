#include "pxa/install_transfer.h"
#include "pxa/container.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static pxa_bytes_t bytes(const uint8_t *p) {return (pxa_bytes_t){p,PXA_INSTALL_TRANSFER_BYTES};}
static pxa_install_transfer_t fixture(void) {
    pxa_install_transfer_t t={0};t.revision=1;t.phase=PXA_INSTALL_TRANSFER_COPYING;t.transfer_id[0]=9;
    t.target_app_volume.bytes[0]=7;t.target_media_volume.bytes[0]=8;
    t.source.generation=42;t.source.data_generation=7;t.source.release_sequence=999;
    t.source.flags=PXA_INSTALL_RECORD_ENABLED|PXA_INSTALL_RECORD_HAS_MEDIA|PXA_INSTALL_RECORD_MANAGED_MEDIA;
    t.source.app_volume.bytes[0]=1;t.source.media_volume.bytes[0]=2;t.source.publisher_root[0]=3;
    t.source.install_id[0]=4;t.source.manifest_sha256[0]=5;t.source.media_set_sha256[0]=6;
    strcpy(t.source.app_id,"quake");return t;
}
int main(void) {
    pxa_install_transfer_t copying=fixture(),ready,surrendered,committed,aborted,out;
    uint8_t first[PXA_INSTALL_TRANSFER_BYTES],second[PXA_INSTALL_TRANSFER_BYTES],altered[PXA_INSTALL_TRANSFER_BYTES],slot;
    assert(PXA_INSTALL_TRANSFER_BYTES==328 && !pxa_install_transfer_encode(&copying,first));
    assert(!memcmp(first,"PXMT\1\0",6) && pxa_read_u16(first+6)==328 && pxa_read_u64(first+8)==1 && first[16]==1);
    assert(!memcmp(first+72,"PXIR",4) && pxa_read_u64(first+80)==42);
    assert(!pxa_install_transfer_decode(bytes(first),&copying.target_app_volume,&out));
    assert(!pxa_install_transfer_encode(&out,second) && !memcmp(first,second,sizeof(first)));
    pxa_volume_id_t wrong={{10}};
    assert(pxa_install_transfer_decode(bytes(first),&wrong,&out)==PXA_STATUS_DENIED && !out.revision);
    pxa_install_record_t target;
    assert(!pxa_install_transfer_target(&copying,&target));
    assert(target.generation==43 && target.data_generation==7 && target.release_sequence==999 && target.flags==copying.source.flags);
    assert(!memcmp(target.install_id,copying.source.install_id,16) && !strcmp(target.app_id,"quake"));
    assert(!memcmp(target.app_volume.bytes,copying.target_app_volume.bytes,16));
    assert(!memcmp(target.media_volume.bytes,copying.target_media_volume.bytes,16) && target.media_set_sha256[0]==6);
    assert(!pxa_install_transfer_advance(&copying,PXA_INSTALL_TRANSFER_READY,&ready) && ready.revision==2);
    assert(!pxa_install_transfer_advance(&ready,PXA_INSTALL_TRANSFER_SURRENDERED,&surrendered) && surrendered.revision==3);
    assert(!pxa_install_transfer_advance(&surrendered,PXA_INSTALL_TRANSFER_COMMITTED,&committed) && committed.revision==4);
    out=committed;
    assert(!pxa_install_transfer_advance(&out,PXA_INSTALL_TRANSFER_COMMITTED,&out) && out.revision==4);
    assert(pxa_install_transfer_advance(&committed,PXA_INSTALL_TRANSFER_ABORTED,&out)==PXA_STATUS_BAD_STATE && out.revision==4);
    assert(pxa_install_transfer_advance(&copying,PXA_INSTALL_TRANSFER_SURRENDERED,&out)==PXA_STATUS_BAD_STATE && out.revision==4);
    assert(!pxa_install_transfer_advance(&ready,PXA_INSTALL_TRANSFER_ABORTED,&aborted) && aborted.revision==3);
    assert(pxa_install_transfer_advance(&aborted,PXA_INSTALL_TRANSFER_COMMITTED,&out)==PXA_STATUS_BAD_STATE);
    assert(!pxa_install_transfer_encode(&ready,second));
    assert(!pxa_install_transfer_select(bytes(first),bytes(second),NULL,&out,&slot) && slot==1 && out.phase==PXA_INSTALL_TRANSFER_READY);
    assert(!pxa_install_transfer_select(bytes(second),bytes(first),NULL,&out,&slot) && slot==0 && out.revision==2);
    assert(!pxa_install_transfer_select((pxa_bytes_t){0},bytes(first),NULL,&out,&slot) && slot==1);
    assert(pxa_install_transfer_select((pxa_bytes_t){0},(pxa_bytes_t){0},NULL,&out,&slot)==PXA_STATUS_NOT_FOUND);
    for(size_t i=0;i<sizeof(first);++i)for(unsigned bit=0;bit<8;++bit) {
        memcpy(altered,first,sizeof(first));altered[i]^=(uint8_t)(1u<<bit);
        assert(pxa_install_transfer_decode(bytes(altered),NULL,&out)==PXA_STATUS_DENIED && !out.revision);
    }
    memcpy(altered,first,sizeof(first));altered[17]=1;
    pxa_write_u32(altered+sizeof(altered)-4,pxa_container_crc32(altered,sizeof(altered)-4));
    assert(pxa_install_transfer_decode(bytes(altered),NULL,&out)==PXA_STATUS_DENIED);
    assert(!pxa_install_transfer_select(bytes(altered),bytes(second),NULL,&out,&slot) && slot==1 && out.revision==2);
    pxa_install_transfer_t fork=ready;fork.revision=1;assert(!pxa_install_transfer_encode(&fork,second));
    assert(pxa_install_transfer_select(bytes(first),bytes(second),NULL,&out,&slot)==PXA_STATUS_DENIED);
    fork=ready;fork.source.data_generation++;assert(!pxa_install_transfer_encode(&fork,second));
    assert(pxa_install_transfer_select(bytes(first),bytes(second),NULL,&out,&slot)==PXA_STATUS_DENIED);
    fork=ready;fork.transfer_id[1]=1;assert(!pxa_install_transfer_encode(&fork,second));
    assert(pxa_install_transfer_select(bytes(first),bytes(second),NULL,&out,&slot)==PXA_STATUS_DENIED);
    assert(!pxa_install_transfer_encode(&committed,first));fork=ready;fork.revision=5;
    assert(!pxa_install_transfer_encode(&fork,second));
    assert(pxa_install_transfer_select(bytes(first),bytes(second),NULL,&out,&slot)==PXA_STATUS_DENIED);
    fork=aborted;fork.revision=5;assert(!pxa_install_transfer_encode(&fork,second));
    assert(pxa_install_transfer_select(bytes(first),bytes(second),NULL,&out,&slot)==PXA_STATUS_DENIED);
    fork=copying;fork.revision=UINT64_MAX;out=copying;
    assert(pxa_install_transfer_advance(&fork,PXA_INSTALL_TRANSFER_READY,&out)==PXA_STATUS_LIMIT_EXCEEDED && out.revision==1);
    fork=copying;fork.source.generation=UINT64_MAX;
    assert(pxa_install_transfer_target(&fork,&target)==PXA_STATUS_INVALID_ARGUMENT);
    fork=copying;fork.target_app_volume=fork.source.app_volume;
    assert(pxa_install_transfer_encode(&fork,first)==PXA_STATUS_INVALID_ARGUMENT);
    fork=copying;fork.target_media_volume=(pxa_volume_id_t){{0}};
    assert(pxa_install_transfer_encode(&fork,first)==PXA_STATUS_INVALID_ARGUMENT);
    fork=copying;fork.source.flags|=PXA_INSTALL_RECORD_ARCHIVED;
    assert(!pxa_install_transfer_target(&fork,&target) && (target.flags&PXA_INSTALL_RECORD_ARCHIVED));
    pxa_install_transfer_t legacy=fixture();legacy.kind=PXA_INSTALL_TRANSFER_KIND_LEGACY_ADOPT;
    legacy.source.generation=1;legacy.source.data_generation=1;
    legacy.source.flags=PXA_INSTALL_RECORD_ENABLED|PXA_INSTALL_RECORD_HAS_MEDIA;
    memset(legacy.source.media_set_sha256,0,32);
    legacy.target_app_volume=legacy.source.app_volume;legacy.target_media_volume=legacy.source.media_volume;
    assert(!pxa_install_transfer_encode(&legacy,first) && pxa_read_u16(first+4)==2 && first[17]==1);
    assert(!pxa_install_transfer_decode(bytes(first),&legacy.target_app_volume,&out) && out.kind==1);
    assert(!pxa_install_transfer_encode(&out,second) && !memcmp(first,second,sizeof(first)));
    assert(!pxa_install_transfer_target(&legacy,&target) && target.generation==1 && target.data_generation==1);
    assert(!memcmp(target.install_id,legacy.source.install_id,16) && !memcmp(target.app_volume.bytes,legacy.source.app_volume.bytes,16));
    assert(!pxa_install_transfer_advance(&legacy,PXA_INSTALL_TRANSFER_READY,&ready));
    assert(!pxa_install_transfer_encode(&ready,second));
    assert(!pxa_install_transfer_select(bytes(first),bytes(second),NULL,&out,&slot) && slot==1 && out.kind==1);
    for(unsigned mutation=0;mutation<7;++mutation) {
        fork=legacy;
        if(mutation==0)fork.kind=PXA_INSTALL_TRANSFER_KIND_MOVE;
        if(mutation==1)fork.kind=2;
        if(mutation==2)fork.source.generation=2;
        if(mutation==3)fork.source.data_generation=2;
        if(mutation==4)fork.source.flags|=PXA_INSTALL_RECORD_ARCHIVED;
        if(mutation==5)fork.source.flags|=PXA_INSTALL_RECORD_MANAGED_MEDIA;
        if(mutation==6)fork.target_media_volume.bytes[0]++;
        assert(pxa_install_transfer_encode(&fork,altered)==PXA_STATUS_INVALID_ARGUMENT);
    }
    memcpy(altered,first,sizeof(first));pxa_write_u16(altered+4,1);
    pxa_write_u32(altered+sizeof(altered)-4,pxa_container_crc32(altered,sizeof(altered)-4));
    assert(pxa_install_transfer_decode(bytes(altered),NULL,&out)==PXA_STATUS_DENIED);
    memcpy(altered,first,sizeof(first));altered[18]=1;
    pxa_write_u32(altered+sizeof(altered)-4,pxa_container_crc32(altered,sizeof(altered)-4));
    assert(pxa_install_transfer_decode(bytes(altered),NULL,&out)==PXA_STATUS_DENIED);
    /* A V2 envelope cannot disguise a normal V1 cross-volume operation. */
    copying=fixture();assert(!pxa_install_transfer_encode(&copying,altered));pxa_write_u16(altered+4,2);
    pxa_write_u32(altered+sizeof(altered)-4,pxa_container_crc32(altered,sizeof(altered)-4));
    assert(pxa_install_transfer_decode(bytes(altered),NULL,&out)==PXA_STATUS_DENIED);
    puts("Install transfer: canonical receipt, all-byte corruption, source identity/CAS lineage, monotonic handoff and target generation projection passed");
    return 0;
}

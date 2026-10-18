#ifndef PXA_GUEST_FS_ROOTS_H
#define PXA_GUEST_FS_ROOTS_H
#include "pxa_fs.h"
#define PXA_FS_IO_READ_AT 16u
#define PXA_FS_IO_CANCEL_READ_AT 17u
#define PXA_FS_FILE_FEATURE_READ_AT UINT64_C(1)
#define PXA_FS_READ_AT_HEADER_BYTES 24u
#define PXA_FS_READ_AT_MAX_BYTES 4096u
/* packet is caller-owned header + destination. Bytes are returned in
 * packet[24..24+result); the sequential cursor is unchanged. Use strictly
 * increasing nonzero cookies for each FILE. No allocation or data copy. */
static inline int32_t pxa_fs_read_at(uint64_t handle,uint64_t offset,
    uint64_t cookie,uint8_t *packet,uint32_t packet_bytes) {
    if(!(handle>>32) || !cookie || !packet || packet_bytes<=24 ||
        packet_bytes>24+PXA_FS_READ_AT_MAX_BYTES ||
        offset>UINT64_MAX-(packet_bytes-24)) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u64(packet,offset);pxa_store_u64(packet+8,cookie);
    pxa_store_u32(packet+16,packet_bytes-24);pxa_store_u32(packet+20,0);
    int32_t n=pxa_io(handle,PXA_FS_IO_READ_AT,packet,packet_bytes);
    return n>0 && (uint32_t)n>packet_bytes-24 ? PXA_STATUS_PROTOCOL_ERROR : n;
}
static inline int32_t pxa_fs_cancel_read_at(uint64_t handle,uint64_t cookie) {
    uint8_t packet[8];
    if(!(handle>>32) || !cookie) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u64(packet,cookie);
    return pxa_io(handle,PXA_FS_IO_CANCEL_READ_AT,packet,8);
}

#define PXA_FS_OPEN_ROOT 9u
#define PXA_FS_REQUEST_ACCESS 10u
#define PXA_FS_OPEN_AT 11u
#define PXA_FS_STAT_AT 16u
#define PXA_FS_QUERY_ROOT 17u
#define PXA_FS_ROOT_APP_MEDIA 5u
#define PXA_FS_RIGHT_READ 1u
#define PXA_FS_RIGHT_ENUMERATE 2u
#define PXA_FS_RIGHT_WRITE 4u
#define PXA_FS_RIGHT_CREATE 8u
#define PXA_FS_RIGHT_REMOVE 16u
#define PXA_FS_ROOT_INFO_BYTES 72u
#define PXA_FS_SCOPED_MAX_PATH 1024u
typedef struct {
    int32_t status;
    uint64_t handle;
    uint32_t rights;
    uint8_t grant_id[16];
} pxa_fs_root_result_t;
typedef struct {
    int32_t status;
    uint64_t handle,features,size;
    uint8_t kind;
} pxa_fs_at_result_t;
typedef struct {
    int32_t status;
    uint8_t volume_id[16]; uint64_t mount_epoch;
    uint32_t rights; uint64_t capabilities;
    uint16_t max_path_bytes,max_segment_bytes;
    uint32_t max_read_at_bytes; uint8_t naming,state;
    uint16_t space_known; uint64_t total_bytes,free_bytes,quota_bytes;
} pxa_fs_root_info_result_t;

/* Host-selected, app-identity-scoped fixed media root. No user picker or
 * transferable Permission/GrantId. Requires app-media-root feature. */
static inline int32_t pxa_fs_request_app_media(uint64_t token) {
    uint8_t p[28]={0};
    if(!token) return -1;
    p[0]=PXA_FS_ROOT_APP_MEDIA;
    return pxa_control_call(PXA_FS_SERVICE,PXA_FS_OPEN_ROOT,token,p,sizeof(p));
}
static inline int32_t pxa_fs_request_open_grant(uint64_t token,uint64_t permission,
    const uint8_t grant_id[16]) {
    uint8_t p[28]={0};
    if(!token || !(permission>>32) || !grant_id) return -1;
    p[0]=4; pxa_store_u64(p+4,permission);
    for(unsigned i=0;i<16;++i) p[12+i]=grant_id[i];
    return pxa_control_call(PXA_FS_SERVICE,PXA_FS_OPEN_ROOT,token,p,sizeof(p));
}
static inline int32_t pxa_fs_request_directory(uint64_t token,uint64_t permission,
    const char *purpose,size_t length,uint32_t rights,int persistent) {
    uint8_t p[274]={0};
    if(!token || !(permission>>32) || !purpose || !length || length>256 ||
        !rights || (rights & ~31u)) return -1;
    for(size_t i=0;i<length;++i) { if(!purpose[i]) return -1; p[18+i]=(uint8_t)purpose[i]; }
    pxa_store_u64(p,permission); pxa_store_u32(p+8,rights);
    pxa_store_u32(p+12,persistent?1:0); pxa_store_u16(p+16,(uint16_t)length);
    return pxa_control_call(PXA_FS_SERVICE,PXA_FS_REQUEST_ACCESS,token,p,(uint32_t)(18+length));
}
static inline int32_t pxa_fs_request_open_at(uint64_t token,uint64_t directory,
    const char *path,size_t length,uint32_t flags) {
    uint8_t p[14+PXA_FS_SCOPED_MAX_PATH];
    if(!token || !(directory>>32) || !path || !length || length>PXA_FS_SCOPED_MAX_PATH ||
        !pxa_fs_flags_valid(flags)) return -1;
    for(size_t i=0;i<length;++i) { if(!path[i]) return -1; p[14+i]=(uint8_t)path[i]; }
    pxa_store_u64(p,directory); pxa_store_u32(p+8,flags); pxa_store_u16(p+12,(uint16_t)length);
    return pxa_control_call(PXA_FS_SERVICE,PXA_FS_OPEN_AT,token,p,(uint32_t)(14+length));
}
static inline int32_t pxa_fs_request_query_root(uint64_t token,uint64_t directory) {
    uint8_t p[8]; if(!token || !(directory>>32)) return -1;
    pxa_store_u64(p,directory); return pxa_control_call(PXA_FS_SERVICE,PXA_FS_QUERY_ROOT,token,p,8);
}
static inline int pxa_fs_parse_root(const pxa_event_t *e,uint64_t token,
    pxa_fs_root_result_t *out) {
    if(!e || !out || e->service!=PXA_FS_SERVICE || e->token!=token || !token ||
        (e->opcode!=PXA_FS_OPEN_ROOT && e->opcode!=PXA_FS_REQUEST_ACCESS) ||
        !e->payload || e->payload_size<4) return 0;
    pxa_zero(out,sizeof(*out)); out->status=(int32_t)pxa_load_u32(e->payload);
    if(out->status) return e->payload_size==4;
    if(e->payload_size!=(e->opcode==PXA_FS_OPEN_ROOT?16u:32u)) return 0;
    out->handle=pxa_load_u64(e->payload+4); out->rights=pxa_load_u32(e->payload+12);
    if(e->opcode==PXA_FS_REQUEST_ACCESS)
        for(unsigned i=0;i<16;++i) out->grant_id[i]=e->payload[16+i];
    return (out->handle>>32)!=0 && out->rights && !(out->rights & ~31u);
}
static inline int pxa_fs_parse_open_at(const pxa_event_t *e,uint64_t token,
    pxa_fs_at_result_t *out) {
    if(!e || !out || e->service!=PXA_FS_SERVICE || e->opcode!=PXA_FS_OPEN_AT ||
        e->token!=token || !token || !e->payload || e->payload_size<4) return 0;
    pxa_zero(out,sizeof(*out)); out->status=(int32_t)pxa_load_u32(e->payload);
    if(out->status) return e->payload_size==4;
    if(e->payload_size!=32 || e->payload[13] || e->payload[14] || e->payload[15]) return 0;
    out->handle=pxa_load_u64(e->payload+4); out->kind=e->payload[12];
    out->features=pxa_load_u64(e->payload+16); out->size=pxa_load_u64(e->payload+24);
    return (out->handle>>32)!=0 && (out->kind==1 || out->kind==2);
}
static inline int pxa_fs_parse_root_info(const pxa_event_t *e,uint64_t token,
    pxa_fs_root_info_result_t *out) {
    if(!e || !out || e->service!=PXA_FS_SERVICE || e->opcode!=PXA_FS_QUERY_ROOT ||
        e->token!=token || !token || !e->payload || e->payload_size<4) return 0;
    pxa_zero(out,sizeof(*out)); out->status=(int32_t)pxa_load_u32(e->payload);
    if(out->status) return e->payload_size==4;
    if(e->payload_size!=4+PXA_FS_ROOT_INFO_BYTES) return 0;
    const uint8_t *p=e->payload+4;
    for(unsigned i=0;i<16;++i) out->volume_id[i]=p[i];
    out->mount_epoch=pxa_load_u64(p+16); out->rights=pxa_load_u32(p+24);
    out->capabilities=pxa_load_u64(p+28); out->max_path_bytes=pxa_load_u16(p+36);
    out->max_segment_bytes=pxa_load_u16(p+38); out->max_read_at_bytes=pxa_load_u32(p+40);
    out->naming=p[44]; out->state=p[45]; out->space_known=pxa_load_u16(p+46);
    out->total_bytes=pxa_load_u64(p+48); out->free_bytes=pxa_load_u64(p+56);
    out->quota_bytes=pxa_load_u64(p+64);
    return out->mount_epoch!=0 && out->max_path_bytes && out->max_segment_bytes;
}
#endif

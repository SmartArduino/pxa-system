/* SPDX-License-Identifier: MIT
 * App-isolated media roots and a trusted browser file picker. */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "services.h"
#include "pxa/posix/pxa_scoped_fs.h"

static pxa_fs_roots_backend_t api,base;
static pxa_fs_service_t *service;
static pxa_scoped_fs_mount_t mount;
static pxa_scoped_fs_grant_t grant;
static pxa_component_t import_owner;
static pxa_request_token_t import_token;
static uint32_t sequence,active_import;
EM_JS(void, notify_import,(uint32_t id,int multiple,const uint8_t *extensions,size_t en,const uint8_t *purpose,size_t pn),{
    Module.browserNotify?.({type:'file-import',id,multiple:!!multiple,
        extensions:UTF8ToString(extensions,en),purpose:UTF8ToString(purpose,pn)});
});
EM_JS(void, notify_cancel,(uint32_t id),{Module.browserNotify?.({type:'file-import-cancel',id});});
static pxa_status_t open_root(void *c,uint8_t kind,const uint8_t id[16],void **directory,uint32_t *rights){
    (void)c;(void)id;if(kind!=PXA_FS_ROOT_APP_MEDIA)return PXA_STATUS_UNSUPPORTED;
    return base.open_root(base.context,PXA_FS_ROOT_GRANT,grant.id,directory,rights);
}
static pxa_status_t open_at(void *c,void *dir,pxa_bytes_t path,uint32_t flags,void **file,pxa_fs_entry_t *info,uint64_t *features){
    (void)c;return base.open_at(base.context,dir,path,flags,file,info,features);
}
static pxa_status_t stat_at(void *c,void *dir,pxa_bytes_t path,pxa_fs_entry_t *info){
    (void)c;return base.stat_at(base.context,dir,path,info);
}
static pxa_status_t query_root(void *c,void *dir,pxa_fs_root_info_t *info){
    (void)c;return base.query_root(base.context,dir,info);
}
static pxa_status_t media_path(void *c,char *out,size_t capacity,size_t *size){
    (void)c;const char path[]="浏览器 / 应用文件";
    if(capacity<sizeof(path)-1)return PXA_STATUS_RESOURCE_LIMIT;
    memcpy(out,path,sizeof(path)-1);*size=sizeof(path)-1;return PXA_STATUS_OK;
}
static pxa_status_t request_import(void *c,pxa_component_t owner,pxa_request_token_t token,uint32_t flags,pxa_bytes_t extensions,pxa_bytes_t purpose){
    (void)c;if(import_token)return PXA_STATUS_BUSY;
    import_owner=owner;import_token=token;do{active_import=++sequence;}while(!active_import);
    notify_import(active_import,flags&PXA_FS_IMPORT_MULTIPLE,extensions.data,extensions.size,purpose.data,purpose.size);
    return PXA_STATUS_OK;
}
static void cancel_import(void *c,pxa_component_t owner,pxa_request_token_t token){
    (void)c;if(owner==import_owner&&token==import_token){notify_cancel(active_import);import_token=0;active_import=0;}
}
EMSCRIPTEN_KEEPALIVE int browser_import_active(uint32_t id){return import_token&&active_import==id;}
EMSCRIPTEN_KEEPALIVE int browser_import_finish(uint32_t id,int status,uint32_t imported,uint32_t failed,uint32_t skipped,int error){
    if(!import_token||active_import!=id)return PXA_STATUS_CANCELLED;
    pxa_component_t owner=import_owner;pxa_request_token_t token=import_token;
    import_token=0;active_import=0;
    pxa_fs_import_summary_t summary={imported,failed,skipped,error};
    return pxa_fs_import_complete(service,owner,token,status,&summary);
}
void browser_files_bind(pxa_fs_service_t *fs){service=fs;}
pxa_status_t browser_files_init(const pxa_fs_roots_backend_t **output){
    mkdir("/data/media",0700);
    pxa_volume_registry_t *volumes;size_t size=pxa_volume_registry_workspace_size(1);
    pxa_status_t s=pxa_volume_registry_init(calloc(1,size),size,1,1,&volumes);if(s)return s;
    pxa_volume_info_t volume={0};volume.id.bytes[0]=1;volume.device_id=volume.id;
    volume.state=PXA_VOLUME_READY;volume.identity_strength=PXA_VOLUME_ID_MEDIA_BOUND;
    volume.capabilities=PXA_VOLUME_READ|PXA_VOLUME_RANDOM_READ;
    strcpy(volume.name,"Browser app files");
    s=pxa_volume_register(volumes,&volume,&mount.mount_epoch);if(s)return s;
    mount.host_path="/media";
    grant.id[0]=1;grant.volume_id=volume.id;grant.revision=1;
    grant.rights=PXA_FS_RIGHT_READ|PXA_FS_RIGHT_ENUMERATE;grant.active=1;
    pxa_scoped_fs_config_t config={.struct_size=sizeof(config),.volumes=volumes,
        .mounts=&mount,.mount_count=1,.grants=&grant,.grant_count=1,.max_open_resources=64};
    pxa_scoped_fs_t *scoped;size=pxa_scoped_fs_workspace_size(&config);
    s=pxa_scoped_fs_init(calloc(1,size),size,&config,&scoped,&base);if(s)return s;
    api=base;api.open_root=open_root;api.open_at=open_at;api.stat_at=stat_at;api.query_root=query_root;
    api.app_media_path=media_path;api.request_import=request_import;api.cancel_import=cancel_import;
    api.features=PXA_FS_FEATURE_APP_MEDIA_ROOT|PXA_FS_FEATURE_APP_MEDIA_PATH|PXA_FS_FEATURE_FILE_IMPORT;
    *output=&api;return PXA_STATUS_OK;
}

/* SPDX-License-Identifier: MIT */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "services.h"
#include "pxa/device.h"
#include "pxa/log.h"
#include "pxa/ipc.h"
#include "pxa/wasi.h"
#include "pxa/service.h"
#include "pxa/posix/pxa_posix_fs.h"
#include "pxa/posix/pxa_posix_storage.h"

#define CHECK(call) do {pxa_status_t status=(call);if(status)return status;} while(0)
#define BYTES(text) {(const uint8_t *)text,sizeof(text)-1}
static pxa_permission_service_t *permissions;
static pxa_fs_backend_t native_fs;
static pxa_storage_backend_t native_kv;
EM_JS(void, storage_changed, (), {Module.browserStorageChanged?.();});
static pxa_status_t kv_set(void *c,pxa_bytes_t key,pxa_bytes_t value) {
    pxa_status_t s=native_kv.set(c,key,value);if(!s)storage_changed();return s;
}
static pxa_status_t kv_remove(void *c,pxa_bytes_t key) {
    pxa_status_t s=native_kv.remove(c,key);if(!s)storage_changed();return s;
}
static pxa_status_t file_write(void *c,void *file,const uint8_t *in,size_t size,size_t *written) {
    pxa_status_t s=native_fs.write(c,file,in,size,written);if(!s)storage_changed();return s;
}
static pxa_status_t file_open(void *c,pxa_bytes_t path,uint32_t flags,void **out,uint8_t *kind) {
    pxa_status_t s=native_fs.open(c,path,flags,out,kind);
    if(!s && (flags & (PXA_FS_OPEN_CREATE|PXA_FS_OPEN_TRUNCATE)))storage_changed();return s;
}
static pxa_status_t file_mkdir(void *c,pxa_bytes_t path) {
    pxa_status_t s=native_fs.make_directory(c,path);if(!s)storage_changed();return s;
}
static pxa_status_t file_remove(void *c,pxa_bytes_t path) {
    pxa_status_t s=native_fs.remove(c,path);if(!s)storage_changed();return s;
}
static pxa_status_t file_rename(void *c,pxa_bytes_t from,pxa_bytes_t to) {
    pxa_status_t s=native_fs.rename(c,from,to);if(!s)storage_changed();return s;
}
static pxa_status_t file_replace(void *c,pxa_bytes_t from,pxa_bytes_t to) {
    pxa_status_t s=native_fs.replace(c,from,to);if(!s)storage_changed();return s;
}
static pxa_permission_declaration_t declarations[9]={
    {BYTES("audio.playback"),BYTES("media"),0}};
static uint8_t origins[8][512];
EM_JS(int, network_origin, (unsigned i,uint8_t *out,size_t capacity), {
    const text=Module.browserNetworkOrigins?.[i];if(!text)return 0;
    const data=new TextEncoder().encode(text);if(data.length>capacity)return 0;
    HEAPU8.set(data,out);return data.length;
});
static pxa_status_t permission_load(void *c,pxa_bytes_t identity,pxa_bytes_t name,
    pxa_bytes_t scope,pxa_permission_decision_t *decision) {
    (void)c;(void)identity;
    /* AudioContext still enforces browser user activation; no hardware or
     * network authority is granted by this local playback declaration. */
    *decision=(name.size==14 && !memcmp(name.data,"audio.playback",14)) ? PXA_PERMISSION_ALLOW : PXA_PERMISSION_DENY;
    if(name.size==10&&!memcmp(name.data,"net.client",10))
        for(unsigned i=1;i<9;i++)if(declarations[i].scope.size==scope.size&&scope.size&&
            !memcmp(declarations[i].scope.data,scope.data,scope.size))*decision=PXA_PERMISSION_ALLOW;
    return PXA_STATUS_OK;
}
static pxa_status_t permission_save(void *c,pxa_bytes_t i,pxa_bytes_t n,pxa_bytes_t s,pxa_permission_decision_t d) {
    (void)c;(void)i;(void)n;(void)s;(void)d;return PXA_STATUS_UNSUPPORTED;
}
static pxa_status_t get_mac(void *c,uint16_t kind,uint8_t out[6],uint32_t *flags) {
    (void)c;(void)kind;(void)out;(void)flags;return PXA_STATUS_UNSUPPORTED;
}
EM_JS(void, write_log, (int level,const uint8_t *text,size_t size), {
    Module.browserNotify?.({type:'log',level,message:UTF8ToString(text,size)});
});
static pxa_status_t log_write(void *c,pxa_component_t component,pxa_bytes_t app,
                             pxa_log_level_t level,pxa_bytes_t message) {
    (void)c;(void)component;(void)app;write_log(level,message.data,message.size);return PXA_STATUS_OK;
}
static void *allocate(void *c,size_t size) {(void)c;return malloc(size);}
static void release(void *c,void *p) {(void)c;free(p);}
static pxa_status_t wasi_control(void *c,pxa_runtime_t *r,pxa_component_t id,const pxa_message_view_t *m) {
    (void)c;(void)r;(void)id;(void)m;return PXA_STATUS_UNSUPPORTED;
}
pxa_status_t browser_services_init(pxa_runtime_t *runtime) {
    pxa_service_ops_t wasi={.struct_size=sizeof(wasi),.service_id=PXA_WASI_SERVICE_ID,
        .major=PXA_WASI_SERVICE_MAJOR,.minor=PXA_WASI_SERVICE_MINOR,
        .features=PXA_WASI_FEATURE_STDIO|PXA_WASI_FEATURE_CLOCKS|PXA_WASI_FEATURE_RANDOM|
                  PXA_WASI_FEATURE_ARGUMENTS|PXA_WASI_FEATURE_ENVIRONMENT,.control=wasi_control};
    CHECK(pxa_service_register(runtime,&wasi));
    unsigned declaration_count=1;
    for(unsigned i=0;i<8;i++){
        int n=network_origin(i,origins[i],sizeof(origins[i]));if(!n)break;
        declarations[declaration_count++]=(pxa_permission_declaration_t){BYTES("net.client"),{origins[i],(size_t)n},0};
    }
    pxa_permission_config_t pc={.struct_size=sizeof(pc),.app_identity=BYTES("browser.app"),
        .declarations=declarations,.declaration_count=declaration_count,.max_authorities=16,
        .store={.struct_size=sizeof(pxa_permission_store_t),.load=permission_load,.save=permission_save}};
    size_t size=pxa_permission_service_workspace_size(&pc);
    CHECK(pxa_permission_service_init(calloc(1,size),size,runtime,&pc,&permissions));
    CHECK(pxa_permission_policy_load(permissions));CHECK(pxa_permission_service_register(permissions));
    pxa_device_config_t dc={.struct_size=sizeof(dc),.permissions=permissions,.get_mac=get_mac,
        .target="browser",.architecture="wasm32",.engine="webassembly",.engine_abi="pxa.core.v2",.formats=PXA_DEVICE_FORMAT_WASM};
    pxa_device_service_t *device;size=pxa_device_service_workspace_size(&dc);
    CHECK(pxa_device_service_init(calloc(1,size),size,runtime,&dc,&device));CHECK(pxa_device_service_register(device));
    pxa_log_config_t lc={.struct_size=sizeof(lc),.write=log_write,.app_id=BYTES("browser.app"),.max_message_bytes=256};
    pxa_log_service_t *log;size=pxa_log_service_workspace_size(&lc);
    CHECK(pxa_log_service_init(calloc(1,size),size,runtime,&lc,&log));CHECK(pxa_log_service_register(log));
    pxa_ipc_limits_t limits;pxa_ipc_limits_init(&limits);
    limits.max_endpoints=4;limits.max_pending_calls=16;
    pxa_ipc_broker_t *ipc;size=pxa_ipc_broker_workspace_size(&limits);
    CHECK(pxa_ipc_broker_init(calloc(1,size),size,runtime,&limits,&ipc));
    CHECK(pxa_ipc_broker_register(ipc));CHECK(pxa_ipc_broker_set_allocator(ipc,NULL,allocate,release));
    /* IDBFS mounts /data before Core initializes. All paths below belong only
     * to this application; guest code has no direct libc/host FS imports. */
    mkdir("/data",0700);mkdir("/data/kv",0700);mkdir("/data/files",0700);
    pxa_posix_storage_config_t kv={.struct_size=sizeof(kv),.root_path="/data/kv",
        .max_keys=128,.max_value_bytes=PXA_STORAGE_MAX_VALUE_BYTES,.quota_bytes=256*1024};
    pxa_posix_storage_t *posix_storage;pxa_storage_config_t sc={.struct_size=sizeof(sc),.max_value_bytes=PXA_STORAGE_MAX_VALUE_BYTES};
    size=pxa_posix_storage_workspace_size(&kv);
    CHECK(pxa_posix_storage_init(calloc(1,size),size,&kv,&posix_storage,&sc.backend));
    native_kv=sc.backend;sc.backend.set=kv_set;sc.backend.remove=kv_remove;
    pxa_storage_service_t *storage;size=pxa_storage_service_workspace_size(&sc);
    CHECK(pxa_storage_service_init(calloc(1,size),size,runtime,&sc,&storage));CHECK(pxa_storage_service_register(storage));
    pxa_posix_fs_config_t pfc={.struct_size=sizeof(pfc),.root_path="/data/files",.quota_bytes=8*1024*1024,.max_open_resources=16};
    pxa_posix_fs_t *posix_fs;pxa_fs_config_t fc={.struct_size=sizeof(fc),.max_open_resources=16};
    size=pxa_posix_fs_workspace_size(&pfc);
    CHECK(pxa_posix_fs_init(calloc(1,size),size,&pfc,&posix_fs,&fc.backend));
    native_fs=fc.backend;fc.backend.write=file_write;fc.backend.open=file_open;
    fc.backend.make_directory=file_mkdir;fc.backend.remove=file_remove;
    fc.backend.rename=file_rename;fc.backend.replace=file_replace;
    pxa_fs_service_t *fs;size=pxa_fs_service_workspace_size(&fc);
    CHECK(pxa_fs_service_init(calloc(1,size),size,runtime,&fc,&fs));CHECK(pxa_fs_service_register(fs));
    CHECK(browser_net_init(runtime,permissions));
    return browser_audio_init(runtime,permissions);
}

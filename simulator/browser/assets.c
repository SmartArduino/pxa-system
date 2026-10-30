/* SPDX-License-Identifier: MIT
 * Verified package resources, using the same cache/loaders as device Hosts.
 * Jobs run on the application's Worker, outside Guest callbacks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "services.h"
#include "pxa/assets.h"
#include "pxa/asset_read.h"
#include "pxa/image/codecs.h"

static pxa_assets_service_t *service;
static pxa_asset_catalog_t catalog;
static pxa_asset_cache_t *cache;
static pxa_asset_read_queue_t reads;
static pxa_memory_budget_t budget;
static pxa_memory_allocator_t pixels,temporary;
static const uint8_t package_key[32]={1};
static int has_catalog;
static void *raw_allocate(void *c,size_t n){(void)c;return malloc(n);}
static void raw_release(void *c,void *p){(void)c;free(p);}
static void *asset_allocate(void *c,size_t n){(void)c;return pxa_memory_allocate(&pixels,n);}
static void asset_release(void *c,void *p){(void)c;pxa_memory_release(p);}

static uint8_t *read_file(const char *path,size_t maximum,size_t *length){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)||ftell(f)<0){fclose(f);return NULL;}
    long n=ftell(f);if((size_t)n>maximum||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    uint8_t *p=malloc(n?n:1);
    if(!p||fread(p,1,n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
    fclose(f);*length=n;return p;
}
static pxa_status_t init_catalog(void){
    size_t size=0;uint8_t *index=read_file("/package/assets/resources.pxi",1024*1024,&size);
    if(!index)return PXA_STATUS_OK;
    size_t manifest_size=0;
    uint8_t *encoded=read_file("/package/manifest.pxm",65536,&manifest_size);
    if(!encoded){free(index);return PXA_STATUS_PROTOCOL_ERROR;}
    pxa_package_limits_t limits,needed;pxa_package_limits_init(&limits);
    limits.max_files=4096;
    pxa_status_t s=pxa_package_manifest_measure((pxa_bytes_t){encoded,manifest_size},&limits,&needed);
    if(s){free(encoded);free(index);return s;}
    size_t bytes=pxa_package_manifest_workspace_size(&needed);void *workspace=calloc(1,bytes);
    pxa_package_manifest_t *manifest=NULL;
    s=workspace?pxa_package_manifest_parse(workspace,bytes,(pxa_bytes_t){encoded,manifest_size},&needed,&manifest):PXA_STATUS_RESOURCE_LIMIT;
    if(!s)s=pxa_asset_catalog_init(&catalog,(pxa_bytes_t){index,size},manifest);
    if(s){free(workspace);free(encoded);free(index);return s;}
    /* Catalog borrows both the parsed manifest and its verified bytes. */
    has_catalog=1;return PXA_STATUS_OK;
}
static pxa_status_t find(void *c,pxa_component_t owner,pxa_bytes_t path,pxa_asset_info_t *info){
    (void)c;(void)owner;
    return has_catalog?pxa_asset_catalog_find(&catalog,path,info):PXA_STATUS_NOT_FOUND;
}
pxa_status_t browser_asset_info(pxa_bytes_t path,pxa_asset_info_t *info){return find(NULL,0,path,info);}
static pxa_status_t request(void *c,pxa_component_t owner,pxa_bytes_t path,uint8_t cls,pxa_asset_ticket_t *ticket){
    pxa_asset_info_t info;pxa_status_t s=find(c,owner,path,&info);if(s)return s;
    return pxa_asset_cache_request(cache,owner,package_key,&info,cls,ticket);
}
static pxa_status_t prefetch(void *c,pxa_component_t owner,pxa_bytes_t path,uint8_t cls,pxa_asset_ticket_t *ticket){
    pxa_asset_info_t info;pxa_status_t s=find(c,owner,path,&info);if(s)return s;
    return pxa_asset_cache_prefetch(cache,owner,package_key,&info,cls,ticket);
}
static pxa_status_t inspect(void *c,pxa_component_t owner,pxa_bytes_t path,pxa_asset_request_state_t *state){
    pxa_asset_info_t info;pxa_status_t s=find(c,owner,path,&info);if(s)return s;
    return pxa_asset_cache_inspect(cache,owner,package_key,&info,pxa_asset_default_memory_class(&info),state);
}
static pxa_status_t query(void *c,pxa_component_t owner,pxa_asset_ticket_t ticket,pxa_asset_request_state_t *state){
    (void)c;return pxa_asset_cache_query(cache,owner,ticket,state);
}
static pxa_status_t acquire(void *c,pxa_component_t owner,pxa_asset_ticket_t ticket,pxa_raster_asset_t **out){
    (void)c;return pxa_asset_cache_acquire(cache,owner,ticket,out);
}
static void release(void *c,pxa_component_t owner,pxa_asset_ticket_t ticket){
    (void)c;pxa_asset_cache_release(cache,owner,ticket);
}
static pxa_status_t read_asset(void *c,pxa_component_t owner,pxa_bytes_t path,uint32_t offset,uint32_t count,uint64_t *ticket){
    (void)c;if(!has_catalog)return PXA_STATUS_NOT_FOUND;
    pxa_asset_blob_block_t block;pxa_status_t s=pxa_asset_catalog_blob_block(&catalog,path,offset,&block);
    return s?s:pxa_asset_read_begin(&reads,owner,&block,offset,count,ticket);
}
static pxa_status_t read_result(void *c,pxa_component_t owner,uint64_t ticket,pxa_bytes_t *out){
    (void)c;return pxa_asset_read_result(&reads,owner,ticket,out);
}
static void read_release(void *c,pxa_component_t owner,uint64_t ticket){
    (void)c;pxa_asset_read_release(&reads,owner,ticket);
}
static pxa_status_t read_input(void *c,uint8_t *out,size_t count,size_t *size){
    *size=fread(out,1,count,c);return ferror((FILE *)c)?PXA_STATUS_IO_ERROR:PXA_STATUS_OK;
}
static FILE *open_asset(const pxa_asset_info_t *info,uint32_t offset){
    if(!pxa_asset_path_valid(info->path))return NULL;
    char path[sizeof("/package/")+PXA_ASSET_PATH_MAX];
    memcpy(path,"/package/",9);memcpy(path+9,info->path.data,info->path.size);path[9+info->path.size]=0;
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)||ftell(f)!=(long)info->stored_bytes||fseek(f,offset,SEEK_SET)){fclose(f);return NULL;}
    return f;
}
static void finish_release(pxa_asset_job_t *job){
    pxa_raster_asset_release(job->asset);pxa_asset_cache_finish_release(cache,job->token);
}
void browser_assets_poll(void){
    if(!cache)return;
    /* Bound work per turn; preserve the cache's normal pin/cancel semantics. */
    for(unsigned turn=0;turn<4;turn++){
        pxa_asset_read_job_t read;
        if(pxa_asset_read_next(&reads,&read)==PXA_STATUS_OK){
            if(read.kind==PXA_ASSET_READ_JOB_RELEASE){pxa_memory_release(read.buffer);pxa_asset_read_freed(&reads,read.token);}
            else{
                uint8_t *result=NULL;size_t bytes=0;FILE *f=open_asset(&read.block.info,read.block.offset);
                pxa_asset_input_t input={f,read_input,NULL,NULL};
                pxa_status_t s=f?pxa_asset_read_load(&read,&input,&temporary,&result,&bytes):PXA_STATUS_NOT_FOUND;
                if(f)fclose(f);pxa_asset_read_finish(&reads,read.token,s,result,bytes);
            }
        }
        pxa_asset_job_t job,discard={0};
        if(pxa_asset_cache_next_job(cache,&job)!=PXA_STATUS_OK)break;
        if(job.kind==PXA_ASSET_JOB_RELEASE){finish_release(&job);continue;}
        if(job.kind==PXA_ASSET_JOB_LOAD){
            pxa_raster_asset_t *asset=NULL;FILE *f=open_asset(&job.info,0);
            pxa_asset_input_t input={f,read_input,NULL,NULL};pxa_status_t s=PXA_STATUS_NOT_FOUND;
            if(f){
                if(job.info.encoding==PXA_ASSET_ENCODING_PNG||job.info.encoding==PXA_ASSET_ENCODING_PNG_RGB565)
                    s=pxa_asset_load_png(&job.info,&input,&pixels,&temporary,pxa_lvgl_image_backend(),&asset);
                else s=pxa_asset_load_resident(&job.info,&input,asset_allocate,asset_release,NULL,&asset);
                fclose(f);
            }
            pxa_asset_cache_finish_load(cache,job.token,s,asset,&discard);
            if(discard.kind==PXA_ASSET_JOB_RELEASE)finish_release(&discard);
        }
    }
    pxa_assets_service_poll(service);
}
pxa_status_t browser_assets_init(pxa_runtime_t *runtime){
    pxa_memory_budget_config_t bc={.limit={2*1024*1024,64*1024*1024},.temporary_limit={2*1024*1024,16*1024*1024}};
    pxa_status_t s=pxa_memory_budget_init(&budget,&bc);if(s)return s;
    size_t limit[]={2*1024*1024,64*1024*1024};pxa_memory_owner_t owner;
    s=pxa_memory_owner_open(&budget,limit,&owner);if(s)return s;
    pixels=(pxa_memory_allocator_t){&budget,owner,PXA_MEMORY_EXTERNAL,PXA_MEMORY_IMAGE,NULL,raw_allocate,raw_release};
    temporary=pixels;temporary.kind=PXA_MEMORY_TEMPORARY;
    pxa_asset_read_init(&reads,&temporary);
    s=init_catalog();if(s)return s;
    pxa_asset_cache_config_t cc={.max_entries=128,.max_requests=64,.max_requests_per_owner=64,.max_pending=32,
        .resident_limit={1024*1024,48*1024*1024},.owner_limit={1024*1024,48*1024*1024}};
    size_t size=pxa_asset_cache_workspace_size(&cc);
    s=pxa_asset_cache_init(calloc(1,size),size,&cc,&cache);if(s)return s;
    pxa_assets_config_t config={.struct_size=sizeof(config),.max_pending=32,.max_pending_per_component=32,
        .max_resources=128,.max_resources_per_component=128,
        .backend={.find=find,.request=request,.query=query,.acquire=acquire,.release=release,.prefetch=prefetch,
            .inspect=inspect,.read=read_asset,.read_result=read_result,.read_release=read_release},
        .image_allocator=&pixels,.image_temporary_allocator=&temporary,.image_backend=pxa_lvgl_image_backend()};
    size=pxa_assets_service_workspace_size(&config);
    s=pxa_assets_service_init(calloc(1,size),size,runtime,&config,&service);
    return s?s:pxa_assets_service_register(service);
}

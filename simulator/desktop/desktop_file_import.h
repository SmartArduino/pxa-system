/* SPDX-License-Identifier: MIT
 * Included after desktop_media_root_t. All calls belong to the LVGL/runtime
 * owner thread, including Core's native-cooperate pump. */
#include <mntent.h>
#include "pxsys/theme.h"
#define DESKTOP_IMPORT_VOLUMES 64u
typedef struct desktop_file_import {
    pxa_lvgl_file_picker_t *picker;
    pxa_file_import_t job;
    pxa_import_destination_t destination;
    uint8_t destination_open;
    pxa_scoped_fs_t *source;
    pxa_fs_roots_backend_t source_api;
    pxa_scoped_fs_mount_t mounts[DESKTOP_IMPORT_VOLUMES];
    struct stat mounted[DESKTOP_IMPORT_VOLUMES];
    uint16_t mount_count;
    pxa_scoped_fs_grant_t unused_grant;
    void *source_memory,*volumes_memory;
    pxa_component_t component;
    pxa_request_token_t token;
    uint8_t started;
} desktop_file_import_t;
static void desktop_media_import_theme(desktop_media_root_t *r,const pxa_lvgl_ui_theme_t *t,
    const pxsys_theme_snapshot_t *system_theme,const char *locale) {
    pxa_lvgl_dialog_style_t *s=&r->picker_config.style;
#define IMPORT_COLOR(field,role) s->field=lv_color_hex(t->rgba[PXA_UI_THEME_##role]>>8)
    IMPORT_COLOR(surface,SURFACE);IMPORT_COLOR(container,SURFACE_CONTAINER);
    IMPORT_COLOR(panel,SURFACE_CONTAINER_HIGH);IMPORT_COLOR(text,TEXT);IMPORT_COLOR(muted,MUTED);
    IMPORT_COLOR(outline,OUTLINE_VARIANT);IMPORT_COLOR(primary,PRIMARY);IMPORT_COLOR(on_primary,ON_PRIMARY);
    IMPORT_COLOR(primary_container,PRIMARY_CONTAINER);IMPORT_COLOR(on_primary_container,ON_PRIMARY_CONTAINER);
    IMPORT_COLOR(error_container,ERROR_CONTAINER);IMPORT_COLOR(on_error_container,ON_ERROR_CONTAINER);
#undef IMPORT_COLOR
    s->spacing=system_theme?system_theme->base_spacing_px:4;
    s->radius=system_theme?system_theme->base_radius_px:4;s->label_font=t->label_font;
    r->picker_config.title_font=t->title_font;r->picker_config.caption_font=t->caption_font;
    r->picker_config.body_font=t->body_font;r->picker_config.chinese=!strncmp(locale,"zh",2);
}
static pxa_status_t desktop_import_emit(void *context,size_t index,uint8_t kind,pxa_bytes_t name) {
    pxa_file_picker_entry_t *rows=context;
    if(name.size>PXA_FS_SCOPED_MAX_PATH_BYTES)return PXA_STATUS_LIMIT_EXCEEDED;
    rows[index].name=malloc(name.size+1);if(!rows[index].name)return PXA_STATUS_RESOURCE_LIMIT;
    rows[index].kind=kind;rows[index].epoch=0;
    memcpy(rows[index].name,name.data,name.size);rows[index].name[name.size]=0;return PXA_STATUS_OK;
}
static pxa_status_t desktop_import_browse(void *context,uint64_t epoch,const char *path,const char *filter,
    size_t skip,pxa_file_picker_entry_t *rows,size_t capacity,size_t *count,uint8_t *more) {
    desktop_media_root_t *r=context;desktop_file_import_t *f=r->import;
    if(!epoch) {
        *count=0;*more=0;
        for(size_t i=skip;i<f->mount_count;++i) {
            if(*count==capacity) {*more=1;break;}
            pxa_file_picker_entry_t *row=&rows[(*count)++];row->epoch=f->mounts[i].mount_epoch;row->kind=PXA_FS_KIND_DIRECTORY;
            row->name=strdup(f->mounts[i].host_path);if(!row->name) {--*count;return PXA_STATUS_RESOURCE_LIMIT;}
        }
        return PXA_STATUS_OK;
    }
    for(unsigned i=0;i<f->mount_count;++i)if(f->mounts[i].mount_epoch==epoch) {
        struct stat current;
        if(stat(f->mounts[i].host_path,&current) || current.st_dev!=f->mounted[i].st_dev || current.st_ino!=f->mounted[i].st_ino)
            return PXA_STATUS_UNAVAILABLE;
    }
    return pxa_file_browser_page(f->source,&f->source_api.files,epoch,path,filter,skip,capacity,count,more,desktop_import_emit,rows);
}
static void desktop_media_import_cleanup(desktop_media_root_t *r) {
    desktop_file_import_t *f=r->import;if(!f)return;
    if(f->started)pxa_file_import_cancel(&f->job);
    pxa_lvgl_file_picker_destroy(f->picker);
    if(f->destination_open)pxa_import_destination_close(&f->destination);
    if(f->source)pxa_scoped_fs_deinit(f->source);
    for(unsigned i=0;i<f->mount_count;++i)free((void *)f->mounts[i].host_path);
    free(f->source_memory);free(f->volumes_memory);free(f);r->import=NULL;
}
static void desktop_media_cancel_import(void *context,pxa_component_t component,pxa_request_token_t token) {
    desktop_media_root_t *r=context;
    if(r->import && r->import->component==component && r->import->token==token)desktop_media_import_cleanup(r);
}
static pxa_status_t desktop_import_mount(desktop_file_import_t *f,pxa_volume_registry_t *volumes,const char *path) {
    if(!path)return PXA_STATUS_OK;
    char *real=realpath(path,NULL);struct stat info;
    if(!real)return PXA_STATUS_OK;
    if(stat(real,&info) || !S_ISDIR(info.st_mode)) {free(real);return PXA_STATUS_OK;}
    for(unsigned i=0;i<f->mount_count;++i)if(!strcmp(f->mounts[i].host_path,real)) {free(real);return PXA_STATUS_OK;}
    if(f->mount_count==DESKTOP_IMPORT_VOLUMES) {free(real);return PXA_STATUS_RESOURCE_LIMIT;}
    pxa_volume_info_t volume={0};volume.id.bytes[0]=42;volume.id.bytes[1]=(uint8_t)(f->mount_count+1);
    volume.device_id=volume.id;volume.state=PXA_VOLUME_READY;
    volume.identity_strength=PXA_VOLUME_ID_MEDIA_BOUND;volume.capabilities=PXA_VOLUME_READ|PXA_VOLUME_RANDOM_READ;
    snprintf(volume.name,sizeof(volume.name),"Filesystem %u",f->mount_count);
    pxa_scoped_fs_mount_t *mount=&f->mounts[f->mount_count];
    pxa_status_t status=pxa_volume_register(volumes,&volume,&mount->mount_epoch);
    if(status) {free(real);return status;}
    mount->host_path=real;f->mounted[f->mount_count++]=info;return PXA_STATUS_OK;
}
static pxa_status_t desktop_media_request_import(void *context,pxa_component_t component,pxa_request_token_t token,
    uint32_t flags,pxa_bytes_t extensions,pxa_bytes_t purpose) {
    desktop_media_root_t *r=context;
    if(r->import)return PXA_STATUS_BUSY;
    desktop_file_import_t *f=calloc(1,sizeof(*f));if(!f)return PXA_STATUS_RESOURCE_LIMIT;
    r->import=f;f->component=component;f->token=token;
    pxa_volume_registry_t *volumes;
    size_t bytes=pxa_volume_registry_workspace_size(DESKTOP_IMPORT_VOLUMES);
    f->volumes_memory=malloc(bytes);
    pxa_status_t status=PXA_STATUS_RESOURCE_LIMIT;
    if(!f->volumes_memory)goto fail;
    status=pxa_volume_registry_init(f->volumes_memory,bytes,DESKTOP_IMPORT_VOLUMES,1,&volumes);if(status)goto fail;
    status=desktop_import_mount(f,volumes,getenv("HOME"));if(status)goto fail;
    status=desktop_import_mount(f,volumes,"/");if(status)goto fail;
    /* Register separate mounts instead of weakening the scoped adapter's
     * no-cross-device rule. This includes removable media and /tmp. */
    FILE *mounts=setmntent("/proc/self/mounts","r");
    if(mounts) {
        struct mntent *entry;
        while((entry=getmntent(mounts))) {
            const char *path=entry->mnt_dir;
            if(!strncmp(path,"/proc",5) || !strncmp(path,"/sys",4) || !strncmp(path,"/dev",4))continue;
            status=desktop_import_mount(f,volumes,path);if(status)break;
        }
        endmntent(mounts);if(status)goto fail;
    }
    pxa_scoped_fs_config_t config={.struct_size=sizeof(config),.volumes=volumes,.mounts=f->mounts,.mount_count=f->mount_count,
        .grants=&f->unused_grant,.grant_count=1,.max_open_resources=2};
    bytes=pxa_scoped_fs_workspace_size(&config);f->source_memory=malloc(bytes);
    if(!f->source_memory) {status=PXA_STATUS_RESOURCE_LIMIT;goto fail;}
    status=pxa_scoped_fs_init(f->source_memory,bytes,&config,&f->source,&f->source_api);if(status)goto fail;
    char filter[PXA_FS_IMPORT_FILTER_MAX+1],reason[PXA_FS_IMPORT_PURPOSE_MAX+1];
    memcpy(filter,extensions.data,extensions.size);filter[extensions.size]=0;
    memcpy(reason,purpose.data,purpose.size);reason[purpose.size]=0;
    pxa_lvgl_file_picker_config_t picker=r->picker_config;
    picker.context=r;picker.browse=desktop_import_browse;picker.app_name=r->app_name;
    picker.extensions=filter;picker.purpose=reason;picker.multiple=(flags&PXA_FS_IMPORT_MULTIPLE)!=0;
    f->picker=pxa_lvgl_file_picker_create(&picker);
    if(!f->picker) {status=PXA_STATUS_RESOURCE_LIMIT;goto fail;}
    r->import_input_release=1;
    return PXA_STATUS_OK;
fail:
    desktop_media_import_cleanup(r);return status;
}
static pxa_status_t desktop_import_open_source(void *context,const pxa_file_choice_t *choice,void **resource,pxa_fs_entry_t *entry) {
    desktop_media_root_t *r=context;
    desktop_file_import_t *f=r->import;
    for(unsigned i=0;i<f->mount_count;++i)if(f->mounts[i].mount_epoch==choice->epoch) {
        struct stat current;
        if(stat(f->mounts[i].host_path,&current) || current.st_dev!=f->mounted[i].st_dev || current.st_ino!=f->mounted[i].st_ino)
            return PXA_STATUS_UNAVAILABLE;
    }
    return pxa_scoped_fs_open_trusted(f->source,choice->epoch,choice->path,PXA_FS_KIND_REGULAR,resource,entry);
}
static pxa_status_t desktop_import_ready(void *context) {
    desktop_media_root_t *r=context;struct stat info;
    return lstat(r->path,&info)==0 && S_ISDIR(info.st_mode)?PXA_STATUS_OK:PXA_STATUS_UNAVAILABLE;
}
static pxa_status_t desktop_import_flush(void *context,void *resource) {
    desktop_media_root_t *r=context;return pxa_import_destination_flush(&r->import->destination,resource);
}
static int desktop_import_same_file(void *context,const pxa_file_choice_t *choice) {
    desktop_media_root_t *r=context;char resolved[4096];
    if(!realpath(r->path,resolved))return 0;
    desktop_file_import_t *f=r->import;
    for(unsigned i=0;i<f->mount_count;++i)if(f->mounts[i].mount_epoch==choice->epoch) {
        char source[4096];int n=snprintf(source,sizeof(source),"%s/%s",f->mounts[i].host_path,choice->path);
        if(n<0 || (size_t)n>=sizeof(source))return 0;
        char *parent=strrchr(source,'/');if(!parent)return 0;*parent=0;
        char canonical[4096];return realpath(source,canonical) && !strcmp(canonical,resolved);
    }
    return 0;
}
static pxa_status_t desktop_import_start(desktop_media_root_t *r) {
    desktop_file_import_t *f=r->import;
    pxa_file_import_config_t copy={.context=r,.source=f->source_api.files,.open_source=desktop_import_open_source,
        .destination_ready=desktop_import_ready,.flush_destination=desktop_import_flush,.same_file=desktop_import_same_file,.maximum_name_bytes=255};
    pxa_status_t status=pxa_import_destination_open(&f->destination,r->path,&copy.destination);
    if(status)return status;
    f->destination_open=1;
    size_t count;const pxa_file_choice_t *choices=pxa_lvgl_file_picker_selection(f->picker,&count);
    uint64_t nonce=((uint64_t)SDL_GetPerformanceCounter()^(uintptr_t)f)|1;
    status=pxa_file_import_begin(&f->job,&copy,choices,count,nonce);
    if(!status)f->started=1;
    return status;
}
static void desktop_media_import_poll(desktop_media_root_t *r) {
    if(r->service)pxa_fs_service_poll(r->service);
    desktop_file_import_t *f=r->import;if(!f)return;
    unsigned state=pxa_lvgl_file_picker_state(f->picker);if(!state)return;
    pxa_status_t status=state==2?PXA_STATUS_CANCELLED:f->started?PXA_STATUS_OK:desktop_import_start(r);
    if(!status)status=pxa_file_import_step(&f->job,4);
    if(status==PXA_STATUS_WOULD_BLOCK) {pxa_lvgl_file_picker_progress(f->picker,f->job.index,f->job.count,f->job.total_copied);return;}
    pxa_component_t component=f->component;pxa_request_token_t token=f->token;pxa_fs_import_summary_t result=f->job.result;
    desktop_media_import_cleanup(r);
    (void)pxa_fs_import_complete(r->service,component,token,status,status==PXA_STATUS_OK?&result:NULL);
}
static int desktop_media_import_controller(desktop_media_root_t *r,uint32_t buttons,uint64_t now) {
    if(!r->import) {
        if(r->import_input_release) {if(buttons)return 1;r->import_input_release=0;}
        return 0;
    }
    pxa_lvgl_file_picker_controller(r->import->picker,buttons,now);return 1;
}

static int desktop_media_import_back(desktop_media_root_t *r) {
    if(!r || !r->import)return 0;
    pxa_lvgl_file_picker_controller(r->import->picker,0,0);
    pxa_lvgl_file_picker_controller(r->import->picker,PXA_UI_CONTROLLER_B,1);
    pxa_lvgl_file_picker_controller(r->import->picker,0,2);return 1;
}

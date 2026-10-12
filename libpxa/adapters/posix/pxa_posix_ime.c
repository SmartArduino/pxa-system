#include "pxa/posix/pxa_posix_ime.h"
#include "pxa/wire.h"
#include "pxa/service.h"
#include "pxa/wasi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

struct dictionary {
    int fd; uint64_t size, position;
};
struct pxa_posix_ime_provider {
    pxa_runtime_t *runtime;
    pxa_activation_plan_t plan;
    pxa_activation_entry_t entry;
    pxa_activation_coordinator_t *coordinator;
    pxa_package_manifest_t *manifest;
    void *manifest_workspace;
    union { void *coordinator_workspace; pxa_posix_verification_t *verification; } work;
    const pxa_component_engine_t *engine;
    uint8_t *encoded;
    size_t manifest_bytes, workspace_bytes, coordinator_bytes;
    uint64_t starting;
    char root[];
};
static int32_t dictionary_io(void *context, uint32_t operation, uint8_t *data, size_t size) {
    struct dictionary *d = context;
    if (operation == PXA_IO_READ) {
        size_t remaining = (size_t)(d->size - d->position);
        if (size > remaining) size = remaining;
        if (size > INT32_MAX) size = INT32_MAX;
        ssize_t n;
        do { n = read(d->fd, data, size); } while (n < 0 && errno == EINTR);
        if (n < 0) return PXA_STATUS_IO_ERROR;
        d->position += n; return (int32_t)n;
    }
    if (operation == 3 && size == 8) {
        uint64_t at = pxa_read_u64(data);
        if (at > d->size || at > LONG_MAX) return PXA_STATUS_INVALID_ARGUMENT;
        if (lseek(d->fd, (off_t)at, SEEK_SET) != (off_t)at) return PXA_STATUS_IO_ERROR;
        d->position = at; return 0;
    }
    return PXA_STATUS_UNSUPPORTED;
}
static void dictionary_close(void *context) {
    struct dictionary *d = context;
    close(d->fd); free(d);
}
static const pxa_resource_ops_t dictionary_ops = {sizeof(pxa_resource_ops_t), dictionary_io};

void pxa_posix_ime_provider_close(pxa_posix_ime_provider_t *p) {
    if (!p) return;
    if (p->coordinator) pxa_activation_deactivate_all(p->coordinator, PXA_STOP_SHUTDOWN);
    if (p->coordinator) free(p->work.coordinator_workspace);
    else pxa_posix_installer_verify_close(p->work.verification);
    free(p->manifest_workspace); free(p->encoded); free(p);
}
pxa_status_t pxa_posix_ime_provider_open_begin(pxa_posix_installer_t *installer,
    const char *directory, pxa_runtime_t *runtime, const pxa_component_engine_t *engine,
    const pxa_package_activation_profile_t *services, const pxa_package_host_profile_t *host,
    pxa_posix_ime_provider_t **output) {
    if (!installer || !directory || !runtime || !engine || !services || !host || !output)
        return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    size_t root_bytes = strlen(directory) + 1;
    if (root_bytes > SIZE_MAX - sizeof(pxa_posix_ime_provider_t)) return PXA_STATUS_RESOURCE_LIMIT;
    pxa_posix_ime_provider_t *p = calloc(1, sizeof(*p) + root_bytes);
    if (!p) return PXA_STATUS_RESOURCE_LIMIT;
    p->runtime = runtime; p->engine = engine;
    pxa_status_t status;
    /* This preliminary read only sizes metadata. verify_begin reopens and
     * verifies the exact manifest, signature and every inventory file below. */
    char path[1536];
    int n = snprintf(path, sizeof(path), "%s/manifest.pxm", directory);
    if (n < 0 || (size_t)n >= sizeof(path)) { status = PXA_STATUS_RESOURCE_LIMIT; goto fail; }
    int file = open(path, O_RDONLY);
    if (file < 0) { status = PXA_STATUS_NOT_FOUND; goto fail; }
    struct stat metadata;
    if (fstat(file, &metadata) || !S_ISREG(metadata.st_mode) ||
        metadata.st_size <= 0 || (uint64_t)metadata.st_size > SIZE_MAX) {
        close(file); status = PXA_STATUS_DENIED; goto fail;
    }
    p->manifest_bytes = (size_t)metadata.st_size;
    p->encoded = malloc(p->manifest_bytes);
    if (!p->encoded) { close(file); status = PXA_STATUS_RESOURCE_LIMIT; goto fail; }
    size_t got = 0;
    while (got < p->manifest_bytes) {
        ssize_t count;
        do { count = read(file, p->encoded + got, p->manifest_bytes - got); }
        while (count < 0 && errno == EINTR);
        if (count <= 0) break;
        got += (size_t)count;
    }
    close(file);
    if (got != p->manifest_bytes) { status = PXA_STATUS_IO_ERROR; goto fail; }
    pxa_package_limits_t limits, required; pxa_package_limits_init(&limits);
    status = pxa_package_manifest_measure((pxa_bytes_t){p->encoded, got}, &limits, &required);
    if (status != PXA_STATUS_OK) goto fail;
    p->workspace_bytes = pxa_package_manifest_workspace_size(&required);
    p->manifest_workspace = malloc(p->workspace_bytes);
    if (!p->manifest_workspace) { status = PXA_STATUS_RESOURCE_LIMIT; goto fail; }
    pxa_posix_installer_result_t result = {0}; result.struct_size = sizeof(result);
    result.encoded = p->encoded; result.encoded_capacity = p->manifest_bytes;
    result.manifest_workspace = p->manifest_workspace; result.manifest_workspace_size = p->workspace_bytes;
    result.manifest = &p->manifest; result.root = p->root; result.root_capacity = root_bytes;
    status = pxa_posix_installer_verify_begin(installer, directory, &result, &p->work.verification);
    if (status != PXA_STATUS_OK) goto fail;
    status = pxa_ime_select_provider(p->manifest, (pxa_bytes_t){0}, services, host, &p->entry);
    if (status != PXA_STATUS_OK) goto fail;
    /* The provider may use the IME protocol and sandboxed clocks only. Other
     * app services carry foreground identities and need a future per-package
     * binding before they are safe for concurrent service packages. */
    for (uint16_t i = 0; i < p->entry.component->service_count; ++i) {
        uint16_t id = p->entry.component->services[i].service;
        if (id != PXA_IME_SERVICE_ID && id != PXA_SERVICE_CORE && id != PXA_WASI_SERVICE_ID) {
            status = PXA_STATUS_UNSUPPORTED; goto fail;
        }
    }
    p->plan = (pxa_activation_plan_t){{(const uint8_t *)p->root, strlen(p->root)}, p->manifest, &p->entry, 1};
    *output = p; return PXA_STATUS_OK;
fail:
    pxa_posix_ime_provider_close(p); return status;
}
bool pxa_posix_ime_provider_loading(const pxa_posix_ime_provider_t *p) {
    return p && !p->coordinator && p->work.verification;
}
pxa_status_t pxa_posix_ime_provider_open_step(pxa_posix_ime_provider_t *p, size_t budget) {
    if (!p || !budget) return PXA_STATUS_INVALID_ARGUMENT;
    if (p->coordinator) return PXA_STATUS_OK;
    if (!p->work.verification) return PXA_STATUS_BAD_STATE;
    pxa_status_t status = pxa_posix_installer_verify_step(p->work.verification, budget);
    if (status == PXA_STATUS_WOULD_BLOCK) return status;
    pxa_posix_installer_verify_close(p->work.verification); p->work.verification = NULL;
    if (status != PXA_STATUS_OK) return status;
    p->coordinator_bytes = pxa_activation_coordinator_workspace_size(&p->plan);
    p->work.coordinator_workspace = malloc(p->coordinator_bytes);
    status = p->work.coordinator_workspace ? pxa_activation_coordinator_init(
        p->work.coordinator_workspace, p->coordinator_bytes, p->runtime, &p->plan, p->engine, &p->coordinator)
        : PXA_STATUS_RESOURCE_LIMIT;
    if (status != PXA_STATUS_OK) { free(p->work.coordinator_workspace); p->work.coordinator_workspace=NULL; }
    return status;
}
pxa_status_t pxa_posix_ime_provider_open(pxa_posix_installer_t *installer,
    const char *directory, pxa_runtime_t *runtime, const pxa_component_engine_t *engine,
    const pxa_package_activation_profile_t *services, const pxa_package_host_profile_t *host,
    pxa_posix_ime_provider_t **output) {
    pxa_status_t status=pxa_posix_ime_provider_open_begin(installer,directory,runtime,engine,services,host,output);
    if (status!=PXA_STATUS_OK) return status;
    do { status=pxa_posix_ime_provider_open_step(*output,16384); } while(status==PXA_STATUS_WOULD_BLOCK);
    if (status!=PXA_STATUS_OK) { pxa_posix_ime_provider_close(*output); *output=NULL; }
    return status;
}
pxa_status_t pxa_posix_ime_provider_start(pxa_posix_ime_provider_t *p,
    uint64_t instance, pxa_component_t *component) {
    if (!p || !instance || !component || p->starting) return PXA_STATUS_INVALID_ARGUMENT;
    if (!p->coordinator) return PXA_STATUS_BAD_STATE;
    p->starting = instance;
    pxa_status_t status = pxa_activation_activate(p->coordinator, p->entry.component->id, instance, component);
    p->starting = 0; return status;
}
pxa_status_t pxa_posix_ime_provider_prepare(pxa_posix_ime_provider_t *p,
    uint64_t instance, pxa_component_t component, uint8_t *config, size_t capacity, size_t *size) {
    if (!p || !p->starting || instance != p->starting) return PXA_STATUS_NOT_FOUND;
    if (!config || capacity < 20 || !size) return PXA_STATUS_INVALID_ARGUMENT;
    const char name[] = "assets/dict_pinyin.dat";
    const pxa_package_file_t *entry = NULL;
    for (uint16_t i = 0; i < p->manifest->file_count; ++i) {
        pxa_bytes_t path = p->manifest->files[i].path;
        if (path.size == sizeof(name)-1 && !memcmp(path.data, name, path.size)) {
            entry = &p->manifest->files[i]; break;
        }
    }
    if (!entry) { *size = 0; return PXA_STATUS_OK; }
    if (!entry->size || entry->size > LONG_MAX) return PXA_STATUS_INVALID_ARGUMENT;
    char path[1200]; int n = snprintf(path, sizeof(path), "%s/%s", p->root, name);
    if (n < 0 || (size_t)n >= sizeof(path)) return PXA_STATUS_RESOURCE_LIMIT;
    struct dictionary *d = calloc(1, sizeof(*d));
    if (!d) return PXA_STATUS_RESOURCE_LIMIT;
    /* Read directly into the Guest's final model buffers. Newlib's unbuffered
     * fread can split a bulk request into one-byte VFS reads; a buffered FILE
     * adds an unnecessary cache. The capability itself owns this descriptor. */
    d->fd = open(path, O_RDONLY); d->size = entry->size;
    if (d->fd < 0) { free(d); return PXA_STATUS_IO_ERROR; }
    pxa_resource_t resource = {d, &dictionary_ops, dictionary_close}; pxa_handle64_t handle;
    pxa_status_t status = pxa_handle_open64(p->runtime, component, PXA_RESOURCE_FILE, 0, &resource, &handle);
    if (status != PXA_STATUS_OK) { dictionary_close(d); return status; }
    pxa_write_u16(config, PXA_IME_CONFIG_DICTIONARY); pxa_write_u16(config+2, 16);
    pxa_write_u64(config+4, handle); pxa_write_u64(config+12, d->size); *size = 20;
    return PXA_STATUS_OK;
}
size_t pxa_posix_ime_provider_metadata_bytes(const pxa_posix_ime_provider_t *p) {
    return p ? sizeof(*p) + strlen(p->root) + 1 + p->manifest_bytes + p->workspace_bytes + p->coordinator_bytes : 0;
}

#include <dirent.h>
static int installed_stat(const char *path, struct stat *metadata) {
#ifdef ESP_PLATFORM
    /* LittleFS VFS has no symlinks and does not implement lstat. */
    return stat(path, metadata);
#else
    return lstat(path, metadata);
#endif
}
static int identity_valid(const char *id) {
    if (!id || !*id || strlen(id)>130 || !strcmp(id,".") || !strcmp(id,"..")) return 0;
    for (; *id; ++id) if (!((*id>='a' && *id<='z') || (*id>='A' && *id<='Z') ||
        (*id>='0' && *id<='9') || *id=='-' || *id=='_' || *id=='.')) return 0;
    return 1;
}
pxa_status_t pxa_posix_ime_registry_resolve(const char *packages, const char *id,
    char *directory, size_t capacity) {
    if (!packages || !identity_valid(id) || !directory || !capacity) return PXA_STATUS_INVALID_ARGUMENT;
    char path[1536]; struct stat metadata;
    int n=snprintf(path,sizeof(path),"%s/%s",packages,id);
    if (n<0 || (size_t)n>=sizeof(path) || installed_stat(path,&metadata) || !S_ISDIR(metadata.st_mode))
        return PXA_STATUS_NOT_FOUND;
    char disabled[1600]; n=snprintf(disabled,sizeof(disabled),"%s/.disabled",path);
    if (n<0 || (size_t)n>=sizeof(disabled) || !installed_stat(disabled,&metadata)) return PXA_STATUS_DENIED;
    n=snprintf(directory,capacity,"%s/current",path);
    if (n<0 || (size_t)n>=capacity) return PXA_STATUS_RESOURCE_LIMIT;
    if (!installed_stat(directory,&metadata) && S_ISDIR(metadata.st_mode)) return PXA_STATUS_OK;
    n=snprintf(directory,capacity,"%s",path);
    return n>=0 && (size_t)n<capacity ? PXA_STATUS_OK : PXA_STATUS_RESOURCE_LIMIT;
}
pxa_status_t pxa_posix_ime_registry_visit(const char *packages,
    pxa_posix_ime_visit_fn visit, void *context) {
    if (!packages || !visit) return PXA_STATUS_INVALID_ARGUMENT;
    DIR *directory=opendir(packages);
    if (!directory) return errno==ENOENT ? PXA_STATUS_OK : PXA_STATUS_IO_ERROR;
    struct dirent *entry;
    while ((entry=readdir(directory))) {
        char path[1536], manifest_path[1600];
        if (pxa_posix_ime_registry_resolve(packages,entry->d_name,path,sizeof(path)) != PXA_STATUS_OK) continue;
        int n=snprintf(manifest_path,sizeof(manifest_path),"%s/manifest.pxm",path);
        if (n<0 || (size_t)n>=sizeof(manifest_path)) continue;
        FILE *file=fopen(manifest_path,"rb"); if(!file) continue;
        if (fseek(file,0,SEEK_END)) { fclose(file); continue; }
        long bytes=ftell(file);
        pxa_package_limits_t limits,required; pxa_package_limits_init(&limits);
        if (bytes<=0 || (unsigned long)bytes>128u*1024u || fseek(file,0,SEEK_SET)) { fclose(file); continue; }
        uint8_t *encoded=malloc((size_t)bytes);
        size_t got=encoded ? fread(encoded,1,(size_t)bytes,file) : 0; fclose(file);
        if (got!=(size_t)bytes || pxa_package_manifest_measure((pxa_bytes_t){encoded,got},&limits,&required)!=PXA_STATUS_OK) {
            free(encoded); continue;
        }
        size_t workspace_bytes=pxa_package_manifest_workspace_size(&required);
        void *workspace=malloc(workspace_bytes); pxa_package_manifest_t *manifest;
        if (workspace && pxa_package_manifest_parse(workspace,workspace_bytes,(pxa_bytes_t){encoded,got},&required,&manifest)==PXA_STATUS_OK)
            for (uint16_t i=0;i<manifest->component_count;++i)
                if (pxa_ime_is_provider(&manifest->components[i])) { visit(context,entry->d_name,manifest->name); break; }
        free(workspace); free(encoded);
    }
    closedir(directory); return PXA_STATUS_OK;
}
/* Shared small-record I/O: bounded, interruptible reads and atomic writes.
 * Selection and preferences follow the same failure/symlink policy. */
static pxa_status_t read_record(const char *path, uint8_t *bytes, size_t capacity, size_t *size) {
    *size=0;
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if (fd<0) return errno==ENOENT?PXA_STATUS_NOT_FOUND:PXA_STATUS_IO_ERROR;
    pxa_status_t status=PXA_STATUS_OK;
    while (*size<capacity) {
        ssize_t count=read(fd,bytes+*size,capacity-*size);
        if (count<0 && errno==EINTR) continue;
        if (count<0) { status=PXA_STATUS_IO_ERROR; break; }
        if (!count) break;
        *size+=(size_t)count;
    }
    if (close(fd)) status=PXA_STATUS_IO_ERROR;
    return status;
}
static pxa_status_t write_record(const char *path, const uint8_t *bytes, size_t size) {
    size_t length=strlen(path);
    if (length>SIZE_MAX-5) return PXA_STATUS_RESOURCE_LIMIT;
    char *temporary=malloc(length+5);
    if (!temporary) return PXA_STATUS_RESOURCE_LIMIT;
    memcpy(temporary,path,length); memcpy(temporary+length,".tmp",5);
    int fd=open(temporary,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
    pxa_status_t status=PXA_STATUS_IO_ERROR;
    if (fd>=0) {
        size_t written=0;
        while (written<size) {
            ssize_t count=write(fd,bytes+written,size-written);
            if (count<0 && errno==EINTR) continue;
            if (count<=0) break;
            written+=(size_t)count;
        }
        bool ok=written==size && fsync(fd)==0;
        if (close(fd)) ok=false;
        if (ok && !rename(temporary,path)) status=PXA_STATUS_OK;
    }
    if (status!=PXA_STATUS_OK) unlink(temporary);
    free(temporary); return status;
}
pxa_status_t pxa_posix_ime_selection_read(const char *path, char *identity, size_t capacity) {
    if (!path || !identity || capacity<131) return PXA_STATUS_INVALID_ARGUMENT;
    size_t bytes=0;
    pxa_status_t status=read_record(path,(uint8_t *)identity,131,&bytes);
    if (status==PXA_STATUS_NOT_FOUND) status=PXA_STATUS_OK;
    if (bytes>130) status=PXA_STATUS_IO_ERROR;
    if (status!=PXA_STATUS_OK) { identity[0]=0; return status; }
    identity[bytes]=0;
    if (bytes && (!identity_valid(identity) || strlen(identity)!=bytes)) { identity[0]=0; return PXA_STATUS_INVALID_ARGUMENT; }
    return PXA_STATUS_OK;
}
pxa_status_t pxa_posix_ime_selection_write(const char *path, const char *identity) {
    if (!path || !identity || (*identity && !identity_valid(identity))) return PXA_STATUS_INVALID_ARGUMENT;
    return write_record(path,(const uint8_t *)identity,strlen(identity));
}

/* Missing settings mean defaults; malformed settings never become indices. */
pxa_status_t pxa_posix_ime_preferences_read(const char *path,pxa_ime_preferences_t *output) {
    if (!path || !output) return PXA_STATUS_INVALID_ARGUMENT;
    *output=(pxa_ime_preferences_t){0};
    uint8_t bytes[4]={0}; size_t count=0;
    pxa_status_t status=read_record(path,bytes,sizeof(bytes),&count);
    if (status==PXA_STATUS_NOT_FOUND) return PXA_STATUS_OK;
    if (status!=PXA_STATUS_OK) return status;
    pxa_ime_preferences_t value={bytes[1],bytes[2]};
    if (count!=3 || bytes[0]!=1 || !pxa_ime_preferences_valid(value)) return PXA_STATUS_INVALID_ARGUMENT;
    *output=value; return PXA_STATUS_OK;
}
pxa_status_t pxa_posix_ime_preferences_write(const char *path,pxa_ime_preferences_t value) {
    if (!path || !pxa_ime_preferences_valid(value)) return PXA_STATUS_INVALID_ARGUMENT;
    const uint8_t bytes[]={1,value.layout,value.language};
    return write_record(path,bytes,sizeof(bytes));
}

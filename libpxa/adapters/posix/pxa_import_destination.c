/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pxa/posix/pxa_import_destination.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__) && !defined(ESP_PLATFORM)
#include <sys/syscall.h>
#endif
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "pxa/esp/pxa_esp_posix_shim.h"
#define fsync pxa_esp_fsync
#endif
static pxa_status_t error(void) {
    if(errno==ENOENT)return PXA_STATUS_NOT_FOUND;
    if(errno==EEXIST || errno==ENOTEMPTY)return PXA_STATUS_BUSY;
    if(errno==ENOSPC || errno==EDQUOT)return PXA_STATUS_QUOTA_EXCEEDED;
    if(errno==EACCES || errno==EPERM || errno==ELOOP)return PXA_STATUS_DENIED;
    if(errno==ENOMEM || errno==EMFILE)return PXA_STATUS_RESOURCE_LIMIT;
    if(errno==ENAMETOOLONG)return PXA_STATUS_LIMIT_EXCEEDED;
    return PXA_STATUS_IO_ERROR;
}
static int name(pxa_bytes_t path,char out[PXA_FS_SCOPED_MAX_PATH_BYTES+1]) {
    if(!path.size || path.size>PXA_FS_SCOPED_MAX_PATH_BYTES ||
        memchr(path.data,'/',path.size) || !pxa_fs_scoped_path_is_valid(path))return 0;
    memcpy(out,path.data,path.size);out[path.size]=0;return 1;
}
#ifdef ESP_PLATFORM
/* ESP app-media mounts are Host-managed link-free filesystems. Use the
 * native VFS here to retain long UTF-8 FAT names; directory descriptor shims
 * target private storage and have a shorter path budget. */
static char *native_path(pxa_import_destination_t *d,const char *leaf) {
    size_t n=strlen(d->root_path)+strlen(leaf)+2;char *path=malloc(n);
    if(path)snprintf(path,n,"%s/%s",d->root_path,leaf);else errno=ENOMEM;
    return path;
}
#endif
static pxa_status_t create(void *context,pxa_bytes_t path,uint32_t flags,void **out,uint8_t *kind) {
    pxa_import_destination_t *d=context;char leaf[PXA_FS_SCOPED_MAX_PATH_BYTES+1];struct stat info;
    if(!out || !kind || flags!=(PXA_FS_OPEN_WRITE|PXA_FS_OPEN_CREATE|PXA_FS_OPEN_EXCLUSIVE) || !name(path,leaf))return PXA_STATUS_INVALID_ARGUMENT;
    *out=NULL;if(d->open)return PXA_STATUS_BUSY;
#ifdef ESP_PLATFORM
    char *native=native_path(d,leaf);if(!native)return PXA_STATUS_RESOURCE_LIMIT;
    int fd=open(native,O_WRONLY|O_CREAT|O_EXCL,0600);free(native);
#else
    int fd=openat(d->root_fd,leaf,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
#endif
    if(fd<0)return error();
    if(fstat(fd,&info) || !S_ISREG(info.st_mode) || info.st_nlink>1) {close(fd);return PXA_STATUS_DENIED;}
    d->file_fd=fd;d->open=1;*out=d;*kind=PXA_FS_KIND_REGULAR;return PXA_STATUS_OK;
}
static pxa_status_t write_file(void *context,void *resource,const uint8_t *data,size_t size,size_t *written) {
    pxa_import_destination_t *d=context;
    if(resource!=d || !d->open || !written || size>4096)return PXA_STATUS_INVALID_ARGUMENT;
    ssize_t n;do {n=write(d->file_fd,data,size);}while(n<0 && errno==EINTR);
    *written=n<0?0:(size_t)n;return n<0?error():PXA_STATUS_OK;
}
static void close_file(void *context,void *resource,uint8_t kind) {
    pxa_import_destination_t *d=context;(void)kind;
    if(resource==d && d->open) {close(d->file_fd);d->open=0;d->file_fd=-1;}
}
static pxa_status_t stat_file(void *context,pxa_bytes_t path,pxa_fs_entry_t *entry) {
    pxa_import_destination_t *d=context;char leaf[PXA_FS_SCOPED_MAX_PATH_BYTES+1];struct stat info;
    if(!name(path,leaf) || !entry)return PXA_STATUS_INVALID_ARGUMENT;
#ifdef ESP_PLATFORM
    char *native=native_path(d,leaf);if(!native)return PXA_STATUS_RESOURCE_LIMIT;
    int result=stat(native,&info);free(native);if(result)return error();
#else
    if(fstatat(d->root_fd,leaf,&info,AT_SYMLINK_NOFOLLOW))return error();
#endif
    /* Every occupied name, including a link/directory, forces another suffix. */
    entry->name=path;entry->kind=S_ISREG(info.st_mode)?PXA_FS_KIND_REGULAR:PXA_FS_KIND_DIRECTORY;
    entry->size=(uint64_t)info.st_size;return PXA_STATUS_OK;
}
static pxa_status_t remove_file(void *context,pxa_bytes_t path) {
    pxa_import_destination_t *d=context;char leaf[PXA_FS_SCOPED_MAX_PATH_BYTES+1];
    if(!name(path,leaf))return PXA_STATUS_INVALID_ARGUMENT;
#ifdef ESP_PLATFORM
    char *native=native_path(d,leaf);if(!native)return PXA_STATUS_RESOURCE_LIMIT;
    int result=unlink(native);free(native);return result?error():PXA_STATUS_OK;
#else
    return unlinkat(d->root_fd,leaf,0)?error():PXA_STATUS_OK;
#endif
}
static pxa_status_t publish(void *context,pxa_bytes_t from,pxa_bytes_t to) {
    pxa_import_destination_t *d=context;char source[PXA_FS_SCOPED_MAX_PATH_BYTES+1],destination[PXA_FS_SCOPED_MAX_PATH_BYTES+1];
    if(!name(from,source) || !name(to,destination))return PXA_STATUS_INVALID_ARGUMENT;
#if defined(__linux__) && !defined(ESP_PLATFORM)
    return syscall(SYS_renameat2,d->root_fd,source,d->root_fd,destination,1u)==0?PXA_STATUS_OK:error();
#elif defined(ESP_PLATFORM) && !defined(CONFIG_FATFS_VFS_RENAME_REPLACES_DESTINATION)
    /* The firmware profile uses FAT's atomic no-replace rename. */
    struct stat info;char *a=native_path(d,source),*b=native_path(d,destination);
    if(!a || !b) {free(a);free(b);return PXA_STATUS_RESOURCE_LIMIT;}
    pxa_status_t result=PXA_STATUS_OK;
    if(!stat(b,&info))result=PXA_STATUS_BUSY;
    else if(errno!=ENOENT)result=error();
    else if(rename(a,b))result=error();
    free(a);free(b);return result;
#else
    (void)d;return PXA_STATUS_UNSUPPORTED;
#endif
}
pxa_status_t pxa_import_destination_open(pxa_import_destination_t *d,const char *path,pxa_fs_backend_t *backend) {
    if(!d || !path || !backend)return PXA_STATUS_INVALID_ARGUMENT;
    memset(d,0,sizeof(*d));d->root_fd=d->file_fd=-1;
    d->root_path=path;
#ifdef ESP_PLATFORM
    struct stat info;if(stat(path,&info))return error();
    if(!S_ISDIR(info.st_mode))return PXA_STATUS_DENIED;
#else
    d->root_fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(d->root_fd<0)return error();
#endif
    *backend=(pxa_fs_backend_t){.struct_size=sizeof(*backend),.context=d,.open=create,
        .write=write_file,.close=close_file,.stat=stat_file,.remove=remove_file,.rename=publish};
    return PXA_STATUS_OK;
}
pxa_status_t pxa_import_destination_flush(pxa_import_destination_t *d,void *resource) {
    if(!d || resource!=d || !d->open)return PXA_STATUS_INVALID_ARGUMENT;
    return fsync(d->file_fd)==0?PXA_STATUS_OK:error();
}
void pxa_import_destination_close(pxa_import_destination_t *d) {
    if(!d)return;
    close_file(d,d,1);if(d->root_fd>=0)close(d->root_fd);d->root_fd=-1;
}

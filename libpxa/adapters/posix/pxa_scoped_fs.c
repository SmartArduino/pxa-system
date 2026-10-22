#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pxa/posix/pxa_scoped_fs.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SCOPED_MAGIC UINT32_C(0x50585346)
#define SCOPED_ALIGN 16u
typedef struct {
    uint64_t epoch, grant_revision;
    uint16_t grant_index;
    int fd;
    DIR *directory;
    uint8_t occupied, kind;
    char relative[PXA_SCOPED_FS_PATH_BYTES];
} scoped_entry_t;
struct pxa_scoped_fs {
    uint32_t magic;
    pxa_scoped_fs_config_t config;
    /* Runtime-owner scratch avoids long path arrays on the ESP task stack. */
    char native_path[2*PXA_SCOPED_FS_PATH_BYTES+2];
    scoped_entry_t entries[];
};
static pxa_status_t os_error(void) {
    switch (errno) {
    case ENOENT: return PXA_STATUS_NOT_FOUND;
    case EACCES: case EPERM: case ELOOP: case EROFS: return PXA_STATUS_DENIED;
    case EMFILE: case ENFILE: case ENOMEM: return PXA_STATUS_RESOURCE_LIMIT;
    case ENAMETOOLONG: return PXA_STATUS_LIMIT_EXCEEDED;
    default: return PXA_STATUS_IO_ERROR;
    }
}
static int path_valid(const char *p) {
    size_t n = strnlen(p,PXA_SCOPED_FS_PATH_BYTES);
    return n < PXA_SCOPED_FS_PATH_BYTES &&
        (!n || pxa_fs_scoped_path_is_valid((pxa_bytes_t){(const uint8_t *)p,n}));
}
static const pxa_scoped_fs_mount_t *mount_for(pxa_scoped_fs_t *fs,uint64_t epoch) {
    uint16_t i;
    for (i=0;i<fs->config.mount_count;++i)
        if (fs->config.mounts[i].mount_epoch==epoch) return &fs->config.mounts[i];
    return NULL;
}
static pxa_status_t ready(pxa_scoped_fs_t *fs,uint64_t epoch,
    pxa_volume_info_t *info) {
    pxa_status_t s = pxa_volume_get(fs->config.volumes,epoch,info);
    if (s!=PXA_STATUS_OK || info->state!=PXA_VOLUME_READY || info->ambiguous)
        return PXA_STATUS_UNAVAILABLE;
    return info->capabilities & PXA_VOLUME_READ ? PXA_STATUS_OK : PXA_STATUS_DENIED;
}
static pxa_status_t check_entry(pxa_scoped_fs_t *fs,scoped_entry_t *e) {
    pxa_volume_info_t info;
    const pxa_scoped_fs_grant_t *g;
    if (!e || e<fs->entries || e>=fs->entries+fs->config.max_open_resources ||
        !e->occupied) return PXA_STATUS_NOT_FOUND;
    g=&fs->config.grants[e->grant_index];
    if (!g->active || g->revision!=e->grant_revision) return PXA_STATUS_DENIED;
    return ready(fs,e->epoch,&info);
}
static scoped_entry_t *allocate_entry(pxa_scoped_fs_t *fs) {
    uint16_t i;
    for (i=0;i<fs->config.max_open_resources;++i) if (!fs->entries[i].occupied) {
        scoped_entry_t *e=&fs->entries[i];
        memset(e,0,sizeof(*e)); e->fd=-1; e->occupied=1; return e;
    }
    return NULL;
}
static void close_entry(void *context,void *resource,uint8_t kind) {
    pxa_scoped_fs_t *fs=context; scoped_entry_t *e=resource;
    (void)kind;
    if (!e || e<fs->entries || e>=fs->entries+fs->config.max_open_resources ||
        !e->occupied) return;
    if (e->directory) closedir(e->directory);
    else if (e->fd>=0) close(e->fd);
    memset(e,0,sizeof(*e));
}
#if !defined(ESP_PLATFORM)
static pxa_status_t open_below_fd(int parent,const char *path,int directory,
    int *fd,DIR **stream,struct stat *info) {
    char copy[PXA_SCOPED_FS_PATH_BYTES]; char *part,*next;
    struct stat root; int current=dup(parent);
    *fd=-1; *stream=NULL;
    if (current<0) return os_error();
    if (fstat(current,&root)!=0) { pxa_status_t s=os_error(); close(current); return s; }
    strcpy(copy,path); part=copy;
    while (*part) {
        int child,flags=O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK;
        next=strchr(part,'/'); if (next) *next=0;
        if (next || directory) flags|=O_DIRECTORY;
        child=openat(current,part,flags); close(current);
        if (child<0) return os_error();
        current=child;
        if (fstat(current,info)!=0) { pxa_status_t s=os_error(); close(current); return s; }
        if (info->st_dev!=root.st_dev) { close(current); return PXA_STATUS_DENIED; }
        if (!next) break;
        part=next+1;
    }
    if (fstat(current,info)!=0) { pxa_status_t s=os_error(); close(current); return s; }
    if ((directory && !S_ISDIR(info->st_mode)) ||
        (!directory && (!S_ISREG(info->st_mode) || info->st_nlink!=1))) {
        close(current); return PXA_STATUS_DENIED;
    }
    if (directory) {
        *stream=fdopendir(current);
        if (!*stream) { pxa_status_t s=os_error(); close(current); return s; }
    } else *fd=current;
    return PXA_STATUS_OK;
}
#endif
/* A tilde is legal in an LFN. Reject generated 8.3 aliases by requiring
 * every tilde-bearing segment to exist in the parent's actual enumeration.
 * This cold check never runs for normal paths or reads through an open file. */
static int same_fat_name(const char *actual,const char *requested,size_t size) {
    if(strlen(actual)!=size)return 0;
    for(size_t i=0;i<size;++i) {
        unsigned char a=(unsigned char)actual[i],b=(unsigned char)requested[i];
        if(a>='A' && a<='Z')a+='a'-'A';
        if(b>='A' && b<='Z')b+='a'-'A';
        if(a!=b)return 0;
    }
    return 1;
}
static pxa_status_t literal_fat_path(pxa_scoped_fs_t *fs,
    const pxa_scoped_fs_mount_t *mount,const char *path) {
    char *full=fs->native_path;size_t used=strlen(mount->host_path);
    if(used>=sizeof(fs->native_path))return PXA_STATUS_LIMIT_EXCEEDED;
    memcpy(full,mount->host_path,used+1);
    for(const char *part=path;*part;) {
        size_t size=strcspn(part,"/");
        if(memchr(part,'~',size)) {
            DIR *parent=opendir(full);struct dirent *entry;int found=0,error=0;
            if(!parent)return os_error();
            for(;;) {
                errno=0;entry=readdir(parent);
                if(!entry){error=errno;break;}
                if(same_fat_name(entry->d_name,part,size)){found=1;break;}
            }
            closedir(parent);
            if(error){errno=error;return os_error();}
            if(!found)return PXA_STATUS_DENIED;
        }
        if(size>=sizeof(fs->native_path)-used-1)return PXA_STATUS_LIMIT_EXCEEDED;
        full[used++]='/';memcpy(full+used,part,size);used+=size;full[used]=0;
        part+=size;if(*part)++part;
    }
    return PXA_STATUS_OK;
}
/* Open without a directory-fd emulation on FAT. Its mount is registered by
 * trusted Host code as a link-free filesystem with no nested mounts. */
static pxa_status_t open_native(pxa_scoped_fs_t *fs,const pxa_scoped_fs_mount_t *m,const char *path,
    int directory,int *fd,DIR **stream,struct stat *info) {
    char *full=fs->native_path;
    int n;
    *fd=-1; *stream=NULL;
    if (!path_valid(path)) return PXA_STATUS_INVALID_ARGUMENT;
    if (m->native_fat) {
        if (strchr(path,'~')) {
            pxa_status_t status=literal_fat_path(fs,m,path);
            if(status!=PXA_STATUS_OK)return status;
        }
        n=snprintf(full,sizeof(fs->native_path),"%s%s%s",m->host_path,*path?"/":"",path);
        if (n<0 || (size_t)n>=sizeof(fs->native_path)) return PXA_STATUS_LIMIT_EXCEEDED;
        if (stat(full,info)!=0) return os_error();
        if (directory) {
            if (!S_ISDIR(info->st_mode)) return PXA_STATUS_INVALID_ARGUMENT;
            *stream=opendir(full);
            return *stream ? PXA_STATUS_OK : os_error();
        }
        if (!S_ISREG(info->st_mode)) return PXA_STATUS_DENIED;
        *fd=open(full,O_RDONLY);
        return *fd>=0 ? PXA_STATUS_OK : os_error();
    }
#if !defined(ESP_PLATFORM)
    {
        int current=open(m->host_path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        pxa_status_t s;
        if (current<0) return os_error();
        s=open_below_fd(current,path,directory,fd,stream,info);
        close(current); return s;
    }
#else
    return PXA_STATUS_UNSUPPORTED;
#endif
}
static pxa_status_t grant_location(pxa_scoped_fs_t *fs,
    const pxa_scoped_fs_grant_t *g,pxa_volume_info_t *info,
    const pxa_scoped_fs_mount_t **mount) {
    pxa_status_t s;
    if (!g->active || !g->revision || !path_valid(g->relative_path) ||
        !g->rights || (g->rights & ~3u)) return PXA_STATUS_DENIED;
    s=pxa_volume_find(fs->config.volumes,&g->volume_id,info);
    if (s!=PXA_STATUS_OK) return s==PXA_STATUS_BUSY ? PXA_STATUS_UNAVAILABLE : s;
    s=ready(fs,info->mount_epoch,info);
    if (s!=PXA_STATUS_OK) return s;
    *mount=mount_for(fs,info->mount_epoch);
    return *mount ? PXA_STATUS_OK : PXA_STATUS_UNAVAILABLE;
}
pxa_status_t pxa_scoped_fs_validate_grant(pxa_scoped_fs_t *fs,
    const pxa_scoped_fs_grant_t *grant) {
    pxa_volume_info_t info; const pxa_scoped_fs_mount_t *m;
    struct stat stat_info; DIR *dir; int fd;
    pxa_status_t s;
    if (!fs || fs->magic!=SCOPED_MAGIC || !grant) return PXA_STATUS_INVALID_ARGUMENT;
    s=grant_location(fs,grant,&info,&m);
    if (s!=PXA_STATUS_OK) return s;
    s=open_native(fs,m,grant->relative_path,1,&fd,&dir,&stat_info);
    if (s==PXA_STATUS_OK) closedir(dir);
    return s;
}
static pxa_status_t open_root(void *context,uint8_t kind,const uint8_t id[16],
    void **resource,uint32_t *rights) {
    pxa_scoped_fs_t *fs=context; uint16_t i;
    *resource=NULL; *rights=0;
    if (kind!=PXA_FS_ROOT_GRANT) return PXA_STATUS_UNSUPPORTED;
    for (i=0;i<fs->config.grant_count;++i) {
        const pxa_scoped_fs_grant_t *g=&fs->config.grants[i];
        pxa_volume_info_t info; const pxa_scoped_fs_mount_t *m;
        scoped_entry_t *e; struct stat st; pxa_status_t s;
        if (memcmp(g->id,id,16)!=0) continue;
        s=grant_location(fs,g,&info,&m); if (s!=PXA_STATUS_OK) return s;
        e=allocate_entry(fs); if (!e) return PXA_STATUS_RESOURCE_LIMIT;
        s=open_native(fs,m,g->relative_path,1,&e->fd,&e->directory,&st);
        if (s!=PXA_STATUS_OK) { close_entry(fs,e,0); return s; }
        e->kind=PXA_FS_KIND_DIRECTORY; e->epoch=info.mount_epoch;
        e->grant_index=i; e->grant_revision=g->revision;
        strcpy(e->relative,g->relative_path);
        *rights=g->rights; *resource=e; return PXA_STATUS_OK;
    }
    return PXA_STATUS_DENIED;
}

static pxa_status_t open_at(void *context,void *directory,pxa_bytes_t path,
    uint32_t flags,void **resource,pxa_fs_entry_t *info,uint64_t *features) {
    pxa_scoped_fs_t *fs=context; scoped_entry_t *parent=directory,*e;
    const pxa_scoped_fs_mount_t *m; struct stat st; int n;
    pxa_status_t s=check_entry(fs,parent);
    *resource=NULL; *features=0;
    if (s!=PXA_STATUS_OK) return s;
    if (parent->kind!=PXA_FS_KIND_DIRECTORY ||
        !pxa_fs_scoped_path_is_valid(path)) return PXA_STATUS_INVALID_ARGUMENT;
    if (flags!=PXA_FS_OPEN_READ && flags!=(PXA_FS_OPEN_READ|PXA_FS_OPEN_DIRECTORY))
        return PXA_STATUS_DENIED;
    if (!(fs->config.grants[parent->grant_index].rights & PXA_FS_RIGHT_READ))
        return PXA_STATUS_DENIED;
    m=mount_for(fs,parent->epoch); if (!m) return PXA_STATUS_UNAVAILABLE;
    e=allocate_entry(fs); if (!e) return PXA_STATUS_RESOURCE_LIMIT;
    n=snprintf(e->relative,sizeof(e->relative),"%s%s%.*s",parent->relative,
        *parent->relative?"/":"",(int)path.size,(const char *)path.data);
    if (n<0 || (size_t)n>=sizeof(e->relative)) {
        close_entry(fs,e,0); return PXA_STATUS_LIMIT_EXCEEDED;
    }
    e->kind=flags & PXA_FS_OPEN_DIRECTORY ? PXA_FS_KIND_DIRECTORY : PXA_FS_KIND_REGULAR;
#if !defined(ESP_PLATFORM)
    if (!m->native_fat) {
        char relative[PXA_SCOPED_FS_PATH_BYTES];
        if (path.size>=sizeof(relative)) { close_entry(fs,e,0); return PXA_STATUS_LIMIT_EXCEEDED; }
        memcpy(relative,path.data,path.size); relative[path.size]=0;
        s=open_below_fd(dirfd(parent->directory),relative,
            e->kind==PXA_FS_KIND_DIRECTORY,&e->fd,&e->directory,&st);
    } else
#endif
    s=open_native(fs,m,e->relative,e->kind==PXA_FS_KIND_DIRECTORY,
        &e->fd,&e->directory,&st);
    if (s!=PXA_STATUS_OK) { close_entry(fs,e,0); return s; }
    e->epoch=parent->epoch; e->grant_index=parent->grant_index;
    e->grant_revision=parent->grant_revision;
    memset(info,0,sizeof(*info)); info->kind=e->kind;
    info->size=e->kind==PXA_FS_KIND_REGULAR ? (uint64_t)st.st_size : 0;
    if(e->kind==PXA_FS_KIND_REGULAR) *features=PXA_FS_FILE_FEATURE_READ_AT;
    *resource=e; return PXA_STATUS_OK;
}
static pxa_status_t read_file(void *context,void *resource,uint8_t *out,
    size_t capacity,size_t *size) {
    pxa_scoped_fs_t *fs=context; scoped_entry_t *e=resource; ssize_t n;
    pxa_status_t s=check_entry(fs,e); *size=0;
    if (s!=PXA_STATUS_OK) return s;
    if (e->kind!=PXA_FS_KIND_REGULAR) return PXA_STATUS_INVALID_ARGUMENT;
    if (!capacity) return PXA_STATUS_INVALID_ARGUMENT;
    do { n=read(e->fd,out,capacity>4096 ? 4096 : capacity); } while (n<0 && errno==EINTR);
    if (n<0) return os_error();
    *size=(size_t)n; return PXA_STATUS_OK;
}
static pxa_status_t read_at_file(void *context,void *resource,uint64_t offset,
    uint8_t *out,size_t capacity,size_t *size) {
    pxa_scoped_fs_t *fs=context;scoped_entry_t *e=resource;struct stat st;ssize_t n;
    pxa_status_t s=check_entry(fs,e);*size=0;
    if(s!=PXA_STATUS_OK)return s;
    if(e->kind!=PXA_FS_KIND_REGULAR || !out || !capacity ||
        capacity>PXA_FS_READ_AT_MAX_BYTES) return PXA_STATUS_INVALID_ARGUMENT;
    if(fstat(e->fd,&st)) return os_error();
    if(st.st_size<0)return PXA_STATUS_IO_ERROR;
    if(offset>=(uint64_t)st.st_size)return PXA_STATUS_OK;
    if((uint64_t)(off_t)offset!=offset)return PXA_STATUS_LIMIT_EXCEEDED;
    do {n=pread(e->fd,out,capacity,(off_t)offset);}while(n<0 && errno==EINTR);
    if(n<0)return os_error();
    *size=(size_t)n;return PXA_STATUS_OK;
}
static pxa_status_t seek_file(void *context,void *resource,int64_t offset,
    uint8_t origin,uint64_t *position) {
    pxa_scoped_fs_t *fs=context; scoped_entry_t *e=resource; off_t n;
    pxa_status_t s=check_entry(fs,e);
    if (s!=PXA_STATUS_OK) return s;
    if (e->kind!=PXA_FS_KIND_REGULAR || origin>2) return PXA_STATUS_INVALID_ARGUMENT;
    if ((int64_t)(off_t)offset!=offset) return PXA_STATUS_LIMIT_EXCEEDED;
    n=lseek(e->fd,(off_t)offset,origin==0?SEEK_SET:origin==1?SEEK_CUR:SEEK_END);
    if (n<0) return os_error();
    *position=(uint64_t)n; return PXA_STATUS_OK;
}
static pxa_status_t stat_at(void *context,void *directory,pxa_bytes_t path,
    pxa_fs_entry_t *info) {
    void *resource=NULL; uint64_t features=0;
    pxa_status_t s=open_at(context,directory,path,PXA_FS_OPEN_READ,&resource,info,&features);
    if (s==PXA_STATUS_DENIED || s==PXA_STATUS_INVALID_ARGUMENT)
        s=open_at(context,directory,path,PXA_FS_OPEN_READ|PXA_FS_OPEN_DIRECTORY,
            &resource,info,&features);
    if (s==PXA_STATUS_OK) close_entry(context,resource,info->kind);
    return s;
}
static pxa_status_t query_root(void *context,void *directory,pxa_fs_root_info_t *out) {
    pxa_scoped_fs_t *fs=context; scoped_entry_t *e=directory;
    const pxa_scoped_fs_grant_t *g; pxa_volume_info_t v;
    pxa_status_t s=check_entry(fs,e);
    if (s!=PXA_STATUS_OK && s!=PXA_STATUS_UNAVAILABLE) return s;
    if (e->kind!=PXA_FS_KIND_DIRECTORY) return PXA_STATUS_INVALID_ARGUMENT;
    g=&fs->config.grants[e->grant_index]; memset(out,0,sizeof(*out));
    out->volume_id=g->volume_id; out->mount_epoch=e->epoch; out->rights=g->rights;
    out->max_path_bytes=PXA_SCOPED_FS_PATH_BYTES-1;
    out->max_segment_bytes=PXA_SCOPED_FS_PATH_BYTES-1;
    out->capabilities=PXA_FS_FILE_FEATURE_READ_AT;
    out->max_read_at_bytes=PXA_FS_READ_AT_MAX_BYTES;
    if (pxa_volume_get(fs->config.volumes,e->epoch,&v)==PXA_STATUS_OK) {
        out->naming=v.capabilities & PXA_VOLUME_CASE_INSENSITIVE ?
            PXA_FS_NAMING_CASE_INSENSITIVE : PXA_FS_NAMING_BYTE_EXACT;
        out->state=v.ambiguous ? PXA_VOLUME_FAILED : v.state;
        out->space_known=v.space_known; out->total_bytes=v.total_bytes;
        out->free_bytes=v.free_bytes;
    }
    return PXA_STATUS_OK;
}
static pxa_status_t read_directory(void *context,void *resource,
    pxa_fs_entry_t *info,uint8_t *end) {
    pxa_scoped_fs_t *fs=context; scoped_entry_t *e=resource;
    pxa_status_t s=check_entry(fs,e); struct dirent *item;
    const pxa_scoped_fs_mount_t *m; unsigned scanned=0;
    *end=0;
    if (s!=PXA_STATUS_OK) return s;
    if (e->kind!=PXA_FS_KIND_DIRECTORY) return PXA_STATUS_INVALID_ARGUMENT;
    if (!(fs->config.grants[e->grant_index].rights & PXA_FS_RIGHT_ENUMERATE))
        return PXA_STATUS_DENIED;
    m=mount_for(fs,e->epoch); if (!m) return PXA_STATUS_UNAVAILABLE;
    while (scanned++<256) {
        size_t n; struct stat st;
        char *relative=fs->native_path;int length;
        errno=0; item=readdir(e->directory);
        if (!item) {
            if (errno) return os_error();
            *end=1; return PXA_STATUS_OK;
        }
        n=strlen(item->d_name);
        if (!pxa_fs_scoped_path_is_valid((pxa_bytes_t){(const uint8_t *)item->d_name,n}))
            continue;
        length=snprintf(relative,PXA_SCOPED_FS_PATH_BYTES,"%s%s%s",e->relative,*e->relative?"/":"",item->d_name);
        if(length<0 || (size_t)length>=PXA_SCOPED_FS_PATH_BYTES)return PXA_STATUS_LIMIT_EXCEEDED;
        if (m->native_fat) {
            size_t prefix=strlen(m->host_path),bytes=(size_t)length;
            if (prefix>=sizeof(fs->native_path)-bytes-1) return PXA_STATUS_LIMIT_EXCEEDED;
            /* Reuse owner-serialized workspace, including overlap-safe prefix
             * insertion. Long names must not inflate the ESP task's stack. */
            memmove(relative+prefix+1,relative,bytes+1);
            memcpy(relative,m->host_path,prefix);relative[prefix]='/';
            if (stat(relative,&st)!=0) return os_error();
        } else {
#if !defined(ESP_PLATFORM)
            if (fstatat(dirfd(e->directory),item->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0)
                return os_error();
            if (S_ISREG(st.st_mode) && st.st_nlink!=1) continue;
#else
            return PXA_STATUS_UNSUPPORTED;
#endif
        }
        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) continue;
        /* readdir storage belongs to this DIR until its next enumeration.
         * The service serializes it before advancing this same directory. */
        info->name=(pxa_bytes_t){(const uint8_t *)item->d_name,n};
        info->kind=S_ISDIR(st.st_mode)?PXA_FS_KIND_DIRECTORY:PXA_FS_KIND_REGULAR;
        info->size=S_ISREG(st.st_mode)?(uint64_t)st.st_size:0;
        return PXA_STATUS_OK;
    }
    return PXA_STATUS_LIMIT_EXCEEDED;
}
size_t pxa_scoped_fs_workspace_size(const pxa_scoped_fs_config_t *c) {
    size_t entries;
    if (!c || c->struct_size<sizeof(*c) || !c->volumes || !c->mounts ||
        !c->grants || !c->grant_count || !c->max_open_resources)
        return 0;
    entries=(size_t)c->max_open_resources*sizeof(scoped_entry_t);
    if (entries>SIZE_MAX-sizeof(pxa_scoped_fs_t)-SCOPED_ALIGN) return 0;
    return SCOPED_ALIGN-1+sizeof(pxa_scoped_fs_t)+entries;
}
pxa_status_t pxa_scoped_fs_init(void *workspace,size_t bytes,
    const pxa_scoped_fs_config_t *c,pxa_scoped_fs_t **out,pxa_fs_roots_backend_t *b) {
    size_t need=pxa_scoped_fs_workspace_size(c); pxa_scoped_fs_t *fs; uint16_t i;
    if (!out || !b) return PXA_STATUS_INVALID_ARGUMENT;
    *out=NULL; memset(b,0,sizeof(*b));
    if (!workspace || !need || bytes<need || (uintptr_t)workspace>UINTPTR_MAX-bytes)
        return PXA_STATUS_INVALID_ARGUMENT;
    for (i=0;i<c->mount_count;++i) {
        if (!c->mounts[i].mount_epoch || !c->mounts[i].host_path ||
            c->mounts[i].host_path[0]!='/' ||
            strnlen(c->mounts[i].host_path,PXA_SCOPED_FS_PATH_BYTES)>=PXA_SCOPED_FS_PATH_BYTES)
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    fs=(pxa_scoped_fs_t *)(((uintptr_t)workspace+SCOPED_ALIGN-1)&~(uintptr_t)(SCOPED_ALIGN-1));
    memset(fs,0,sizeof(*fs)+(size_t)c->max_open_resources*sizeof(scoped_entry_t));
    fs->magic=SCOPED_MAGIC; fs->config=*c;
    b->struct_size=sizeof(*b); b->context=fs;
    b->files.struct_size=sizeof(b->files); b->files.context=fs;
    b->files.read=read_file; b->files.seek=seek_file;
    b->files.read_directory=read_directory; b->files.close=close_entry;
    b->open_root=open_root; b->open_at=open_at; b->stat_at=stat_at; b->query_root=query_root;
    b->read_at=read_at_file;
    *out=fs; return PXA_STATUS_OK;
}
void pxa_scoped_fs_deinit(pxa_scoped_fs_t *fs) {
    uint16_t i;
    if (!fs || fs->magic!=SCOPED_MAGIC) return;
    for (i=0;i<fs->config.max_open_resources;++i) close_entry(fs,&fs->entries[i],0);
    fs->magic=0;
}

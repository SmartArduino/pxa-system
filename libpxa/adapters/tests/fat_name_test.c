#define _GNU_SOURCE
#undef NDEBUG
#include "pxa/posix/pxa_scoped_fs.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static pxa_bytes_t bytes(const char *s) {
    return (pxa_bytes_t){(const uint8_t *)s,strlen(s)};
}
int main(void) {
    char root[]="/tmp/pxa-fat-names-XXXXXX",folder[256],file[512];
    assert(mkdtemp(root));
    snprintf(folder,sizeof(folder),"%s/Folder ~ real",root);assert(!mkdir(folder,0700));
    const char name[]="GG Aleste II ~ Power Strike II.gg";
    snprintf(file,sizeof(file),"%s/%s",folder,name);
    int fd=open(file,O_CREAT|O_EXCL|O_WRONLY,0600);assert(fd>=0);assert(!close(fd));
    pxa_volume_registry_t *registry;pxa_volume_info_t volume={0};uint64_t epoch;
    size_t size=pxa_volume_registry_workspace_size(1);void *volumes=malloc(size);assert(volumes);
    assert(!pxa_volume_registry_init(volumes,size,1,1,&registry));
    volume.id.bytes[0]=volume.device_id.bytes[0]=1;
    volume.state=PXA_VOLUME_READY;volume.capabilities=PXA_VOLUME_READ;
    assert(!pxa_volume_register(registry,&volume,&epoch));
    pxa_scoped_fs_mount_t mount={epoch,root,1};pxa_scoped_fs_grant_t grant={0};
    grant.id[0]=1;grant.volume_id=volume.id;grant.revision=1;grant.active=1;grant.rights=3;
    pxa_scoped_fs_config_t config={0};config.struct_size=sizeof(config);
    config.volumes=registry;config.mounts=&mount;config.mount_count=1;
    config.grants=&grant;config.grant_count=1;config.max_open_resources=3;
    size=pxa_scoped_fs_workspace_size(&config);void *memory=malloc(size);assert(memory);
    pxa_scoped_fs_t *fs;pxa_fs_roots_backend_t backend;
    assert(!pxa_scoped_fs_init(memory,size,&config,&fs,&backend));
    void *directory,*child,*rom;uint32_t rights;uint64_t features;pxa_fs_entry_t entry;
    assert(!backend.open_root(backend.context,PXA_FS_ROOT_GRANT,grant.id,&directory,&rights));
    assert(backend.open_at(backend.context,directory,bytes("FOLDER~1"),65,&child,&entry,&features)==PXA_STATUS_DENIED);
    assert(!backend.open_at(backend.context,directory,bytes("Folder ~ real"),65,&child,&entry,&features));
    uint8_t end=0;assert(!backend.files.read_directory(backend.files.context,child,&entry,&end));
    assert(!end && entry.name.size==strlen(name) && !memcmp(entry.name.data,name,strlen(name)));
    assert(backend.open_at(backend.context,child,bytes("GGAL~1.GG"),1,&rom,&entry,&features)==PXA_STATUS_DENIED);
    assert(!backend.open_at(backend.context,child,bytes(name),1,&rom,&entry,&features));
    backend.files.close(backend.files.context,rom,PXA_FS_KIND_REGULAR);
    backend.files.close(backend.files.context,child,PXA_FS_KIND_DIRECTORY);
    /* Every parent containing a tilde must also be a literal name. */
    assert(backend.open_at(backend.context,directory,bytes("FOLDER~1/GGAL~1.GG"),1,&rom,&entry,&features)==PXA_STATUS_DENIED);
    char relative[256];snprintf(relative,sizeof(relative),"Folder ~ real/%s",name);
    assert(!backend.open_at(backend.context,directory,bytes(relative),1,&rom,&entry,&features));
    backend.files.close(backend.files.context,rom,PXA_FS_KIND_REGULAR);
    backend.files.close(backend.files.context,directory,PXA_FS_KIND_DIRECTORY);
    pxa_scoped_fs_deinit(fs);free(memory);free(volumes);
    assert(!unlink(file));assert(!rmdir(folder));assert(!rmdir(root));
    puts("FAT literal tilde names enumerate/open; generated aliases remain denied");
}

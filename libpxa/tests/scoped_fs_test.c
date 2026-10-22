#define _GNU_SOURCE
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
static void put_file(const char *path,const char *s) {
    int fd=open(path,O_CREAT|O_WRONLY|O_EXCL,0600); assert(fd>=0);
    assert(write(fd,s,strlen(s))==(ssize_t)strlen(s)); assert(close(fd)==0);
}
static unsigned inventory_depth,inventory_entries;
static void enter_inventory(void *context) {
    assert(context==&inventory_depth && !inventory_depth);++inventory_depth;++inventory_entries;
}
static void leave_inventory(void *context) {
    assert(context==&inventory_depth && inventory_depth==1);--inventory_depth;
}
static void test_native_boundaries(void) {
    char root[]="/tmp/pxa-native-scope-XXXXXX",pxa[512],private[512],media[512],nested[512];
    const char *const protected[]={"PXA/apps","PXA/system"};
    pxa_volume_registry_t *registry;pxa_volume_info_t volume={0};pxa_scoped_fs_t *fs;
    pxa_scoped_fs_mount_t mounts[2]={0};pxa_scoped_fs_grant_t grant={0};
    pxa_scoped_fs_config_t config={0};pxa_fs_roots_backend_t backend;
    void *registry_memory,*memory,*directory,*child,*rejected;uint32_t rights;
    pxa_fs_entry_t entry;uint64_t epoch,nested_epoch,features;uint8_t end;unsigned count=0;
    assert(mkdtemp(root));
    snprintf(pxa,sizeof(pxa),"%s/PXA",root);assert(!mkdir(pxa,0700));
    snprintf(private,sizeof(private),"%s/PXA/apps",root);assert(!mkdir(private,0700));
    snprintf(media,sizeof(media),"%s/PXA/media",root);assert(!mkdir(media,0700));
    snprintf(nested,sizeof(nested),"%s/sdcard",root);assert(!mkdir(nested,0700));
    registry_memory=malloc(pxa_volume_registry_workspace_size(2));assert(registry_memory);
    assert(!pxa_volume_registry_init(registry_memory,pxa_volume_registry_workspace_size(2),2,1,&registry));
    volume.id.bytes[0]=1;volume.device_id.bytes[0]=1;volume.state=PXA_VOLUME_READY;
    volume.capabilities=PXA_VOLUME_READ;
    assert(!pxa_volume_register(registry,&volume,&epoch));grant.volume_id=volume.id;
    volume.id.bytes[0]=2;assert(!pxa_volume_register(registry,&volume,&nested_epoch));
    mounts[0]=(pxa_scoped_fs_mount_t){epoch,root,1};mounts[1]=(pxa_scoped_fs_mount_t){nested_epoch,nested,1};
    grant.id[0]=1;grant.revision=1;grant.active=1;grant.rights=3;
    config.struct_size=sizeof(config);config.volumes=registry;config.mounts=mounts;config.mount_count=2;
    config.grants=&grant;config.grant_count=1;config.max_open_resources=3;
    config.inventory_context=&inventory_depth;config.enter_inventory=enter_inventory;
    assert(!pxa_scoped_fs_workspace_size(&config)); /* Guards must be paired. */
    config.leave_inventory=leave_inventory;config.protected_paths=protected;config.protected_path_count=2;
    memory=malloc(pxa_scoped_fs_workspace_size(&config));assert(memory);
    assert(!pxa_scoped_fs_init(memory,pxa_scoped_fs_workspace_size(&config),&config,&fs,&backend));
    assert(!pxa_scoped_fs_validate_grant(fs,&grant));
    assert(!backend.open_root(backend.context,PXA_FS_ROOT_GRANT,grant.id,&directory,&rights));
    assert(backend.open_at(backend.context,directory,bytes("sdcard"),PXA_FS_OPEN_READ|PXA_FS_OPEN_DIRECTORY,
        &rejected,&entry,&features)==PXA_STATUS_DENIED);
    assert(backend.open_at(backend.context,directory,bytes("pxa/APPS/private"),PXA_FS_OPEN_READ,
        &rejected,&entry,&features)==PXA_STATUS_DENIED);
    assert(backend.open_at(backend.context,directory,bytes("PXA/system"),PXA_FS_OPEN_READ|PXA_FS_OPEN_DIRECTORY,
        &rejected,&entry,&features)==PXA_STATUS_DENIED);
    do {
        assert(!backend.files.read_directory(backend.files.context,directory,&entry,&end));
        if(!end) {assert(entry.name.size==3 && !memcmp(entry.name.data,"PXA",3));++count;}
    } while(!end);
    assert(count==1);
    assert(!backend.open_at(backend.context,directory,bytes("PXA"),PXA_FS_OPEN_READ|PXA_FS_OPEN_DIRECTORY,
        &child,&entry,&features));
    assert(!backend.files.read_directory(backend.files.context,child,&entry,&end) && !end);
    assert(entry.name.size==5 && !memcmp(entry.name.data,"media",5));
    assert(!backend.files.read_directory(backend.files.context,child,&entry,&end) && end);
    backend.files.close(backend.files.context,child,PXA_FS_KIND_DIRECTORY);
    backend.files.close(backend.files.context,directory,PXA_FS_KIND_DIRECTORY);
    strcpy(grant.relative_path,"PXA/apps");++grant.revision;
    assert(pxa_scoped_fs_validate_grant(fs,&grant)==PXA_STATUS_DENIED);
    /* The immutable resource prefix must not consume the Guest path budget.
     * This valid file has a volume-relative path longer than 255 bytes. */
    char generation[512],payload[512],segment[65],directories[4][512];
    strcpy(generation,media);memset(segment,'a',64);segment[64]=0;
    for(unsigned i=0;i<4;++i) {
        size_t length=strlen(generation);generation[length]='/';
        strcpy(generation+length+1,i==3?"package":segment);
        assert(!mkdir(generation,0700));strcpy(directories[i],generation);
    }
    assert(snprintf(payload,sizeof(payload),"%s/%s",generation,segment)<(int)sizeof(payload));
    put_file(payload,"resource");
    strcpy(grant.relative_path,generation+strlen(root)+1);++grant.revision;
    assert(!pxa_scoped_fs_validate_grant(fs,&grant));
    assert(!backend.open_root(backend.context,PXA_FS_ROOT_GRANT,grant.id,&directory,&rights));
    assert(!backend.open_at(backend.context,directory,bytes(segment),PXA_FS_OPEN_READ,&child,&entry,&features));
    backend.files.close(backend.files.context,child,PXA_FS_KIND_REGULAR);
    backend.files.close(backend.files.context,directory,PXA_FS_KIND_DIRECTORY);
    assert(!unlink(payload));for(int i=3;i>=0;--i)assert(!rmdir(directories[i]));
    pxa_scoped_fs_deinit(fs);assert(!inventory_depth && inventory_entries>=12);
    free(memory);free(registry_memory);
    assert(!rmdir(nested) && !rmdir(private) && !rmdir(media) && !rmdir(pxa) && !rmdir(root));
}
int main(void) {
    char root[]="/tmp/pxa-scoped-XXXXXX",p[512],q[512],io[32];
    void *registry_memory,*memory,*directory,*file,*child=NULL;
    pxa_volume_registry_t *registry; pxa_scoped_fs_t *fs;
    pxa_volume_info_t v={0},info; uint64_t epoch,clone,next,features,pos;
    pxa_scoped_fs_mount_t mounts[3]={0}; pxa_scoped_fs_grant_t grants[1]={0};
    pxa_scoped_fs_config_t config={0}; pxa_fs_roots_backend_t backend;
    pxa_fs_entry_t entry; pxa_fs_root_info_t root_info; size_t n; uint32_t rights;
    assert(mkdtemp(root));
    snprintf(p,sizeof(p),"%s/data",root); assert(!mkdir(p,0700));
    snprintf(p,sizeof(p),"%s/data/pak0.pak",root); put_file(p,"PACK0123456789");
    snprintf(p,sizeof(p),"%s/outside.bin",root); put_file(p,"OUTSIDE");
    snprintf(q,sizeof(q),"%s/data/link",root); assert(!symlink(p,q));
    snprintf(q,sizeof(q),"%s/data/hard",root); assert(!link(p,q));
    snprintf(p,sizeof(p),"%s/data/pipe",root); assert(!mkfifo(p,0600));
    registry_memory=malloc(pxa_volume_registry_workspace_size(3)); assert(registry_memory);
    assert(!pxa_volume_registry_init(registry_memory,pxa_volume_registry_workspace_size(3),3,100,&registry));
    v.id.bytes[0]=1; v.device_id.bytes[0]=2; v.state=PXA_VOLUME_READY;
    v.identity_strength=PXA_VOLUME_ID_MEDIA_BOUND; v.capabilities=PXA_VOLUME_READ;
    strcpy(v.name,"test SD partition");
    assert(!pxa_volume_register(registry,&v,&epoch));
    assert(epoch==100 && !pxa_volume_find(registry,&v.id,&info));
    assert(!pxa_volume_register(registry,&v,&clone));
    assert(pxa_volume_find(registry,&v.id,&info)==PXA_STATUS_BUSY);
    assert(!pxa_volume_get(registry,epoch,&info) && info.ambiguous);
    assert(!pxa_volume_unregister(registry,clone));
    assert(!pxa_volume_find(registry,&v.id,&info) && !info.ambiguous);
    {
        uint16_t cursor=0; uint64_t revision=pxa_volume_revision(registry);
        assert(!pxa_volume_next(registry,revision,&cursor,&info));
        assert(pxa_volume_next(registry,revision,&cursor,&info)==PXA_STATUS_NOT_FOUND);
        assert(!pxa_volume_update(registry,epoch,PXA_VOLUME_READY,100,90,3));
        cursor=0; assert(pxa_volume_next(registry,revision,&cursor,&info)==PXA_STATUS_BAD_STATE);
    }
    mounts[0]=(pxa_scoped_fs_mount_t){epoch,root,0}; mounts[1]=(pxa_scoped_fs_mount_t){epoch,root,0};
    grants[0].id[0]=5; grants[0].volume_id=v.id; grants[0].revision=1;
    grants[0].rights=3; grants[0].active=1; strcpy(grants[0].relative_path,"data");
    config.struct_size=sizeof(config); config.volumes=registry;
    config.mounts=mounts; config.mount_count=3; config.grants=grants;
    config.grant_count=1; config.max_open_resources=4;
    memory=malloc(pxa_scoped_fs_workspace_size(&config)); assert(memory);
    assert(!pxa_scoped_fs_init(memory,pxa_scoped_fs_workspace_size(&config),&config,&fs,&backend));
    assert(!pxa_scoped_fs_validate_grant(fs,&grants[0]));
    assert(!backend.open_root(backend.context,4,grants[0].id,&directory,&rights) && rights==3);
    assert(!backend.query_root(backend.context,directory,&root_info) && root_info.mount_epoch==epoch);
    assert(!backend.open_at(backend.context,directory,bytes("pak0.pak"),1,&file,&entry,&features));
    assert(entry.size==14 && entry.kind==1 && features==PXA_FS_FILE_FEATURE_READ_AT);
    assert(root_info.max_read_at_bytes==4096 && root_info.capabilities==1);
    assert(!backend.files.seek(backend.files.context,file,5,0,&pos));
    assert(!backend.read_at(backend.files.context,file,10,(uint8_t *)io,4,&n));
    assert(n==4 && !memcmp(io,"6789",4));
    assert(!backend.files.seek(backend.files.context,file,0,1,&pos) && pos==5);
    assert(!backend.read_at(backend.files.context,file,UINT64_MAX,(uint8_t *)io,4,&n) && !n);
    assert(!backend.files.seek(backend.files.context,file,0,0,&pos));
    assert(!backend.files.read(backend.files.context,file,(uint8_t *)io,sizeof(io),&n));
    assert(n==14 && !memcmp(io,"PACK0123456789",14));
    assert(!backend.files.seek(backend.files.context,file,-4,2,&pos) && pos==10);
    assert(!backend.files.read(backend.files.context,file,(uint8_t *)io,4,&n));
    assert(n==4 && !memcmp(io,"6789",4));
    assert(!backend.files.read(backend.files.context,file,(uint8_t *)io,4,&n) && !n);
    assert(backend.open_at(backend.context,directory,bytes("link"),1,&child,&entry,&features)!=0);
    assert(backend.open_at(backend.context,directory,bytes("hard"),1,&child,&entry,&features)!=0);
    assert(backend.open_at(backend.context,directory,bytes("pipe"),1,&child,&entry,&features)!=0);
    assert(backend.open_at(backend.context,directory,bytes("pak0.pak"),2,&child,&entry,&features)==PXA_STATUS_DENIED);
    {
        const char *bad[]={"../outside.bin","/outside.bin",".PXA-store/x","x/../x","x.","x ","0:/x","x\\y"};
        unsigned i;
        for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i)
            assert(backend.open_at(backend.context,directory,bytes(bad[i]),1,&child,&entry,&features)==PXA_STATUS_INVALID_ARGUMENT);
    }
    /* Replacing the directory pathname cannot redirect the existing fd root. */
    snprintf(p,sizeof(p),"%s/data",root); snprintf(q,sizeof(q),"%s/moved",root);
    assert(!rename(p,q)); assert(!symlink("/",p));
    assert(!backend.open_at(backend.context,directory,bytes("pak0.pak"),1,&child,&entry,&features));
    backend.files.close(backend.files.context,child,1);
    backend.files.close(backend.files.context,directory,2);
    /* Independently open files survive closing their directory. */
    assert(!backend.files.seek(backend.files.context,file,0,0,&pos));
    assert(!pxa_volume_register(registry,&v,&clone));
    assert(backend.files.read(backend.files.context,file,(uint8_t *)io,4,&n)==PXA_STATUS_UNAVAILABLE);
    assert(!pxa_volume_unregister(registry,clone));
    grants[0].active=0;
    assert(backend.files.read(backend.files.context,file,(uint8_t *)io,4,&n)==PXA_STATUS_DENIED);
    assert(backend.read_at(backend.files.context,file,0,(uint8_t *)io,4,&n)==PXA_STATUS_DENIED);
    grants[0].active=1;
    assert(!pxa_volume_unregister(registry,epoch));
    assert(!pxa_volume_register(registry,&v,&next) && next!=epoch);
    memset(&mounts[0],0,sizeof(mounts[0]));
    memset(&mounts[1],0,sizeof(mounts[1]));
    mounts[2]=(pxa_scoped_fs_mount_t){next,root,0};
    assert(backend.files.read(backend.files.context,file,(uint8_t *)io,4,&n)==PXA_STATUS_UNAVAILABLE);
    assert(backend.read_at(backend.files.context,file,0,(uint8_t *)io,4,&n)==PXA_STATUS_UNAVAILABLE);
    backend.files.close(backend.files.context,file,1);
    /* The live backend sees a newly registered mount in a previously empty
     * slot. Its stale file handle could not silently follow this new epoch. */
    strcpy(grants[0].relative_path,"moved"); ++grants[0].revision;
    assert(!backend.open_root(backend.context,4,grants[0].id,&directory,&rights));
    assert(!backend.query_root(backend.context,directory,&root_info) && root_info.mount_epoch==next);
    assert(!backend.open_at(backend.context,directory,bytes("pak0.pak"),1,&file,&entry,&features));
    assert(!backend.files.read(backend.files.context,file,(uint8_t *)io,4,&n) && n==4 && !memcmp(io,"PACK",4));
    backend.files.close(backend.files.context,file,1);
    backend.files.close(backend.files.context,directory,2);
    pxa_scoped_fs_deinit(fs); free(memory); free(registry_memory);
    snprintf(p,sizeof(p),"%s/data",root); assert(!unlink(p));
    snprintf(p,sizeof(p),"%s/moved/pak0.pak",root); assert(!unlink(p));
    snprintf(p,sizeof(p),"%s/moved/link",root); assert(!unlink(p));
    snprintf(p,sizeof(p),"%s/moved/hard",root); assert(!unlink(p));
    snprintf(p,sizeof(p),"%s/moved/pipe",root); assert(!unlink(p));
    snprintf(p,sizeof(p),"%s/moved",root); assert(!rmdir(p));
    snprintf(p,sizeof(p),"%s/outside.bin",root); assert(!unlink(p)); assert(!rmdir(root));
    test_native_boundaries();
    puts("scoped roots: private trees, nested mounts, inventory guards, links, epochs and revocation passed");
    return 0;
}

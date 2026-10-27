/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "pxa/file_import.h"
#include "pxa/posix/pxa_posix_fs.h"
#include "pxa/posix/pxa_import_destination.h"
#include "pxa/posix/pxa_file_browser.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct {
    pxa_scoped_fs_t *source;
    pxa_import_destination_t destination;
    pxa_fs_backend_t original;
    pxa_volume_registry_t *volumes;
    uint64_t epoch;
    unsigned ready,flush_fail,write_fail;
    size_t largest_read,largest_write;
} fixture_t;
static pxa_status_t source_open(void *c,const pxa_file_choice_t *choice,void **out,pxa_fs_entry_t *info) {
    fixture_t *f=c;return pxa_scoped_fs_open_trusted(f->source,choice->epoch,choice->path,PXA_FS_KIND_REGULAR,out,info);
}
static pxa_status_t ready(void *c) {return ((fixture_t *)c)->ready?0:PXA_STATUS_UNAVAILABLE;}
static pxa_status_t flush(void *c,void *resource) {
    fixture_t *f=c;return f->flush_fail?PXA_STATUS_IO_ERROR:pxa_import_destination_flush(&f->destination,resource);
}
static fixture_t *write_fixture;
static pxa_status_t write_short(void *c,void *r,const uint8_t *p,size_t n,size_t *written) {
    fixture_t *f=write_fixture;(void)c;if(n>f->largest_write)f->largest_write=n;
    if(f->write_fail)return PXA_STATUS_QUOTA_EXCEEDED;
    return f->original.write(f->original.context,r,p,n>173?173:n,written);
}
static void put(const char *root,const char *name,size_t size) {
    char path[2048];assert(snprintf(path,sizeof(path),"%s/%s",root,name)<(int)sizeof(path));
    FILE *file=fopen(path,"wb");assert(file);
    uint8_t chunk[4096];for(size_t i=0;i<sizeof(chunk);++i)chunk[i]=(uint8_t)i;
    while(size) {size_t n=size>sizeof(chunk)?sizeof(chunk):size;assert(fwrite(chunk,1,n,file)==n);size-=n;}
    assert(!fclose(file));
}
static void verify(const char *root,const char *name,size_t expected) {
    char path[2048];snprintf(path,sizeof(path),"%s/%s",root,name);FILE *file=fopen(path,"rb");assert(file);
    uint8_t chunk[4096];size_t total=0,n;
    while((n=fread(chunk,1,sizeof(chunk),file))) {for(size_t i=0;i<n;++i)assert(chunk[i]==(uint8_t)(total+i));total+=n;}
    assert(!ferror(file) && total==expected);fclose(file);
}
static void no_partials(const char *root) {
    DIR *dir=opendir(root);assert(dir);struct dirent *entry;
    while((entry=readdir(dir)))assert(strncmp(entry->d_name,".import-",8));closedir(dir);
}
static void finish(pxa_file_import_t *job) {
    pxa_status_t status;unsigned steps=0;
    do {status=pxa_file_import_step(job,1);assert(++steps<20000);}while(status==PXA_STATUS_WOULD_BLOCK);
    assert(status==PXA_STATUS_OK);
}
static pxa_status_t emit(void *context,size_t index,uint8_t kind,pxa_bytes_t name) {
    (void)index;(void)kind;assert(name.size);++*(unsigned *)context;return PXA_STATUS_OK;
}
int main(void) {
    char root[]="/tmp/pxa-import-XXXXXX",destination[1024],private[1024],link[1024],target[1024];
    assert(mkdtemp(root));snprintf(destination,sizeof(destination),"%s/output",root);assert(!mkdir(destination,0700));
    snprintf(private,sizeof(private),"%s/private",root);assert(!mkdir(private,0700));put(private,"hidden.gba",10);
    fixture_t f={.ready=1};pxa_fs_roots_backend_t source;
    pxa_scoped_fs_mount_t mount={0};pxa_scoped_fs_grant_t grant={0};
    void *vm=malloc(pxa_volume_registry_workspace_size(1));assert(vm);
    assert(!pxa_volume_registry_init(vm,pxa_volume_registry_workspace_size(1),1,10,&f.volumes));
    pxa_volume_info_t volume={0};volume.id.bytes[0]=1;volume.device_id=volume.id;volume.state=PXA_VOLUME_READY;
    volume.identity_strength=PXA_VOLUME_ID_MEDIA_BOUND;volume.capabilities=PXA_VOLUME_READ;
    assert(!pxa_volume_register(f.volumes,&volume,&f.epoch));mount.mount_epoch=f.epoch;mount.host_path=root;
    const char *protected_paths[]={"private"};
    pxa_scoped_fs_config_t sc={.struct_size=sizeof(sc),.volumes=f.volumes,.mounts=&mount,.mount_count=1,
        .grants=&grant,.grant_count=1,.max_open_resources=4,.protected_paths=protected_paths,.protected_path_count=1};
    size_t size=pxa_scoped_fs_workspace_size(&sc);void *sm=malloc(size);assert(sm);
    assert(!pxa_scoped_fs_init(sm,size,&sc,&f.source,&source));
    pxa_file_import_config_t config={.context=&f,.source=source.files,.open_source=source_open,
        .destination_ready=ready,.flush_destination=flush};
    assert(!pxa_import_destination_open(&f.destination,destination,&config.destination));f.original=config.destination;
    /* Wrap only write, retaining the real backend context on other operations. */
    assert(pxa_file_import_matches("中文测试.GBA","gb,gbc,gba"));
    assert(!pxa_file_import_matches("test.gba.zip","gba"));assert(pxa_file_import_matches("anything",""));
    put(root,"中文测试.GBA",32u*1024u*1024u);put(root,"small.gba",9000);
    snprintf(target,sizeof(target),"%s/small.gba",root);snprintf(link,sizeof(link),"%s/link.gba",root);assert(!symlink(target,link));
    pxa_file_choice_t choices[]={{f.epoch,"中文测试.GBA"},{f.epoch,"small.gba"},{f.epoch,"private/hidden.gba"},{f.epoch,"link.gba"}};
    pxa_file_import_t job;assert(sizeof(job)<6144);
    assert(!pxa_file_import_begin(&job,&config,choices,4,123));finish(&job);
    assert(job.result.imported==2 && job.result.failed==2 && job.result.first_error==PXA_STATUS_DENIED);
    verify(destination,"中文测试.GBA",32u*1024u*1024u);verify(destination,"small.gba",9000);no_partials(destination);
    /* Duplicate import retains the existing original and publishes a new name. */
    assert(!pxa_file_import_begin(&job,&config,choices+1,1,124));finish(&job);
    assert(job.result.imported==1);verify(destination,"small (2).gba",9000);verify(root,"small.gba",9000);
    /* Source cancellation and failure must never publish incomplete ROMs. */
    assert(!pxa_file_import_begin(&job,&config,choices,1,125));assert(pxa_file_import_step(&job,4)==PXA_STATUS_WOULD_BLOCK);
    pxa_file_import_cancel(&job);no_partials(destination);
    f.flush_fail=1;assert(!pxa_file_import_begin(&job,&config,choices+1,1,126));finish(&job);
    assert(job.result.failed==1 && !job.result.imported);no_partials(destination);f.flush_fail=0;
    /* Exercise partial writes and out-of-space through the actual copy loop. */
    write_fixture=&f;config.destination.write=write_short;
    assert(!pxa_file_import_begin(&job,&config,choices+1,1,127));finish(&job);
    assert(job.result.imported==1 && f.largest_write<=4096);verify(destination,"small (3).gba",9000);
    f.write_fail=1;assert(!pxa_file_import_begin(&job,&config,choices+1,1,128));finish(&job);
    assert(job.result.failed==1 && job.result.first_error==PXA_STATUS_QUOTA_EXCEEDED);no_partials(destination);
    pxa_fs_entry_t info;void *resource=NULL;size_t written;uint8_t data[256];
    /* Trusted enumeration bypasses grants, but never private paths or links. */
    size_t count;uint8_t more;unsigned emitted=0;
    assert(!pxa_file_browser_page(f.source,&source.files,f.epoch,"","gba",0,12,&count,&more,emit,&emitted));
    assert(count==3 && emitted==3 && !more); /* two ROMs and the output directory */
    /* A duplicate at NAME_MAX must shorten only its stem, preserving the
     * extension and UTF-8 boundary rather than failing or overwriting. */
    config.destination=f.original;config.maximum_name_bytes=255;
    char long_name[256],duplicate[256];
    memset(long_name,'a',251);memcpy(long_name+251,".gba",5);
    memset(duplicate,'a',247);memcpy(duplicate+247," (2).gba",9);
    put(root,long_name,37);pxa_file_choice_t long_choice={f.epoch,long_name};
    assert(!pxa_file_import_begin(&job,&config,&long_choice,1,129));finish(&job);
    assert(job.result.imported==1);
    assert(!pxa_file_import_begin(&job,&config,&long_choice,1,130));finish(&job);
    assert(job.result.imported==1);verify(destination,duplicate,37);verify(destination,long_name,37);
    /* Model FAT's UTF-16 budget using a small limit on a real directory. */
    config.maximum_name_bytes=0;config.maximum_name_utf16_units=9;
    put(root,"中文测.gba",29);pxa_file_choice_t unicode_choice={f.epoch,"中文测.gba"};
    assert(!pxa_file_import_begin(&job,&config,&unicode_choice,1,131));finish(&job);
    assert(job.result.imported==1);
    assert(!pxa_file_import_begin(&job,&config,&unicode_choice,1,132));finish(&job);
    assert(job.result.imported==1);verify(destination,"中 (2).gba",29);no_partials(destination);
    assert(pxa_scoped_fs_open_trusted(f.source,f.epoch,"private",2,&resource,&info)==PXA_STATUS_DENIED);
    assert(!pxa_scoped_fs_open_trusted(f.source,f.epoch,"small.gba",1,&resource,&info));
    assert(!pxa_volume_unregister(f.volumes,f.epoch));
    assert(source.files.read(source.files.context,resource,data,sizeof(data),&written)==PXA_STATUS_UNAVAILABLE);
    source.files.close(source.files.context,resource,1);
    pxa_scoped_fs_deinit(f.source);pxa_import_destination_close(&f.destination);free(vm);free(sm);
    assert(!unlink(link));assert(!pxa_posix_fs_remove_tree(root));
    puts("PASS: 32 MiB bounded import, original preservation, duplicates, UTF-8, cancellation, flush failure, private/link denial and unplug");
}

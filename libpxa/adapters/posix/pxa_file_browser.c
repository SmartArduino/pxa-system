/* SPDX-License-Identifier: MIT */
#include "pxa/posix/pxa_file_browser.h"
#include "pxa/file_import.h"
#include <string.h>
#include <stdlib.h>
pxa_status_t pxa_file_browser_page(pxa_scoped_fs_t *fs,const pxa_fs_backend_t *files,
    uint64_t epoch,const char *path,const char *extensions,size_t skip,size_t capacity,
    size_t *count,uint8_t *more,pxa_file_browser_emit_fn emit,void *context) {
    void *directory=NULL; pxa_fs_entry_t entry; size_t seen=0; uint8_t end=0;
    pxa_status_t status;
    if(!count || !more || !emit || !files || !capacity)return PXA_STATUS_INVALID_ARGUMENT;
    *count=0;*more=0;
    status=pxa_scoped_fs_open_trusted(fs,epoch,path,PXA_FS_KIND_DIRECTORY,&directory,&entry);
    if(status!=PXA_STATUS_OK)return status;
    while((status=files->read_directory(files->context,directory,&entry,&end))==PXA_STATUS_OK && !end) {
        if(entry.name.size>PXA_FS_SCOPED_MAX_PATH_BYTES) {status=PXA_STATUS_LIMIT_EXCEEDED;break;}
        char *name=malloc(entry.name.size+1);if(!name) {status=PXA_STATUS_RESOURCE_LIMIT;break;}
        memcpy(name,entry.name.data,entry.name.size);name[entry.name.size]=0;
        int include=entry.kind==PXA_FS_KIND_DIRECTORY ||
            (entry.kind==PXA_FS_KIND_REGULAR && pxa_file_import_matches(name,extensions));
        free(name);if(!include)continue;
        if(seen++<skip)continue;
        if(*count==capacity) {*more=1;break;}
        status=emit(context,*count,entry.kind,entry.name);
        if(status!=PXA_STATUS_OK)break;
        ++*count;
    }
    files->close(files->context,directory,PXA_FS_KIND_DIRECTORY);
    return status;
}

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pxa/posix/pxa_posix_app_store.h"
#include <stdio.h>
#include <string.h>

pxa_status_t pxa_posix_app_home_path(const pxa_posix_app_home_t *home,uint8_t kind,
    char *output,size_t capacity) {
    const char *child=kind==PXA_POSIX_APP_HOME_PACKAGE?"package":kind==PXA_POSIX_APP_HOME_PRIVATE?"private":NULL;
    size_t length;
    if(!home || !output || !capacity || !child)return PXA_STATUS_INVALID_ARGUMENT;
    output[0]=0;length=strnlen(home->home,sizeof(home->home));
    if(!length || length==sizeof(home->home) || home->home[0]!='/')return PXA_STATUS_INVALID_ARGUMENT;
    if(kind==PXA_POSIX_APP_HOME_PRIVATE && home->record.data_generation) {
        int n=snprintf(output,capacity,"%s/data/%016llx",home->home,(unsigned long long)home->record.data_generation);
        if(n<0 || (size_t)n>=capacity) {output[0]=0;return PXA_STATUS_LIMIT_EXCEEDED;}
        return PXA_STATUS_OK;
    }
    if(length+strlen(child)+2>capacity)return PXA_STATUS_LIMIT_EXCEEDED;
    memcpy(output,home->home,length);output[length]='/';strcpy(output+length+1,child);
    return PXA_STATUS_OK;
}

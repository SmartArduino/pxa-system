/* SPDX-License-Identifier: MIT */
#include "pxa/file_import.h"
#include <stdio.h>
#include <string.h>

static pxa_bytes_t bytes(const char *s) {return (pxa_bytes_t){(const uint8_t *)s,strlen(s)};}
static unsigned lower(unsigned c) {return c>='A'&&c<='Z'?c+'a'-'A':c;}
int pxa_file_import_matches(const char *name,const char *extensions) {
    if(!name || !extensions)return 0;
    if(!*extensions)return 1;
    const char *dot=strrchr(name,'.');if(!dot || !dot[1])return 0;
    ++dot;
    for(const char *s=extensions;*s;) {
        size_t n=strcspn(s,",");
        if(strlen(dot)==n) {
            size_t i=0;while(i<n && lower((unsigned char)dot[i])==lower((unsigned char)s[i]))++i;
            if(i==n)return 1;
        }
        s+=n;if(*s)++s;
    }
    return 0;
}
static void close_files(pxa_file_import_t *job) {
    const pxa_fs_backend_t *src=&job->config.source,*dst=&job->config.destination;
    if(job->input)src->close(src->context,job->input,PXA_FS_KIND_REGULAR);
    if(job->output)dst->close(dst->context,job->output,PXA_FS_KIND_REGULAR);
    job->input=job->output=NULL;
}
static void discard(pxa_file_import_t *job) {
    close_files(job);
    if(job->temporary_created) {
        /* Never remove a file through a remounted/rebound destination. */
        if(job->config.destination_ready(job->config.context)==PXA_STATUS_OK)
            (void)job->config.destination.remove(job->config.destination.context,bytes(job->temporary));
        job->temporary_created=0;
    }
}
static void next(pxa_file_import_t *job,pxa_status_t status) {
    if(status!=PXA_STATUS_OK) {
        discard(job);++job->result.failed;
        if(!job->result.first_error)job->result.first_error=status;
    }
    ++job->index;job->suffix=0;job->copied=job->expected=0;
}
pxa_status_t pxa_file_import_begin(pxa_file_import_t *job,const pxa_file_import_config_t *config,
    const pxa_file_choice_t *choices,size_t count,uint64_t nonce) {
    if(!job || !config || !choices || !count || count>UINT32_MAX || !nonce ||
        !config->open_source || !config->source.read || !config->source.close ||
        !config->destination.open || !config->destination.write || !config->destination.close ||
        !config->destination.stat || !config->destination.rename || !config->destination.remove ||
        !config->destination_ready || !config->flush_destination)return PXA_STATUS_INVALID_ARGUMENT;
    memset(job,0,sizeof(*job));job->config=*config;job->choices=choices;job->count=count;
    job->nonce=nonce;job->active=1;return PXA_STATUS_OK;
}
static size_t utf16_units(const char *text,size_t bytes) {
    size_t units=0;
    for(size_t i=0;i<bytes;++i)if(((unsigned char)text[i]&0xc0)!=0x80)
        units+=(unsigned char)text[i]>=0xf0?2:1;
    return units;
}
static pxa_status_t candidate_name(pxa_file_import_t *job) {
    const char *path=job->choices[job->index].path,*name=strrchr(path,'/');name=name?name+1:path;
    if(!*name || strlen(name)>PXA_FS_SCOPED_MAX_PATH_BYTES || !pxa_fs_scoped_path_is_valid(bytes(name)))
        return PXA_STATUS_INVALID_ARGUMENT;
    if(job->suffix==1) {strcpy(job->name,name);return PXA_STATUS_OK;}
    const char *dot=strrchr(name,'.');if(!dot || dot==name)dot=name+strlen(name);
    char suffix[32];int n=snprintf(suffix,sizeof(suffix)," (%u)",(unsigned)job->suffix);
    size_t ext=strlen(dot),stem=(size_t)(dot-name);
    if(n<0 || (size_t)n+ext>=sizeof(job->name))return PXA_STATUS_LIMIT_EXCEEDED;
    size_t budget=job->config.maximum_name_bytes?job->config.maximum_name_bytes:sizeof(job->name)-1;
    if(budget>sizeof(job->name)-1)budget=sizeof(job->name)-1;
    if((size_t)n+ext>=budget)return PXA_STATUS_LIMIT_EXCEEDED;
    size_t max=budget-(size_t)n-ext;
    if(stem>max) {stem=max;while(stem && ((unsigned char)name[stem]&0xc0)==0x80)--stem;}
    if(job->config.maximum_name_utf16_units) {
        size_t reserved=(size_t)n+utf16_units(dot,ext);
        if(reserved>=job->config.maximum_name_utf16_units)return PXA_STATUS_LIMIT_EXCEEDED;
        while(stem && utf16_units(name,stem)>job->config.maximum_name_utf16_units-reserved) {
            --stem;while(stem && ((unsigned char)name[stem]&0xc0)==0x80)--stem;
        }
    }
    if(!stem)return PXA_STATUS_LIMIT_EXCEEDED;
    memcpy(job->name,name,stem);memcpy(job->name+stem,suffix,(size_t)n);
    memcpy(job->name+stem+n,dot,ext+1);return PXA_STATUS_OK;
}
pxa_status_t pxa_file_import_step(pxa_file_import_t *job,unsigned chunks) {
    if(!job || !job->active || !chunks)return PXA_STATUS_INVALID_ARGUMENT;
    const pxa_fs_backend_t *src=&job->config.source,*dst=&job->config.destination;
    while(chunks-- && job->index<job->count) {
        pxa_status_t status=job->config.destination_ready(job->config.context);
        if(status!=PXA_STATUS_OK) {next(job,status);continue;}
        if(!job->input) {
            if(job->config.same_file && job->config.same_file(job->config.context,&job->choices[job->index])) {
                ++job->result.skipped;next(job,PXA_STATUS_OK);continue;
            }
            pxa_fs_entry_t info={0};
            status=job->config.open_source(job->config.context,&job->choices[job->index],&job->input,&info);
            if(status==PXA_STATUS_OK && (!job->input || info.kind!=PXA_FS_KIND_REGULAR))status=PXA_STATUS_DENIED;
            if(status!=PXA_STATUS_OK) {next(job,status);continue;}
            job->expected=info.size;job->suffix=1;
        }
        if(!job->output) {
            pxa_fs_entry_t info={0};status=candidate_name(job);
            if(status!=PXA_STATUS_OK) {next(job,status);continue;}
            status=dst->stat(dst->context,bytes(job->name),&info);
            if(status==PXA_STATUS_OK) {
                if(job->suffix==UINT32_MAX)next(job,PXA_STATUS_RESOURCE_LIMIT);
                else ++job->suffix;
                continue;
            }
            if(status!=PXA_STATUS_NOT_FOUND) {next(job,status);continue;}
            snprintf(job->temporary,sizeof(job->temporary),".import-%016llx-%08x.part",
                (unsigned long long)job->nonce,(unsigned)job->index);
            uint8_t kind=0;status=dst->open(dst->context,bytes(job->temporary),
                PXA_FS_OPEN_WRITE|PXA_FS_OPEN_CREATE|PXA_FS_OPEN_EXCLUSIVE,&job->output,&kind);
            if(status!=PXA_STATUS_OK) {next(job,status);continue;}
            job->temporary_created=1;
            if(!job->output || kind!=PXA_FS_KIND_REGULAR) {next(job,PXA_STATUS_INTERNAL);continue;}
        }
        size_t n=0;status=src->read(src->context,job->input,job->buffer,sizeof(job->buffer),&n);
        if(status!=PXA_STATUS_OK || n>sizeof(job->buffer) || n>job->expected-job->copied) {
            next(job,status!=PXA_STATUS_OK?status:PXA_STATUS_IO_ERROR);continue;
        }
        if(n) {
            size_t done=0;
            while(done<n) {
                size_t wrote=0;status=dst->write(dst->context,job->output,job->buffer+done,n-done,&wrote);
                if(status!=PXA_STATUS_OK || !wrote || wrote>n-done)break;
                done+=wrote;
            }
            if(status!=PXA_STATUS_OK || done!=n) {next(job,status?status:PXA_STATUS_IO_ERROR);continue;}
            job->copied+=n;job->total_copied+=n;continue;
        }
        if(job->copied!=job->expected) {next(job,PXA_STATUS_IO_ERROR);continue;}
        status=job->config.flush_destination(job->config.context,job->output);
        close_files(job);
        if(status==PXA_STATUS_OK)status=job->config.destination_ready(job->config.context);
        if(status==PXA_STATUS_OK)status=dst->rename(dst->context,bytes(job->temporary),bytes(job->name));
        if(status!=PXA_STATUS_OK) {next(job,status);continue;}
        job->temporary_created=0;++job->result.imported;next(job,PXA_STATUS_OK);
    }
    if(job->index<job->count)return PXA_STATUS_WOULD_BLOCK;
    job->active=0;return PXA_STATUS_OK;
}
void pxa_file_import_cancel(pxa_file_import_t *job) {
    if(job && job->active) {discard(job);job->active=0;}
}

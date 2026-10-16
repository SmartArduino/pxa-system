/* Exercise the actual decoder against the independent, ordered TGH snapshot.
 * Every spelling, every candidate page and both keyboard layouts are visited. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pxa_lvgl_ime_builtin.h"
#ifndef EXPECT_SCRIPTS
#define EXPECT_SCRIPTS 1
#endif
static char target[8], wanted_py[8];
static bool found,committed;
static uint32_t chosen;
static unsigned pinyin_index,candidate_count;
static bool more;
static size_t utf8_size(const char *s) {
    unsigned char c=(unsigned char)*s;
    return c<128?1:c<224?2:c<240?3:4;
}
static pxa_status_t present(void *context,const pxa_ime_frame_t *f) {
    (void)context;
    if(f->edit==PXA_IME_EDIT_INSERT)committed=f->commit.size==strlen(target) && !memcmp(f->commit.data,target,f->commit.size);
    size_t offset=0;candidate_count=f->candidate_count;
    more=(f->flags&PXA_IME_MORE_CANDIDATES)!=0;pinyin_index=UINT32_MAX;
    for (unsigned i=0;i<(unsigned)f->candidate_count+f->pinyin_count;++i) {
        assert(offset+2<=f->items.size);
        unsigned n=f->items.data[offset]|(unsigned)f->items.data[offset+1]<<8;offset+=2;
        assert(n && offset+n<=f->items.size);
        if(i<f->candidate_count && n==strlen(target) && !memcmp(f->items.data+offset,target,n)) {found=true;chosen=f->page_start+i;}
        if(i>=f->candidate_count && n==strlen(wanted_py) && !memcmp(f->items.data+offset,wanted_py,n))pinyin_index=i-f->candidate_count;
        offset+=n;
    }
    assert(offset==f->items.size);return PXA_STATUS_OK;
}
static void input(pxa_lvgl_ime_builtin_t *s,unsigned layout,unsigned action,unsigned value,const char *text) {
    pxa_bytes_t bytes={(const uint8_t *)text,text?strlen(text):0};
    assert(pxa_lvgl_ime_builtin_input(s,action,layout,value,bytes,present,NULL)==PXA_STATUS_OK);
}
static void check(pxa_lvgl_ime_builtin_t *s,const char *py,const char *character,unsigned layout) {
    strcpy(wanted_py,py);size_t bytes=utf8_size(character);memcpy(target,character,bytes);target[bytes]=0;found=false;
    input(s,layout,PXA_IME_RESET,0,NULL);found=false;
    char keys[8];strcpy(keys,py);
    if(layout==PXA_IME_K9) {
        static const char map[]="22233344455566677778889999";
        for(unsigned i=0;keys[i];++i)keys[i]=map[(unsigned)(keys[i]-'a')];
    }
    input(s,layout,PXA_IME_TEXT,0,keys);
    assert(pinyin_index!=UINT32_MAX);
    found=false;input(s,layout,PXA_IME_CHOOSE_PINYIN,pinyin_index,NULL);
    unsigned page=0;
    while(!found && more) {assert(candidate_count && page<65535);page+=candidate_count;input(s,layout,PXA_IME_PAGE,page,NULL);}
    if(!found)fprintf(stderr,"Unreachable %s/%s layout=%u\n",py,target,layout);
    assert(found);committed=false;input(s,layout,PXA_IME_CHOOSE,chosen,NULL);assert(committed);
}
int main(int argc,char **argv) {
    assert(argc==3 && pxa_lvgl_ime_builtin_scripts()==EXPECT_SCRIPTS);
    pxa_lvgl_ime_builtin_t *s=pxa_lvgl_ime_builtin_create(NULL);
    if(!EXPECT_SCRIPTS) {assert(!s);puts("Builtin dictionaries disabled: no decoder allocation");return 0;}
    assert(s);unsigned checks=0,total=0;
    for(unsigned script=0;script<2;++script) {
        if(!(EXPECT_SCRIPTS&(1u<<script)))continue;
        assert(pxa_lvgl_ime_builtin_set_traditional(s,script!=0));
        FILE *f=fopen(argv[script?2:1],"r");assert(f);char line[512];unsigned lines=0;
        while(fgets(line,sizeof(line),f)) {
            if(line[0]=='#')continue;
            char *character,*readings;
            if(script) {character=strtok(line,"\t");readings=strtok(NULL,"\r\n");}
            else {assert(strtok(line,"\t"));character=strtok(NULL,"\t");readings=strtok(NULL,"\t");}
            assert(character && readings);++lines;
            char *py=readings;
            while(py && *py) {
                char *next=strchr(py,',');if(next)*next++=0;
                for(unsigned layout=0;layout<2;++layout) {check(s,py,character,layout);++checks;}
                py=next;
            }
        }
        assert(script ? lines>8593 : lines==8105);total+=lines;fclose(f);
    }
    pxa_lvgl_ime_builtin_destroy(s);
    printf("TGH characters=%u decoder_checks=%u scripts=%u: complete readings, T9/QWERTY and all candidate pages passed\n",total,checks,EXPECT_SCRIPTS);
}

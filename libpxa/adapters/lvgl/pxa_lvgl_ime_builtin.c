#include "pxa_lvgl_ime_builtin.h"
#include <stdlib.h>
#include <string.h>

#ifndef PXA_IME_BUILTIN_SIMPLIFIED
#define PXA_IME_BUILTIN_SIMPLIFIED 1
#endif
#ifndef PXA_IME_BUILTIN_TRADITIONAL
#define PXA_IME_BUILTIN_TRADITIONAL 0
#endif

typedef struct { char spelling[7]; uint8_t length; uint16_t offset; } builtin_syllable_t;
typedef struct {
    const builtin_syllable_t *rows;
    const char *characters;
    uint16_t count;
} builtin_dictionary_t;
#if PXA_IME_BUILTIN_SIMPLIFIED
#include "dictionaries/simplified.h"
static const builtin_dictionary_t simplified={builtin_simplified_rows,
    (const char *)builtin_simplified_characters,sizeof(builtin_simplified_rows)/sizeof(*builtin_simplified_rows)};
#endif
#if PXA_IME_BUILTIN_TRADITIONAL
#include "dictionaries/traditional.h"
static const builtin_dictionary_t traditional={builtin_traditional_rows,
    (const char *)builtin_traditional_characters,sizeof(builtin_traditional_rows)/sizeof(*builtin_traditional_rows)};
#endif

enum { BUILTIN_MAX_INPUT=64, BUILTIN_MAX_SPELLINGS=32, BUILTIN_MAX_CANDIDATES=12 };
struct pxa_lvgl_ime_builtin {
    const builtin_dictionary_t *dictionary;
    char input[BUILTIN_MAX_INPUT+1];
    uint16_t spellings[BUILTIN_MAX_SPELLINGS], entries, selected, locked;
    uint8_t count, size, consumed, locked_size;
    uint32_t page;
};
unsigned pxa_lvgl_ime_builtin_scripts(void) {
    return (PXA_IME_BUILTIN_SIMPLIFIED?1u:0u) | (PXA_IME_BUILTIN_TRADITIONAL?2u:0u);
}
static const builtin_dictionary_t *dictionary(bool use_traditional) {
#if PXA_IME_BUILTIN_TRADITIONAL
    if (use_traditional) return &traditional;
#endif
#if PXA_IME_BUILTIN_SIMPLIFIED
    if (!use_traditional) return &simplified;
#endif
    (void)use_traditional; return NULL;
}
bool pxa_lvgl_ime_builtin_set_traditional(pxa_lvgl_ime_builtin_t *s,bool use_traditional) {
    const builtin_dictionary_t *table=dictionary(use_traditional);
    if (!s || !table) return false;
    s->dictionary=table; s->entries=table->count;
    pxa_lvgl_ime_builtin_reset(s); return true;
}
pxa_lvgl_ime_builtin_t *pxa_lvgl_ime_builtin_create(void *parent) {
    (void)parent;
    if (!pxa_lvgl_ime_builtin_scripts()) return NULL;
    pxa_lvgl_ime_builtin_t *s=calloc(1,sizeof(*s));
    if (!s) return NULL;
    if (!pxa_lvgl_ime_builtin_set_traditional(s,!PXA_IME_BUILTIN_SIMPLIFIED)) { free(s); return NULL; }
    return s;
}
void pxa_lvgl_ime_builtin_destroy(pxa_lvgl_ime_builtin_t *s) { free(s); }
void pxa_lvgl_ime_builtin_reset(pxa_lvgl_ime_builtin_t *s) {
    if (!s) return;
    s->input[0]=0; s->size=s->count=s->consumed=s->locked_size=0; s->page=s->selected=0;
}
static const char *spelling(const pxa_lvgl_ime_builtin_t *s,unsigned index) {
    return s->dictionary->rows[s->spellings[index]].spelling;
}
static const char *words(const pxa_lvgl_ime_builtin_t *s) {
    return s->count?s->dictionary->characters+s->dictionary->rows[s->spellings[s->selected]].offset:"";
}
static bool matches(const char *py,const char *input,size_t n,bool digits) {
    static const char keys[]="22233344455566677778889999";
    for (size_t i=0;i<n;++i) {
        if (!py[i] || py[i]<'a' || py[i]>'z') return false;
        if ((digits?keys[py[i]-'a']:py[i])!=input[i]) return false;
    }
    return true;
}
static void search(pxa_lvgl_ime_builtin_t *s,uint8_t layout) {
    s->count=0; s->page=0; s->selected=0; s->consumed=0;
    if (s->locked_size && s->size>=s->locked_size) {
        s->spellings[0]=s->locked; s->count=1; s->consumed=s->locked_size; return;
    }
    s->locked_size=0;
    size_t segment=0;
    while (segment<s->size && s->input[segment]!='\'') ++segment;
    if (!segment) return;
    size_t complete=0;
    for (unsigned i=0;i<s->entries;++i) {
        const builtin_syllable_t *row=&s->dictionary->rows[i];
        const char *py=row->spelling; size_t n=row->length;
        if (n<=segment && n>complete && matches(py,s->input,n,layout==PXA_IME_K9)) complete=n;
    }
    for (unsigned i=0;i<s->entries && s->count<BUILTIN_MAX_SPELLINGS;++i) {
        const builtin_syllable_t *row=&s->dictionary->rows[i];
        const char *py=row->spelling; size_t n=row->length;
        if (complete ? n!=complete : n<segment) continue;
        size_t length=complete?complete:segment;
        if (matches(py,s->input,length,layout==PXA_IME_K9)) s->spellings[s->count++]=(uint16_t)i;
    }
    s->consumed=(uint8_t)(complete?complete:segment);
    while (s->consumed<s->size && s->input[s->consumed]=='\'') ++s->consumed;
}
static size_t character_bytes(const char *text) {
    if (!*text) return 0;
    unsigned char c=(unsigned char)*text;
    size_t n=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
    for (size_t i=1;i<n;++i) if (!text[i] || ((unsigned char)text[i]&0xc0)!=0x80) return 1;
    return n;
}
static const char *candidate(const pxa_lvgl_ime_builtin_t *s,uint32_t index) {
    const char *text=words(s);
    while (*text && index--) text+=character_bytes(text);
    return text;
}
static void choose(pxa_lvgl_ime_builtin_t *s,uint8_t layout,uint32_t index,char *commit,size_t *used) {
    const char *word=candidate(s,index); size_t n=character_bytes(word);
    if (!n || !s->consumed || s->consumed>s->size) {
        memcpy(commit+*used,s->input,s->size); *used+=s->size;
        pxa_lvgl_ime_builtin_reset(s); return;
    }
    memcpy(commit+*used,word,n); *used+=n;
    s->size-=s->consumed;
    memmove(s->input,s->input+s->consumed,s->size+1);
    s->locked_size=0;
    search(s,layout);
}
static void item(char *buffer,size_t *size,const char *text,size_t n) {
    buffer[(*size)++]=(char)n; buffer[(*size)++]=(char)(n>>8);
    memcpy(buffer+*size,text,n); *size+=n;
}
pxa_status_t pxa_lvgl_ime_builtin_input(pxa_lvgl_ime_builtin_t *s,
    uint16_t action,uint8_t layout,uint32_t value,pxa_bytes_t text,
    pxa_ime_present_fn present,void *context) {
    if (!s || !present || text.size>7 || (text.size && !text.data)) return PXA_STATUS_INVALID_ARGUMENT;
    char commit[BUILTIN_MAX_INPUT*4+8]; size_t committed=0;
    pxa_ime_frame_t frame={.sequence=2};
    switch (action) {
    case PXA_IME_RESET: pxa_lvgl_ime_builtin_reset(s); break;
    case PXA_IME_TEXT: {
        if (layout==PXA_IME_K9 && !value && text.size==1 && text.data[0]=='\'') {
            if (s->count && s->consumed) {
                s->locked=s->spellings[s->selected]; s->locked_size=s->consumed;
                search(s,layout);
            }
            break;
        }
        bool composition=!value && text.size;
        for (size_t i=0;i<text.size;++i) composition&=layout==PXA_IME_K9?
            text.data[i]>='2' && text.data[i]<='9':
            (text.data[i]>='a' && text.data[i]<='z') || text.data[i]=='\'';
        if (composition) {
            if (s->size+text.size<=BUILTIN_MAX_INPUT) {
                memcpy(s->input+s->size,text.data,text.size); s->size+=(uint8_t)text.size;
                s->input[s->size]=0; search(s,layout);
            }
        } else {
            while (s->size) choose(s,layout,0,commit,&committed);
            if (text.size) memcpy(commit+committed,text.data,text.size);
            committed+=text.size;
        }
        break;
    }
    case PXA_IME_BACKSPACE:
        if (s->size) { s->input[--s->size]=0; search(s,layout); }
        else frame.edit=PXA_IME_EDIT_BACKSPACE;
        break;
    case PXA_IME_CONFIRM:
        if (s->size) choose(s,layout,0,commit,&committed);
        else if (value) frame.flags=PXA_IME_SUBMIT;
        else commit[committed++]=' ';
        break;
    case PXA_IME_CHOOSE:
        if (*candidate(s,value)) choose(s,layout,value,commit,&committed);
        break;
    case PXA_IME_CHOOSE_PINYIN:
        if (value<s->count) { s->selected=(uint16_t)value; s->page=0; }
        break;
    case PXA_IME_PAGE: s->page=value; break;
    default: return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (committed) { frame.edit=PXA_IME_EDIT_INSERT; frame.commit=(pxa_bytes_t){(const uint8_t *)commit,committed}; }
    char reading[BUILTIN_MAX_INPUT+9];
    /* PRESENT may synchronously delete the editor or make it a password,
     * releasing this backend. Every borrowed reply byte must be on this
     * stack, not in state that an editor observer can invalidate. */
    memcpy(reading,s->input,s->size+1);
    const char *preedit=reading;
    if (layout==PXA_IME_K9 && s->count) {
        const char *py=spelling(s,s->selected); size_t n=strlen(py);
        memcpy(reading,py,n);
        if (s->size>s->consumed) {
            reading[n++]='\'';
            memcpy(reading+n,s->input+s->consumed,s->size-s->consumed); n+=s->size-s->consumed;
        }
        reading[n]=0; preedit=reading;
    }
    frame.preedit=(pxa_bytes_t){(const uint8_t *)preedit,strlen(preedit)};
    char items[BUILTIN_MAX_CANDIDATES*6+BUILTIN_MAX_SPELLINGS*10]; size_t size=0;
    const char *word=candidate(s,s->page);
    while (*word && frame.candidate_count<BUILTIN_MAX_CANDIDATES) {
        size_t n=character_bytes(word); item(items,&size,word,n); word+=n; ++frame.candidate_count;
    }
    if (*word) frame.flags|=PXA_IME_MORE_CANDIDATES;
    for (unsigned i=0;i<s->count;++i) {
        const char *py=spelling(s,i); size_t n=strlen(py);
        if (n>7) break;
        item(items,&size,py,n); ++frame.pinyin_count;
    }
    frame.page_start=s->page; frame.items=(pxa_bytes_t){(const uint8_t *)items,size};
    return present(context,&frame);
}

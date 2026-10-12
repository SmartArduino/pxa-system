#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#include "pxa/posix/pxa_posix_ime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char root[]="/tmp/pxa-ime-preferences.XXXXXX",path[128],link[128],temporary[132];
    assert(mkdtemp(root)); snprintf(path,sizeof(path),"%s/preferences",root);
    snprintf(link,sizeof(link),"%s/link",root);
    pxa_ime_preferences_t value={255,255};
    assert(pxa_posix_ime_preferences_read(path,&value)==PXA_STATUS_OK);
    assert(value.layout==0 && value.language==0);
    for (unsigned layout=0;layout<3;++layout) for (unsigned language=0;language<2;++language) {
        value=(pxa_ime_preferences_t){layout,language};
        assert(pxa_posix_ime_preferences_write(path,value)==PXA_STATUS_OK);
        value=(pxa_ime_preferences_t){0};
        assert(pxa_posix_ime_preferences_read(path,&value)==PXA_STATUS_OK);
        assert(value.layout==layout && value.language==language);
    }
    assert(pxa_posix_ime_preferences_write(path,(pxa_ime_preferences_t){3,0})==PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_posix_ime_preferences_write(path,(pxa_ime_preferences_t){0,2})==PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_posix_ime_preferences_read(path,&value)==PXA_STATUS_OK && value.layout==2 && value.language==1);
    assert(symlink(path,link)==0);
    assert(pxa_posix_ime_preferences_read(link,&value)!=PXA_STATUS_OK);
    FILE *f=fopen(path,"ab"); assert(f); assert(fputc(0,f)!=EOF); assert(!fclose(f));
    assert(pxa_posix_ime_preferences_read(path,&value)==PXA_STATUS_INVALID_ARGUMENT);
    assert(value.layout==0 && value.language==0);
    f=fopen(path,"wb"); assert(f); assert(fputc(1,f)!=EOF); assert(!fclose(f));
    assert(pxa_posix_ime_preferences_read(path,&value)==PXA_STATUS_INVALID_ARGUMENT);
    // Provider selection shares preferences' atomic I/O and link rejection.
    char selected[131];
    assert(pxa_posix_ime_selection_write(path,"pinyin-ime")==PXA_STATUS_OK);
    assert(pxa_posix_ime_selection_read(path,selected,sizeof(selected))==PXA_STATUS_OK);
    assert(!strcmp(selected,"pinyin-ime"));
    assert(pxa_posix_ime_selection_read(link,selected,sizeof(selected))!=PXA_STATUS_OK && !*selected);
    snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    assert(symlink(path,temporary)==0);
    assert(pxa_posix_ime_selection_write(path,"other-ime")!=PXA_STATUS_OK);
    assert(pxa_posix_ime_selection_read(path,selected,sizeof(selected))==PXA_STATUS_OK);
    assert(!strcmp(selected,"pinyin-ime"));
    assert(pxa_posix_ime_selection_write(path,"../other")==PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_posix_ime_selection_write(path,"")==PXA_STATUS_OK);
    assert(pxa_posix_ime_selection_read(path,selected,sizeof(selected))==PXA_STATUS_OK && !*selected);
    assert(!unlink(link) && !unlink(path) && !rmdir(root));
    puts("IME preferences: defaults, all layouts/languages, invalid records and symlink rejection passed");
}

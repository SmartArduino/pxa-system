#ifndef PXA_GUEST_IMAGE_SET_H
#define PXA_GUEST_IMAGE_SET_H

#include "pxa_assets.h"

/* Explicit app-owned IMAGE handles, not a hidden pixel cache or scheduler.
 * Borrow paths and zero-initialized handle storage through stop + drain.
 * Select only the current page's images AFTER committing its new UI, then
 * pump. This retires old UI pins before a load can need their memory.
 * Feed the app's normal events to on_event; pump after handling the result.
 * One LOAD is outstanding. Errors never trigger an implicit retry loop.
 * Reserve a distinct full-width token prefix; never reset/reuse a live set.
 * Max 64 declared images; Host limits still bound simultaneous live handles. */
typedef struct {
    const char *const *paths;
    uint64_t *handles;
    uint64_t next_token, pending_token, wanted;
    int32_t error;
    size_t count;
    uint8_t pending_image, paused, cancelling, stopped;
} pxa_image_set_t;

#define PXA_IMAGE_SET_INIT(paths_, handles_, token_) \
    { .paths=(paths_), .handles=(handles_), .next_token=(token_), \
      .count=sizeof(paths_)/sizeof((paths_)[0]) }

static inline uint64_t pxa_image_set_handle(const pxa_image_set_t *set, unsigned id) {
    return set && id<set->count ? set->handles[id] : 0;
}
static inline void pxa_image_set_close_except(pxa_image_set_t *set,uint64_t keep) {
    for (unsigned i=0;i<set->count;++i) {
        if (!set->handles[i] || (keep & (UINT64_C(1)<<i))) continue;
        int32_t status=pxa_close_handle(set->handles[i]);
        if (!status || status==PXA_STATUS_NOT_FOUND) set->handles[i]=0;
        else if (!set->error) set->error=status;
    }
}
static inline void pxa_image_set_cancel_pending(pxa_image_set_t *set) {
    if (set->pending_token && !set->cancelling) {
        (void)pxa_cancel(set->pending_token);
        set->cancelling=1;
    }
}
static inline int32_t pxa_image_set_select(pxa_image_set_t *set,uint64_t wanted) {
    if (!set || !set->paths || !set->handles || !set->count || set->count>64 ||
        (set->count<64 && (wanted>>set->count))) return PXA_STATUS_INVALID_ARGUMENT;
    if (set->stopped) return PXA_STATUS_BAD_STATE;
    if (set->wanted!=wanted) {
        set->wanted=wanted;set->error=0;
        pxa_image_set_close_except(set,wanted);
        if (set->pending_token && !(wanted & (UINT64_C(1)<<set->pending_image)))
            pxa_image_set_cancel_pending(set);
    }
    return set->error;
}
static inline int pxa_image_set_ready(const pxa_image_set_t *set) {
    if (!set || set->error || set->stopped) return 0;
    for (unsigned i=0;i<set->count;++i)
        if ((set->wanted & (UINT64_C(1)<<i)) && !set->handles[i]) return 0;
    return 1;
}
static inline void pxa_image_set_pump(pxa_image_set_t *set) {
    if (!set || set->paused || set->stopped || set->pending_token || set->error) return;
    for (unsigned i=0;i<set->count;++i) {
        if (!(set->wanted & (UINT64_C(1)<<i)) || set->handles[i]) continue;
        if (set->next_token==UINT64_MAX) {set->error=PXA_STATUS_RESOURCE_LIMIT;return;}
        set->pending_image=(uint8_t)i;
        set->pending_token=++set->next_token;set->cancelling=0;
        set->error=pxa_assets_load_image(set->pending_token,set->paths[i]);
        if (set->error) set->pending_token=0;
        return;
    }
}
static inline void pxa_image_set_retry(pxa_image_set_t *set) {
    if (set && !set->stopped) set->error=0;
}
static inline void pxa_image_set_pause(pxa_image_set_t *set,int paused) {
    if (!set) return;
    set->paused=paused!=0;
    if (set->paused) pxa_image_set_cancel_pending(set);
}
static inline void pxa_image_set_stop(pxa_image_set_t *set) {
    if (!set) return;
    set->stopped=1;set->wanted=0;
    pxa_image_set_pause(set,1);
    pxa_image_set_close_except(set,0);
}
static inline int pxa_image_set_on_event(pxa_image_set_t *set,const pxa_event_t *event) {
    if (!set || !event || !set->pending_token || event->service!=PXA_ASSETS_SERVICE ||
        event->opcode!=PXA_ASSETS_LOAD || event->token!=set->pending_token) return 0;
    pxa_asset_result_t result;
    uint64_t token=set->pending_token;
    unsigned i=set->pending_image;
    set->pending_token=0;
    if (!pxa_assets_parse_result(event,token,PXA_ASSETS_LOAD,&result)) {
        set->error=PXA_STATUS_PROTOCOL_ERROR;
    } else if (!result.status) {
        set->handles[i]=result.handle;
        if (result.info.kind!=PXA_ASSET_IMAGE) {
            set->error=PXA_STATUS_PROTOCOL_ERROR;
            pxa_image_set_close_except(set,set->wanted & ~(UINT64_C(1)<<i));
        } else if (set->paused || set->stopped || !(set->wanted & (UINT64_C(1)<<i))) {
            pxa_image_set_close_except(set,set->wanted & ~(UINT64_C(1)<<i));
        }
    } else if (!set->cancelling && (set->wanted & (UINT64_C(1)<<i))) set->error=result.status;
    set->cancelling=0;
    return 1;
}
#endif

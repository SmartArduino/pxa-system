/* SPDX-License-Identifier: MIT
 * Browser-native Ogg playback; queue ownership and completion semantics stay
 * in the shared PXA audio service. No codec is linked into the system. */
#include <emscripten.h>
#include "services.h"
#include "pxa/audio.h"
static pxa_audio_playback_queue_t events;
static uint64_t owner,token;
EM_JS(int,music_available,(),{return !!Module.browserMusicAvailable;});
EM_JS(void,music_start,(uint64_t id,const uint8_t *path,size_t size,int loop,int db),{
    const name=UTF8ToString(path,size),binary=Module.FS.readFile('/package/'+name);
    Module.browserNotify?.({type:'music-start',id:Number(id),binary,loop:!!loop,gain:db});
});
EM_JS(void,music_control_js,(uint64_t id,int action,int db),{
    Module.browserNotify?.({type:'music-control',id:Number(id),action,gain:db});
});
pxa_status_t browser_music_play(void *c,uint64_t session,const pxa_audio_asset_t *asset,uint64_t *instance){
    (void)c;*instance=0;
    if(!music_available())return PXA_STATUS_UNSUPPORTED;
    pxa_asset_info_t info;pxa_status_t s=browser_asset_info((pxa_bytes_t){asset->path,asset->path_size},&info);
    if(s)return s;
    if(info.kind!=PXA_ASSET_AUDIO||(info.encoding!=PXA_ASSET_ENCODING_OGG_VORBIS&&info.encoding!=PXA_ASSET_ENCODING_OGG_OPUS))return PXA_STATUS_UNSUPPORTED;
    if(info.stored_bytes>16*1024*1024)return PXA_STATUS_RESOURCE_LIMIT;
    if(owner&&owner!=session)return PXA_STATUS_WOULD_BLOCK;
    s=pxa_audio_playback_begin(&events,session,instance);if(s)return s;
    pxa_audio_playback_finish(&events,token,PXA_AUDIO_PLAYBACK_REPLACED,0);
    owner=session;token=*instance;
    music_start(token,asset->path,asset->path_size,asset->flags&PXA_AUDIO_ASSET_LOOP,asset->gain_db_q8);return 0;
}
pxa_status_t browser_music_control(void *c,uint64_t session,const pxa_audio_asset_control_t *v){
    (void)c;if(owner!=session)return PXA_STATUS_OK;
    if(v->action==PXA_AUDIO_ASSET_STOP){
        pxa_audio_playback_finish(&events,token,PXA_AUDIO_PLAYBACK_STOPPED,0);
        music_control_js(token,v->action,v->gain_db_q8);owner=token=0;
    }else music_control_js(token,v->action,v->gain_db_q8);
    return PXA_STATUS_OK;
}
void browser_music_gain(uint64_t session,int16_t gain){if(owner==session)music_control_js(token,5,gain);}
void browser_music_close(uint64_t session){
    pxa_audio_asset_control_t stop={0,PXA_AUDIO_ASSET_STOP};browser_music_control(NULL,session,&stop);pxa_audio_playback_close(&events,session);
}
pxa_status_t browser_music_peek(void *c,pxa_audio_playback_event_t *e){(void)c;return pxa_audio_playback_peek(&events,e);}
pxa_status_t browser_music_consume(void *c,const pxa_audio_playback_event_t *e){(void)c;return pxa_audio_playback_consume(&events,e);}
EMSCRIPTEN_KEEPALIVE void browser_music_event(uint32_t id,int state,int status){
    if(token!=id||!owner)return;
    if(state==PXA_AUDIO_PLAYBACK_READY)pxa_audio_playback_ready(&events,token);
    else if(state==PXA_AUDIO_PLAYBACK_ENDED||state==PXA_AUDIO_PLAYBACK_ERROR){
        pxa_audio_playback_finish(&events,token,state,status);owner=token=0;
    }
}

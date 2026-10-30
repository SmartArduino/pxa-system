/* SPDX-License-Identifier: MIT */
#include <emscripten.h>
#include <stdlib.h>
#include <math.h>
#include "pxa/audio_mixer.h"
#include "pxa/audio_sound.h"
#include "services.h"

static pxa_audio_mixer_t mixer;
static pxa_audio_service_t *service;
static uint8_t active[PXA_AUDIO_MIXER_VOICES];
static int16_t graph_gain[PXA_AUDIO_MIXER_VOICES];
#define SOUND_SLOTS 12
static pxa_audio_sound_voice_t sounds[PXA_AUDIO_MIXER_VOICES][SOUND_SLOTS];
static uint8_t sound_phase;
static int32_t held_sound;
static int32_t gain(int16_t db){return (int32_t)(powf(10.f,(float)db/(256.f*20.f))*32768.f);}
static pxa_audio_mixer_voice_t *voice(uint64_t id) {
    return id && id<=PXA_AUDIO_MIXER_VOICES && active[id-1] ? &mixer.voices[id-1] : NULL;
}
static pxa_status_t open_pcm(void *context, const pxa_audio_format_t *format,
                            pxa_audio_pcm_info_t *info, uint64_t *id) {
    (void)context;
    for (unsigned i=0;i<PXA_AUDIO_MIXER_VOICES;i++) if (!active[i]) {
        pxa_audio_mixer_reset(&mixer.voices[i]);
        pxa_status_t status=pxa_audio_mixer_configure(&mixer.voices[i],format->sample_rate,format->channels,48000);
        if (status) return status;
        active[i]=1;graph_gain[i]=0;*id=i+1;
        *info=(pxa_audio_pcm_info_t){48000,format->sample_rate*80/1000,1};
        return PXA_STATUS_OK;
    }
    return PXA_STATUS_RESOURCE_LIMIT;
}
static pxa_status_t open_session(void *context,uint16_t usage,pxa_audio_format_t *format,uint64_t *id) {
    (void)usage;pxa_audio_pcm_info_t info;
    *format=(pxa_audio_format_t){16000,1,20};
    return open_pcm(context,format,&info,id);
}
static pxa_status_t commit(void *c,uint64_t id,const pxa_audio_graph_t *graph) {
    (void)c;pxa_status_t s=pxa_audio_mixer_commit(voice(id),graph);if(!s){graph_gain[id-1]=graph->gain_db_q8;browser_music_gain(id,graph->gain_db_q8);}return s;
}
static pxa_status_t submit(void *c,uint64_t id,const uint8_t *pcm,size_t bytes) {
    (void)c;return pxa_audio_mixer_write(voice(id),pcm,bytes);
}
static pxa_status_t tone(void *c,uint64_t id,const pxa_audio_tone_t *tone) {
    (void)c;return pxa_audio_mixer_tone(voice(id),tone);
}
static pxa_status_t effect(void *c,uint64_t id,const pxa_audio_tone_t *tones,size_t count,uint8_t track) {
    (void)c;return pxa_audio_mixer_effect(voice(id),tones,count,track);
}
static pxa_status_t stop_effect(void *c,uint64_t id,uint8_t track) {
    (void)c;return pxa_audio_mixer_stop_effect(voice(id),track);
}
static pxa_status_t query(void *c,uint64_t id,pxa_audio_state_t *out) {
    (void)c;pxa_audio_mixer_voice_t *v=voice(id);
    if (!v) return PXA_STATUS_NOT_FOUND;
    *out=v->state; return PXA_STATUS_OK;
}
static pxa_status_t flush(void *c,uint64_t id) {
    (void)c;pxa_audio_mixer_voice_t *v=voice(id);
    if (!v) return PXA_STATUS_NOT_FOUND;
    pxa_audio_asset_control_t stop={0,PXA_AUDIO_ASSET_STOP};browser_music_control(NULL,id,&stop);
    pxa_audio_mixer_flush(v); return PXA_STATUS_OK;
}
static void close_session(void *c,uint64_t id) {
    (void)c;pxa_audio_mixer_voice_t *v=voice(id);
    if (v) {browser_music_close(id);for(unsigned i=0;i<SOUND_SLOTS;i++)pxa_audio_sound_release(&sounds[id-1][i]);pxa_audio_mixer_reset(v);active[id-1]=0;}
}
static pxa_status_t play_sound_ex(void *c,uint64_t id,pxa_asset_object_t *asset,const pxa_audio_sound_options_t *options){
    (void)c;if(!voice(id))return PXA_STATUS_NOT_FOUND;
    if(!voice(id)->committed)return PXA_STATUS_BAD_STATE;
    unsigned slot=options->track;
    if(slot==PXA_AUDIO_SOUND_APPEND){for(slot=PXA_AUDIO_SOUND_TRACKS;slot<SOUND_SLOTS&&sounds[id-1][slot].asset;slot++);}
    if(slot>=SOUND_SLOTS)return PXA_STATUS_WOULD_BLOCK;
    return pxa_audio_sound_start(&sounds[id-1][slot],asset,id,options->track,options->loop,gain(options->gain_db_q8))?PXA_STATUS_OK:PXA_STATUS_UNSUPPORTED;
}
static pxa_status_t play_sound(void *c,uint64_t id,pxa_asset_object_t *asset,int16_t db){
    pxa_audio_sound_options_t options={.gain_db_q8=db,.track=PXA_AUDIO_SOUND_APPEND};
    return play_sound_ex(c,id,asset,&options);
}
static pxa_status_t control_sound(void *c,uint64_t id,const pxa_audio_sound_control_t *control){
    (void)c;if(!voice(id))return PXA_STATUS_NOT_FOUND;
    for(unsigned i=0;i<SOUND_SLOTS;i++)if(sounds[id-1][i].track==control->track)
        pxa_audio_sound_control(&sounds[id-1][i],control->action,gain(control->gain_db_q8));
    return PXA_STATUS_OK;
}
static pxa_status_t play_music(void *c,uint64_t id,const pxa_audio_asset_t *asset,uint64_t *instance){
    if(!voice(id))return PXA_STATUS_NOT_FOUND;
    if(!voice(id)->committed)return PXA_STATUS_BAD_STATE;
    pxa_status_t s=browser_music_play(c,id,asset,instance);if(!s)browser_music_gain(id,graph_gain[id-1]);return s;
}
static pxa_status_t play_asset(void *c,uint64_t id,const pxa_audio_asset_t *asset){uint64_t instance;return play_music(c,id,asset,&instance);}
static pxa_status_t control_asset(void *c,uint64_t id,const pxa_audio_asset_control_t *v){
    if(!voice(id))return PXA_STATUS_NOT_FOUND;
    for(unsigned i=0;i<SOUND_SLOTS;i++)pxa_audio_sound_control(&sounds[id-1][i],v->action,gain(v->gain_db_q8));
    return browser_music_control(c,id,v);
}
void browser_audio_poll(void){if(service)pxa_audio_service_poll(service);}
pxa_status_t browser_audio_init(pxa_runtime_t *runtime,pxa_permission_service_t *permissions) {
    pxa_audio_config_t config={.struct_size=sizeof(config),.max_sessions=3,.max_sessions_per_component=3,
        .max_eq_bands=PXA_AUDIO_MAX_EQ_BANDS,.permissions=permissions,
        .backend={.struct_size=sizeof(pxa_audio_backend_t),.open=open_session,.open_pcm=open_pcm,
            .commit=commit,.submit=submit,.close=close_session,.query=query,.flush=flush,
            .play_asset=play_asset,.control_asset=control_asset,
            .play_music=play_music,.control_music=browser_music_control,
            .playback_peek=browser_music_peek,.playback_consume=browser_music_consume,
            .play_sound=play_sound,.play_sound_ex=play_sound_ex,.control_sound=control_sound,
            .open_capture=browser_capture_open,.capture_io=browser_capture_io,.close_capture=browser_capture_close,
            .play_tone=tone,.play_effect=effect,.stop_effect=stop_effect}};
    size_t size=pxa_audio_service_workspace_size(&config);
    pxa_status_t status=pxa_audio_service_init(calloc(1,size),size,runtime,&config,&service);
    return status ? status : pxa_audio_service_register(service);
}
EMSCRIPTEN_KEEPALIVE int16_t *browser_audio_render(uint32_t samples) {
    static int16_t output[960];
    if (samples>960) return NULL;
    pxa_audio_mixer_render(&mixer,output,samples);
    for(unsigned i=0;i<samples;i++){
        if(!sound_phase){held_sound=0;
            for(unsigned v=0;v<PXA_AUDIO_MIXER_VOICES;v++)if(active[v])
                for(unsigned s=0;s<SOUND_SLOTS;s++)held_sound+=(int32_t)(pxa_audio_sound_render(&sounds[v][s])*mixer.voices[v].gain);
        }
        output[i]=pxa_audio_output_limit((int32_t)output[i]+held_sound);sound_phase=(sound_phase+1)%3;
    }
    return output;
}

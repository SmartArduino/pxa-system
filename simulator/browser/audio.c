/* SPDX-License-Identifier: MIT */
#include <emscripten.h>
#include <stdlib.h>
#include "pxa/audio_mixer.h"
#include "services.h"

static pxa_audio_mixer_t mixer;
static uint8_t active[PXA_AUDIO_MIXER_VOICES];
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
        active[i]=1;*id=i+1;
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
    (void)c;return pxa_audio_mixer_commit(voice(id),graph);
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
    pxa_audio_mixer_flush(v); return PXA_STATUS_OK;
}
static void close_session(void *c,uint64_t id) {
    (void)c;pxa_audio_mixer_voice_t *v=voice(id);
    if (v) {pxa_audio_mixer_reset(v);active[id-1]=0;}
}
pxa_status_t browser_audio_init(pxa_runtime_t *runtime,pxa_permission_service_t *permissions) {
    pxa_audio_config_t config={.struct_size=sizeof(config),.max_sessions=3,.max_sessions_per_component=3,
        .max_eq_bands=PXA_AUDIO_MAX_EQ_BANDS,.permissions=permissions,
        .backend={.struct_size=sizeof(pxa_audio_backend_t),.open=open_session,.open_pcm=open_pcm,
            .commit=commit,.submit=submit,.close=close_session,.query=query,.flush=flush,
            .play_tone=tone,.play_effect=effect,.stop_effect=stop_effect}};
    size_t size=pxa_audio_service_workspace_size(&config);
    pxa_audio_service_t *service;
    pxa_status_t status=pxa_audio_service_init(calloc(1,size),size,runtime,&config,&service);
    return status ? status : pxa_audio_service_register(service);
}
EMSCRIPTEN_KEEPALIVE int16_t *browser_audio_render(uint32_t samples) {
    static int16_t output[960];
    if (samples>960) return NULL;
    pxa_audio_mixer_render(&mixer,output,samples);return output;
}

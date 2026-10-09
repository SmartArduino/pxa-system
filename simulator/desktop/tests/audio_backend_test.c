/* Exercise the actual SDL backend with its device paused; drive callback
 * deterministically and let only the file decoder run on a worker. */
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include <assert.h>
#include <unistd.h>
static void expect_music_event(product_host_t *h,uint64_t instance,uint8_t state,pxa_status_t status) {
    pxa_audio_playback_event_t event;
    for (unsigned i=0;i<2000;++i) {
        if (!audio_playback_peek(h,&event)) {
            assert(event.instance==instance && event.state==state && event.status==status);
            assert(!audio_playback_consume(h,&event)); return;
        }
        SDL_Delay(1);
    }
    assert(!"missing music event");
}
static void expect_terminal(product_host_t *h,uint64_t instance,uint8_t state) {
    pxa_audio_playback_event_t event;
    if (!audio_playback_peek(h,&event) && event.state==PXA_AUDIO_PLAYBACK_READY)
        expect_music_event(h,instance,PXA_AUDIO_PLAYBACK_READY,0);
    expect_music_event(h,instance,state,0);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    product_host_t *h = calloc(1, sizeof(*h));
    assert(h);
    assert(resource_memory_init(h, 128 * 1024, 2 * 1024 * 1024, 16 * 1024, 512 * 1024) == 0);
    snprintf(h->package_root, sizeof(h->package_root), "%s", argv[1]);
    // Python built PXRI 1.2 from temporary copies of the original songs. This
    // fixture trusts its manifest; signed AOT tests cover package authority.
    const char *names[]={"assets/bad.ogg","assets/resources.pxi","assets/tone-opus.ogg","assets/tone.ogg"};
    pxa_package_file_t files[4]; uint8_t digests[4][32];
    for(unsigned i=0;i<4;++i) {
        char path[1024]; snprintf(path,sizeof(path),"%s/%s",argv[1],names[i]);
        FILE *file=fopen(path,"rb"); assert(file);
        assert(!fseek(file,0,SEEK_END)); long bytes=ftell(file); assert(bytes>0 && !fseek(file,0,SEEK_SET));
        uint8_t *data=malloc((size_t)bytes); assert(data);
        assert(fread(data,1,(size_t)bytes,file)==(size_t)bytes); fclose(file);
        assert(!pxa_openssl_sha256(data,(size_t)bytes,digests[i])); free(data);
        files[i]=(pxa_package_file_t){{(const uint8_t*)names[i],strlen(names[i])},(uint64_t)bytes,digests[i]};
    }
    static const uint8_t fixture[]="authenticated audio fixture";
    pxa_package_manifest_t manifest={0}; manifest.files=files; manifest.file_count=4;
    manifest.encoded=(pxa_bytes_t){fixture,sizeof(fixture)-1};
    pxa_posix_asset_worker_config_t wc={0}; wc.package_root=argv[1]; wc.manifest=&manifest;
    wc.cache=(pxa_asset_cache_config_t){4,8,4,4,{65536,65536},{65536,65536}};
    wc.max_catalog_bytes=65536; wc.metadata_allocator=&h->resource_allocators[1][PXA_MEMORY_METADATA];
    wc.temporary_allocator=&h->resource_allocators[1][PXA_MEMORY_TEMPORARY];
    wc.allocator_context=h; wc.allocate=asset_allocate; wc.release=raster_asset_free;
    assert(!pxa_posix_asset_worker_create(&wc,&h->asset_worker));
    assert(!pxa_posix_asset_worker_stack_bytes(h->asset_worker));
    uint64_t a, b;
    pxa_audio_format_t f;
    pxa_audio_graph_t g = {.route = PXA_AUDIO_ROUTE_SPEAKER};
    uint8_t pcm[640];
    int16_t output[320];
    pxa_audio_state_t state;
    assert(audio_open(h, 1, &f, &a) == PXA_STATUS_OK);
    assert(audio_open(h, 1, &f, &b) == PXA_STATUS_OK && a != b);
    assert(audio_commit(h, a, &g) == PXA_STATUS_OK);
    assert(audio_commit(h, b, &g) == PXA_STATUS_OK);
    for (unsigned i=0; i<320; ++i) { pcm[i*2]=0xe8; pcm[i*2+1]=3; }
    assert(audio_submit(h, a, pcm, sizeof(pcm)) == PXA_STATUS_OK);
    assert(audio_submit(h, b, pcm, sizeof(pcm)) == PXA_STATUS_OK);
    audio_callback(h, (uint8_t*)output, sizeof(output));
    assert(output[80] == 2000);
    assert(audio_query(h, a, &state) == PXA_STATUS_OK && state.accepted_samples == 320);
    for (unsigned i=0; i<4; ++i) assert(audio_submit(h,a,pcm,sizeof(pcm)) == PXA_STATUS_OK);
    assert(audio_submit(h,a,pcm,sizeof(pcm)) == PXA_STATUS_WOULD_BLOCK);
    assert(audio_flush(h,a) == PXA_STATUS_OK);
    pxa_audio_tone_t t = {.frequency_hz=440, .duration_ms=1000, .gain_db_q8=-6*256};
    assert(audio_play_tone(h,b,&t) == PXA_STATUS_OK);
    audio_close(h,a);
    for (unsigned i=0; i<50; ++i) audio_callback(h,(uint8_t*)output,sizeof(output));
    assert(audio_query(h,b,&state) == PXA_STATUS_OK && state.accepted_samples == 16320);
    pxa_asset_info_t info={0}; info.kind=PXA_ASSET_AUDIO;
    info.encoding=PXA_ASSET_ENCODING_PCM_U8_16K_MONO; info.stored_bytes=info.decoded_bytes=160;
    pxa_asset_object_t *sound; uint8_t *sound_data;
    assert(!pxa_asset_object_create(&info,raster_asset_allocate,raster_asset_free,
        &h->resource_allocators[1][PXA_MEMORY_AUDIO],&sound,&sound_data));
    memset(sound_data,255,160); pxa_asset_object_finish_loading(sound);
    for(unsigned i=0;i<PRODUCT_AUDIO_SOUND_VOICES;++i) assert(!audio_play_sound(h,b,sound,0));
    assert(audio_play_sound(h,b,sound,0)==PXA_STATUS_WOULD_BLOCK);
    assert(pxa_asset_object_reference_count(sound)==1+PRODUCT_AUDIO_SOUND_VOICES);
    audio_callback(h,(uint8_t*)output,sizeof(output));
    assert(output[80] > 0 && pxa_asset_object_reference_count(sound)==1);
    assert(!audio_play_sound(h,b,sound,0));
    pxa_audio_asset_control_t stop_sound={0,PXA_AUDIO_ASSET_STOP};
    assert(!audio_control_asset(h,b,&stop_sound) && pxa_asset_object_reference_count(sound)==1);
    pxa_asset_object_release(sound);
    // Resident signed PCM and independent tracks use the actual SDL callback.
    info.encoding=PXA_ASSET_ENCODING_PCM_S16LE_16K_MONO;
    info.stored_bytes=info.decoded_bytes=2000;
    assert(!pxa_asset_object_create(&info,raster_asset_allocate,raster_asset_free,
        &h->resource_allocators[1][PXA_MEMORY_AUDIO],&sound,&sound_data));
    for(unsigned i=0;i<1000;++i) {sound_data[2*i]=0xe8;sound_data[2*i+1]=3;}
    pxa_asset_object_finish_loading(sound);
    pxa_audio_sound_options_t tracked={0,1,1};
    assert(!audio_play_sound_ex(h,b,sound,&tracked));
    for(unsigned i=0;i<12;++i) audio_callback(h,(uint8_t*)output,sizeof(output));
    assert(output[80]==1000 && pxa_asset_object_reference_count(sound)==2);
    pxa_audio_asset_control_t music_gain={-20*256,PXA_AUDIO_ASSET_SET_GAIN};
    assert(!audio_control_music(h,b,&music_gain));
    audio_callback(h,(uint8_t*)output,sizeof(output)); assert(output[80]==1000);
    assert(!audio_play_sound_ex(h,b,sound,&tracked));
    assert(pxa_asset_object_reference_count(sound)==2);
    pxa_audio_sound_control_t track_stop={0,1,PXA_AUDIO_ASSET_STOP};
    assert(!audio_control_sound(h,b,&track_stop));
    audio_callback(h,(uint8_t*)output,sizeof(output));
    assert(pxa_asset_object_reference_count(sound)==1 && output[80]==0);
    pxa_asset_object_release(sound);
    /* Ogg streams into 8192 samples regardless of track length. */
    static const uint8_t path[]="assets/tone.ogg";
    pxa_audio_asset_t asset={path,sizeof(path)-1,0,0};
    uint64_t first,second,third;
    assert(audio_play_music(h,b,&asset,&first) == PXA_STATUS_OK);
    unsigned consumed=0;
    for (unsigned iteration=0; iteration<2000; ++iteration) {
        SDL_LockAudioDevice(h->audio_device);
        unsigned before=h->music_count;
        assert(before <= 8192);
        audio_callback(h,(uint8_t*)output,sizeof(output));
        consumed += before - h->music_count;
        int finished=h->music_finished && !h->music_count;
        SDL_UnlockAudioDevice(h->audio_device);
        if (finished) break;
        SDL_Delay(2);
    }
    assert(consumed == 16000); /* Includes the buffered tail at EOF. */
    SDL_LockAudioDevice(h->audio_device); audio_callback(h,(uint8_t*)output,sizeof(output)); SDL_UnlockAudioDevice(h->audio_device);
    expect_music_event(h,first,PXA_AUDIO_PLAYBACK_READY,0);
    expect_music_event(h,first,PXA_AUDIO_PLAYBACK_ENDED,0);
    asset.path = (const uint8_t *)"assets/tone-opus.ogg";
    asset.path_size = strlen((const char *)asset.path);
    assert(audio_play_music(h,b,&asset,&second) == PXA_STATUS_OK && second>first);
    consumed = 0;
    for (unsigned iteration=0; iteration<2000; ++iteration) {
        SDL_LockAudioDevice(h->audio_device);
        unsigned before=h->music_count;
        assert(before <= 8192);
        audio_callback(h,(uint8_t*)output,sizeof(output));
        consumed += before - h->music_count;
        int finished=h->music_finished && !h->music_count;
        SDL_UnlockAudioDevice(h->audio_device);
        if (finished) break;
        SDL_Delay(2);
    }
    assert(consumed == 16000); /* Includes the buffered tail at EOF. */
    SDL_LockAudioDevice(h->audio_device); audio_callback(h,(uint8_t*)output,sizeof(output)); SDL_UnlockAudioDevice(h->audio_device);
    expect_music_event(h,second,PXA_AUDIO_PLAYBACK_READY,0);
    expect_music_event(h,second,PXA_AUDIO_PLAYBACK_ENDED,0);
    assert(audio_open(h,1,&f,&a) == PXA_STATUS_OK);
    assert(audio_commit(h,a,&g) == PXA_STATUS_OK);
    asset.flags=PXA_AUDIO_ASSET_LOOP;
    assert(audio_play_music(h,b,&asset,&third) == PXA_STATUS_OK && third>second);
    assert(audio_play_asset(h,a,&asset) == PXA_STATUS_WOULD_BLOCK);
    pxa_audio_asset_control_t pause={0,PXA_AUDIO_ASSET_PAUSE};
    assert(audio_control_asset(h,b,&pause) == PXA_STATUS_OK);
    SDL_Delay(10);
    SDL_LockAudioDevice(h->audio_device);
    unsigned before=h->music_count;
    audio_callback(h,(uint8_t*)output,sizeof(output));
    assert(h->music_count == before);
    SDL_UnlockAudioDevice(h->audio_device);
    pxa_audio_asset_control_t stop={0,PXA_AUDIO_ASSET_STOP};
    assert(!audio_control_asset(h,b,&stop));
    expect_terminal(h,third,PXA_AUDIO_PLAYBACK_STOPPED);
    uint64_t ids[PXA_AUDIO_PLAYBACK_CAPACITY],rejected;
    for (unsigned i=0;i<PXA_AUDIO_PLAYBACK_CAPACITY;++i)
        assert(!audio_play_music(h,b,&asset,&ids[i]));
    uint64_t active=h->music_token;
    assert(audio_play_music(h,b,&asset,&rejected)==PXA_STATUS_WOULD_BLOCK && !rejected);
    assert(h->music_token==active);
    for (unsigned i=0;i<PXA_AUDIO_PLAYBACK_CAPACITY-1;++i)
        expect_terminal(h,ids[i],PXA_AUDIO_PLAYBACK_REPLACED);
    assert(!audio_control_asset(h,b,&stop));
    expect_terminal(h,ids[PXA_AUDIO_PLAYBACK_CAPACITY-1],PXA_AUDIO_PLAYBACK_STOPPED);

    asset.path=(const uint8_t*)"assets/bad.ogg"; asset.path_size=14; asset.flags=0;
    assert(!audio_play_music(h,b,&asset,&first));
    expect_music_event(h,first,PXA_AUDIO_PLAYBACK_ERROR,PXA_STATUS_PROTOCOL_ERROR);
    // Direct music input no longer requires a private verification buffer.
    char bad_path[1024]; snprintf(bad_path,sizeof(bad_path),"%s/assets/tone.ogg",argv[1]);
    asset.path=(const uint8_t*)"assets/tone.ogg"; asset.path_size=15;
    assert(!unlink(bad_path));
    assert(!audio_play_music(h,b,&asset,&first));
    expect_music_event(h,first,PXA_AUDIO_PLAYBACK_ERROR,PXA_STATUS_NOT_FOUND);
    audio_close(h,a); assert(h->audio_device);
    audio_close(h,b); assert(!h->audio_device && !h->music_thread);
    assert(h->music_read_bytes && h->music_read_calls);
    assert(!pxa_posix_asset_worker_stack_bytes(h->asset_worker));
    while(pxa_posix_asset_worker_destroy(h->asset_worker)==PXA_STATUS_WOULD_BLOCK) SDL_Delay(1);
    h->asset_worker=NULL;
    assert(resource_memory_end(h) == 0);
    free(h); SDL_Quit();
    puts("SDL audio: installed Vorbis/Opus, READY/ENDED, replacement, backpressure, format/missing errors, PCM and pause passed");
    return 0;
}

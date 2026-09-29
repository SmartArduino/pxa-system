/* Real backend allocation paths contend for one small quota. */
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include "../../../libpxa/tests/raster_snapshot_scenario.h"
#include <unistd.h>

int main(void) {
    product_host_t *h = calloc(1, sizeof(*h));
    char root[] = "/tmp/pxa-shared-budget-XXXXXX", image_path[256];
    assert(h && mkdtemp(root));
    assert(resource_memory_init(h, 1024, 512, 128, 256) == PXA_STATUS_OK);
    void *temporary = resource_allocate(h, PXA_MEMORY_EXTERNAL, PXA_MEMORY_TEMPORARY,
        256 - (pxa_memory_allocation_bytes(1) - 1));
    assert(temporary);
    assert(!resource_allocate(h, PXA_MEMORY_EXTERNAL, PXA_MEMORY_TEMPORARY, 1));
    void *resident = resource_allocate(h, PXA_MEMORY_EXTERNAL, PXA_MEMORY_RASTER, 32);
    assert(resident);
    pxa_memory_release(resident); pxa_memory_release(temporary);
    SDL_setenv("PXA_RESOURCE_TEMPORARY_EXTERNAL_BYTES", "0", 1);
    assert(asset_setting("PXA_RESOURCE_TEMPORARY_EXTERNAL_BYTES", 512, 1024) == 0);
    snprintf(h->package_root, sizeof(h->package_root), "%s", root);
    snprintf(image_path, sizeof(image_path), "%s/image.png", root);
    uint8_t png[24] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 1, 0, 0, 0, 1};
    FILE *f = fopen(image_path, "wb"); assert(f);
    assert(fwrite(png, 1, sizeof(png), f) == sizeof(png)); fclose(f);
    lv_init(); h->display = lv_display_create(4, 4); assert(h->display);
    h->width = h->height = 4; h->content_parent = lv_display_get_screen_active(h->display);
    uint64_t surface, session; uint32_t caps; pxa_audio_format_t format;
    pxa_game_render_desc_t desc = {4, 4, 3, 0, PXA_GAME_RENDER_SCRATCH_NONE, 4096};
    assert(game_render_create(h, &desc, &surface, &caps) == PXA_STATUS_OK);
    uint8_t upload[PXA_RASTER_UPLOAD_HEADER_BYTES + 256] = {0};
    snapshot_upload_header(upload, PXA_RASTER_UPLOAD_TEXTURE_INDEX8, 16, 16);
    memset(upload + PXA_RASTER_UPLOAD_HEADER_BYTES, 0x73, 256);
    assert(surface_raster_upload(h, surface, upload, sizeof(upload)) == PXA_STATUS_OK);
    /* The worker's actual allocator shares the quota with dynamic uploads. */
    void *file_object = asset_allocate(h, 64, PXA_ASSET_MEMORY_EXTERNAL, PXA_ASSET_TEXTURE);
    assert(file_object);
    const void *img = resolve_asset((const uint8_t *)"image.png", 9, h);
    assert(img);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(audio_open(h, 1, &format, &session) == PXA_STATUS_OK);
    pxa_asset_info_t sound_info={0}; sound_info.kind=PXA_ASSET_AUDIO;
    sound_info.encoding=PXA_ASSET_ENCODING_PCM_U8_16K_MONO;
    sound_info.stored_bytes=sound_info.decoded_bytes=160;
    pxa_asset_object_t *sound=NULL; uint8_t *sound_data=NULL;
    assert(pxa_asset_object_create(&sound_info,raster_asset_allocate,raster_asset_free,
        &h->resource_allocators[1][PXA_MEMORY_AUDIO],&sound,&sound_data)==PXA_STATUS_RESOURCE_LIMIT);
    assert(!sound && !sound_data);
    release_asset(img, h); raster_asset_free(h, file_object);
    assert(!pxa_asset_object_create(&sound_info,raster_asset_allocate,raster_asset_free,
        &h->resource_allocators[1][PXA_MEMORY_AUDIO],&sound,&sound_data));
    memset(sound_data,128,160); pxa_asset_object_finish_loading(sound);
    assert(audio_play_sound(h,session,sound,0)==PXA_STATUS_BAD_STATE);
    pxa_audio_graph_t graph = {.route = PXA_AUDIO_ROUTE_SPEAKER};
    assert(!audio_commit(h,session,&graph));
    assert(!audio_play_sound(h,session,sound,0));
    pxa_raster_asset_t *old = h->raster_bindings.textures[0];
    upload[PXA_RASTER_UPLOAD_HEADER_BYTES] = 0x24;
    assert(surface_raster_upload(h, surface, upload, sizeof(upload)) == PXA_STATUS_RESOURCE_LIMIT);
    assert(h->raster_bindings.textures[0] == old);
    pxa_raster_resources_t view;
    pxa_raster_bindings_view(&h->raster_bindings, caps, &view);
    assert(view.textures[0].pixels[0] == 0x73); /* rejected replacement preserves old resource */
    pxa_memory_stats_t stats;
    assert(pxa_memory_budget_stats(&h->resource_budget, 0, &stats) == PXA_STATUS_OK);
    assert(stats.charged[1] <= 512 && stats.denied >= 2 && !stats.reserved[1]);
    assert(stats.by_kind[PXA_MEMORY_RASTER][1] && stats.by_kind[PXA_MEMORY_AUDIO][1]);
    assert(stats.temporary_peak[1] == 256 && !stats.by_kind[PXA_MEMORY_TEMPORARY][1]);
    assert(resource_memory_end(h) == PXA_STATUS_WOULD_BLOCK);
    assert(surface_raster_upload(h, surface, upload, sizeof(upload)) == PXA_STATUS_RESOURCE_LIMIT);
    audio_close(h, session);
    assert(pxa_asset_object_reference_count(sound)==1); pxa_asset_object_release(sound);
    surface_close(h, surface);
    assert(resource_memory_end(h) == PXA_STATUS_OK);
    lv_display_delete(h->display); lv_deinit(); free(h); SDL_Quit();
    assert(!unlink(image_path) && !rmdir(root));
    puts("shared budget: actual file/dynamic/image/audio paths, cross-category pressure, retained replacement, close and zero leaked charges passed");
    return 0;
}

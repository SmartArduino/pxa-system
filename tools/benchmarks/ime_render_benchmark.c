/* Opt-in Linux/POSIX LVGL microbenchmark; never linked into firmware.
 * Include the selected adapter implementation to compare two source snapshots
 * against exactly the same libraries, without changing the public IME API. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "lvgl.h"
static unsigned map_calls;
static void counted_map(lv_obj_t *obj,const char *const *map) {
    ++map_calls; lv_buttonmatrix_set_map(obj,map);
}
#define lv_buttonmatrix_set_map counted_map
#include IME_SOURCE
#ifdef IME_BENCHMARK_MEMORY
#include "ime_alloc_trace.h"
#endif
#undef lv_buttonmatrix_set_map
static uint64_t flushed_pixels,flush_calls;
static pxa_status_t input(void *ctx,uint64_t generation,uint16_t action,uint8_t layout,uint8_t language,uint32_t value,pxa_bytes_t text) {
    (void)ctx;(void)generation;(void)action;(void)layout;(void)language;(void)value;(void)text; return PXA_STATUS_OK;
}
static void focus(void *ctx,uint64_t generation,bool active,bool provider) {
    (void)ctx;(void)generation;(void)active;(void)provider;
}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels) {
    (void)pixels; flushed_pixels+=(uint64_t)(area->x2-area->x1+1)*(area->y2-area->y1+1); ++flush_calls; lv_display_flush_ready(display);
}

static uint64_t ns(clockid_t clock_id) {
    struct timespec now; assert(!clock_gettime(clock_id,&now));
    return (uint64_t)now.tv_sec*1000000000+now.tv_nsec;
}
int main(int argc,char **argv) {
    assert(argc==4); bool circle=atoi(argv[1]); int workload=atoi(argv[2]);
    unsigned n=(unsigned)strtoul(argv[3],NULL,10); assert(n && workload>=0 && workload<=4);
    lv_init();
    lv_display_t *display=lv_display_create(480,480);
    static uint8_t pixels[480*10*4];
    lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    lv_obj_t *text=lv_textarea_create(lv_screen_active());
    lv_obj_set_floating(text,true); lv_obj_set_size(text,400,40); lv_obj_set_pos(text,20,430);
    lv_textarea_set_one_line(text,true); lv_textarea_set_text(text,"abcdefghi");
    pxa_lvgl_ime_config_t config={.struct_size=sizeof(config),.input=input,.focus=focus,
        .preferences={PXA_IME_LAYOUT_FULL,PXA_IME_CHINESE},
        .environment={.width=480,.height=480,.density_q16=65536,.font_scale_q16=65536,.display_shape=circle?2:0}};
    // Make the keyboard opaque with distinct colors, like a real system theme.
    config.theme.rgba[PXA_UI_THEME_TEXT]=0xe3e2e8ff;
    config.theme.rgba[PXA_UI_THEME_SURFACE_CONTAINER]=0x202024ff;
    config.theme.rgba[PXA_UI_THEME_SURFACE_CONTAINER_HIGHEST]=0x343438ff;
    config.theme.rgba[PXA_UI_THEME_PRIMARY_CONTAINER]=0x505078ff;
    config.theme.rgba[PXA_UI_THEME_ON_PRIMARY_CONTAINER]=0xffffffff;
    pxa_lvgl_ime_t *ime=pxa_lvgl_ime_create(&config); assert(ime);
    assert(!pxa_lvgl_ime_focus(ime,text)); assert(pxa_lvgl_ime_editor_view(ime));
    static const uint8_t items[]={1,0,'a'},preedit[]={'n','i'};
    pxa_ime_frame_t frame={.sequence=1};
    if (workload==0) {
        frame.items=(pxa_bytes_t){items,sizeof(items)}; frame.candidate_count=1;
        frame.preedit=(pxa_bytes_t){preedit,sizeof(preedit)};
    }
    assert(!pxa_lvgl_ime_present(ime,pxa_lvgl_ime_generation(ime),&frame));
    lv_refr_now(display);
    // Prove same-position UTF-8 replacement and clearing still refresh the aid.
    lv_textarea_set_text(text,"中文xyz"); lv_textarea_set_cursor_pos(text,1); refresh_editor(ime,false);
    assert(strstr(lv_label_get_text(ime->editor_text),"中文xyz"));
    lv_textarea_set_text(text,"正文xyz"); lv_textarea_set_cursor_pos(text,1); refresh_editor(ime,false);
    assert(strstr(lv_label_get_text(ime->editor_text),"正文xyz"));
    lv_textarea_set_text(text,""); refresh_editor(ime,false); assert(!*lv_label_get_text(ime->editor_text));
    lv_textarea_set_text(text,"abcdefghi"); lv_textarea_set_cursor_pos(text,4); refresh_editor(ime,false);
    lv_refr_now(display); map_calls=0; flushed_pixels=flush_calls=0;
#ifdef IME_BENCHMARK_MEMORY
    pthread_mutex_lock(&allocation_mutex);
    size_t stable=live_bytes,initial_peak=peak_bytes;
    peak_bytes=live_bytes; alloc_calls=0;
    pthread_mutex_unlock(&allocation_mutex);
#endif
    uint64_t start=ns(CLOCK_MONOTONIC),cpu_start=ns(CLOCK_PROCESS_CPUTIME_ID);
    for (unsigned i=0;i<n;++i) {
        if (workload<=1) {
            frame.sequence=i+2;
            assert(!pxa_lvgl_ime_present(ime,pxa_lvgl_ime_generation(ime),&frame));
        } else if (workload==2) {
            lv_textarea_set_cursor_pos(text,4+(i&1)); refresh_editor(ime,false);
        } else if (workload==3) lv_obj_invalidate(ime->root);
        else {
            lv_buttonmatrix_set_selected_button(ime->editor_navigation,i&1);
            lv_obj_set_state(ime->editor_navigation,LV_STATE_PRESSED,(i&1)==0);
        }
        // Run layout, invalidation processing and software rasterization.
        // Keep ticks fixed: no blink/animation scheduling or LCD/DMA latency.
        lv_refr_now(display);
    }
    uint64_t elapsed=ns(CLOCK_MONOTONIC)-start,cpu_elapsed=ns(CLOCK_PROCESS_CPUTIME_ID)-cpu_start;
    printf("{\"circle\":%d,\"workload\":%d,\"iterations\":%u,\"us_per_iteration\":%.3f,\"cpu_us_per_iteration\":%.3f,\"flushed_pixels\":%llu,\"flush_calls\":%llu,\"map_calls\":%u",
        circle,workload,n,(double)elapsed/n/1000,(double)cpu_elapsed/n/1000,
        (unsigned long long)flushed_pixels,(unsigned long long)flush_calls,map_calls);
#ifdef IME_BENCHMARK_MEMORY
    pthread_mutex_lock(&allocation_mutex);
    size_t run_peak=peak_bytes,run_end=live_bytes,run_allocs=alloc_calls;
    pthread_mutex_unlock(&allocation_mutex);
    printf(",\"initialization_peak\":%zu,\"stable\":%zu,\"run_peak\":%zu,\"end\":%zu,\"allocation_calls\":%zu",
        initial_peak,stable,run_peak,run_end,run_allocs);
#endif
    puts("}");
    pxa_lvgl_ime_destroy(ime); lv_display_delete(display); lv_deinit();
}

/* Capture a real signed UI application's static opening screen and memory
 * counters before/after migration. This does not measure gameplay or FPS. */
#undef NDEBUG
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static unsigned ready_home, legacy_lookups, ready_page[6];
static void observe_log(uint32_t component, unsigned level, const uint8_t *data, size_t size) {
    (void)component; (void)level;
    const char *ready="plane-shooter: ready home";
    if (size==strlen(ready) && !memcmp(data,ready,size)) ++ready_home;
    static const char *const pages[]={"home","hangar","shop","briefing","results","battle"};
    const size_t prefix=sizeof("plane-shooter: ready ")-1;
    for (unsigned i=0;i<6;++i)
        if (size==prefix+strlen(pages[i]) && !memcmp(data,"plane-shooter: ready ",prefix) &&
            !memcmp(data+prefix,pages[i],size-prefix)) ++ready_page[i];
}
static void observe_asset(const uint8_t *path, size_t size) {
    (void)path; (void)size; ++legacy_lookups;
}
#define PXSYS_PRODUCT_LOG_OBSERVER observe_log
#define PXSYS_PRODUCT_ASSET_LOOKUP_OBSERVER observe_asset
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#define PXSYS_CLOCK_NOW_US() UINT64_C(0x5eed1234)
#include "../product_runner.c"
#include <assert.h>

typedef struct {
    product_host_t *host;
    const char *directory;
    uint64_t started;
    unsigned captured;
    unsigned wait_ready;
    unsigned walkthrough, step, background_loading, background_state, exit_loading;
    uint64_t step_time;
} capture_t;

static void save(capture_t *test, const char *name, const void *data, size_t size) {
    char path[2048];
    int n = snprintf(path, sizeof(path), "%s/%s", test->directory, name);
    assert(n > 0 && (size_t)n < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, size, file) == size && !fclose(file));
}

static void bind_capture(void *context, void (*focus)(void *, bool),
    void (*exit_app)(void *), void *runner) {
    (void)focus; (void)exit_app;
    ((capture_t *)context)->host = runner;
}

static void tap(product_host_t *host, int x, int y) {
    for (unsigned phase=0;phase<=2;phase+=2) {
        uint8_t value[12]={0}; value[1]=(uint8_t)phase;
        pxa_write_u32(value+4,(uint32_t)x); pxa_write_u32(value+8,(uint32_t)y);
        assert(!pxa_ui_queue_event(host->ui,host->active_component,PXA_UI_PRIMARY_SURFACE,2,
            PXA_UI_EVENT_POINTER,PXA_UI_EVENT_FLAG_RELIABLE,now_us(NULL),value,sizeof(value)));
        dispatch_component_events(host);
    }
}

static void capture_named(capture_t *test, const char *name) {
    lv_draw_buf_t *snapshot=lv_snapshot_take(test->host->content_parent,LV_COLOR_FORMAT_ARGB8888);
    assert(snapshot);
    save(test,name,snapshot->data,(size_t)snapshot->header.stride*snapshot->header.h);
    lv_draw_buf_destroy(snapshot);
}

static void walk(capture_t *test) {
    static const unsigned pages[]={0,1,2,3,0,5};
    uint64_t now=now_us(NULL);
    if (test->step<6) {
        if (now-test->step_time<UINT64_C(250000)) return;
        unsigned page=pages[test->step];
        unsigned expected=test->step==4 ? 2 : 1;
        if (test->wait_ready && ready_page[page]<expected) return;
        if (test->wait_ready) assert(!legacy_lookups);
        if (test->step<5) {
            char name[64]; snprintf(name,sizeof(name),"menu-%u.bgra",test->step);
            capture_named(test,name);
        } else capture_named(test,"battle-start.bgra");
        ++test->step; test->step_time=now;
        if (test->step==1) tap(test->host,120,215);
        else if (test->step==2) tap(test->host,180,215);
        else if (test->step==3) tap(test->host,240,215);
        else if (test->step==4) tap(test->host,60,215);
        else if (test->step==5) tap(test->host,150,165);
        return;
    }
    if (test->step==6 && now-test->step_time>=UINT64_C(2000000)) {
        product_focus_changed(test->host,false); dispatch_lifecycle(test->host);
        capture_named(test,"battle-paused.bgra");
        ++test->step; test->step_time=now;
    } else if (test->step==7 && now-test->step_time>=UINT64_C(300000)) {
        capture_named(test,"battle-still-paused.bgra");
        product_focus_changed(test->host,true); ++test->step; test->step_time=now;
    } else if (test->step==8 && now-test->step_time>=UINT64_C(1000000)) {
        capture_named(test,"battle-resumed.bgra");
        test->captured=1; test->host->exit_requested=1;
    }
}

static void pump_capture(void *context) {
    capture_t *test = context;
    product_host_t *host = test->host;
    if (!host || test->captured) return;
    uint64_t elapsed = now_us(NULL) - test->started;
    assert(elapsed < UINT64_C(30000000));
    if (host->active_component && host->ui && elapsed>=UINT64_C(100000)) {
        if (test->exit_loading) {
            assert(!ready_home && !legacy_lookups);
            test->captured=1; host->exit_requested=1; return;
        }
        if (test->background_loading && !test->background_state) {
            assert(!ready_home);
            product_focus_changed(host,false); test->background_state=1;
            test->step_time=now_us(NULL); return;
        }
        if (test->background_state==1) {
            assert(!ready_home);
            if (now_us(NULL)-test->step_time<UINT64_C(300000)) return;
            product_focus_changed(host,true); test->background_state=2; return;
        }
    }
    if (elapsed < UINT64_C(1000000) || !host->active_component || !host->ui) return;
    pxa_ui_memory_snapshot_t ui;
    assert(!pxa_ui_memory_snapshot(host->ui, host->active_component, &ui));
    if (!ui.canvas_count || !ui.commit_count) return;
    if (test->walkthrough) { walk(test); return; }
    if (test->wait_ready && !ready_home) return;
    if (test->wait_ready) assert(!legacy_lookups);
    pxa_wamr_memory_snapshot_t wasm;
    pxa_memory_stats_t memory;
    assert(!pxa_wamr_engine_memory_snapshot(host->engine, &wasm));
    assert(!pxa_memory_budget_stats(&host->resource_budget, 0, &memory));
    /* Collect counters before allocating the capture buffer. LVGL heap and
     * decoder allocations are not included in the shared budget counters. */
    lv_draw_buf_t *snapshot = lv_snapshot_take(host->content_parent,
                                               LV_COLOR_FORMAT_ARGB8888);
    assert(snapshot && snapshot->header.w && snapshot->header.h);
    save(test, "screen.bgra", snapshot->data,
         (size_t)snapshot->header.stride * snapshot->header.h);
    char report[2048];
    int n = snprintf(report, sizeof(report),
        "{\"width\":%u,\"height\":%u,\"stride\":%u,"
        "\"linear_current\":%llu,\"linear_peak\":%llu,\"artifact_buffers\":%llu,"
        "\"wamr_current\":%u,\"wamr_peak\":%u,"
        "\"ui_current\":%zu,\"ui_peak\":%zu,\"ui_nodes\":%zu,\"ui_commits\":%llu,"
        "\"shared_internal\":%zu,\"shared_external\":%zu,"
        "\"shared_peak_internal\":%zu,\"shared_peak_external\":%zu,"
        "\"image_internal\":%zu,\"image_external\":%zu,"
        "\"legacy_lookups\":%u,\"ready_home\":%u,"
        "\"scope\":\"opening screen only; shared budget excludes native LVGL heap and decoder allocations\"}\n",
        snapshot->header.w, snapshot->header.h, snapshot->header.stride,
        (unsigned long long)wasm.linear_current_bytes,
        (unsigned long long)wasm.linear_peak_bytes,
        (unsigned long long)wasm.artifact_buffer_bytes,
        wasm.current_bytes, wasm.peak_bytes, ui.current_bytes, ui.peak_bytes,
        ui.node_count, (unsigned long long)ui.commit_count,
        memory.charged[0], memory.charged[1], memory.peak[0], memory.peak[1],
        memory.by_kind[PXA_MEMORY_IMAGE][0], memory.by_kind[PXA_MEMORY_IMAGE][1],
        legacy_lookups, ready_home);
    assert(n > 0 && (size_t)n < sizeof(report));
    save(test, "memory.json", report, (size_t)n);
    lv_draw_buf_destroy(snapshot);
    test->captured = 1;
    host->exit_requested = 1;
}

int main(int argc, char **argv) {
    assert(argc == 4 || argc == 5);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    options_t options = {0};
    options.package_path = argv[1]; options.publisher_key = argv[2];
    options.width = 296; options.height = 240; options.locale = "en-US";
    capture_t test = {0}; test.directory = argv[3]; test.started = now_us(NULL);
    if (argc==5) {
        test.walkthrough=!strcmp(argv[4],"walk") || !strcmp(argv[4],"walk-baseline");
        test.background_loading=!strcmp(argv[4],"background-loading");
        test.exit_loading=!strcmp(argv[4],"exit-loading");
        test.wait_ready=strcmp(argv[4],"walk-baseline")!=0;
        assert(test.walkthrough || test.background_loading || test.exit_loading || !strcmp(argv[4],"wait-ready"));
    }
    test.step_time=test.started;
    assert(!run_product_simulator(&options, NULL, NULL, pump_capture, &test,
        NULL, &test, bind_capture, NULL, NULL, NULL));
    assert(test.captured && !test.host);
    printf("Signed UI AOT passed: walkthrough=%u step=%u background_loading=%u exit_loading=%u legacy_lookups=%u ready_home=%u ready_battle=%u\n",
        test.walkthrough,test.step,test.background_loading,test.exit_loading,legacy_lookups,ready_home,ready_page[5]);
    return 0;
}

/* Signed Store AOT integration: deterministic offline catalog, real UI/assets.
 * Captures are deliberately outside the measured resource budget. */
#undef NDEBUG
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static unsigned ready, unavailable, legacy_lookups;
static void observe_log(uint32_t component,unsigned level,const uint8_t *data,size_t size) {
    (void)component;(void)level;
    const char *ok="store: search image ready", *fail="store: search image unavailable";
    if(size==strlen(ok) && !memcmp(data,ok,size)) ++ready;
    if(size==strlen(fail) && !memcmp(data,fail,size)) ++unavailable;
}
static void observe_asset(const uint8_t *path,size_t size) {
    (void)path;(void)size;++legacy_lookups;
}
#define PXSYS_PRODUCT_LOG_OBSERVER observe_log
#define PXSYS_PRODUCT_ASSET_LOOKUP_OBSERVER observe_asset
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#define pxsys_desktop_net_backend store_test_net_backend
#include "../product_runner.c"
#include <assert.h>

static pxa_status_t offline_start(void *context,const pxa_net_request_t *request,uint64_t *operation) {
    (void)context;(void)request;*operation=0;return PXA_STATUS_UNAVAILABLE;
}
static pxa_status_t offline_poll(void *context,uint64_t operation,pxa_net_response_t *response) {
    (void)context;(void)operation;(void)response;assert(0);return PXA_STATUS_WOULD_BLOCK;
}
static void offline_cancel(void *context,uint64_t operation) {(void)context;(void)operation;}
static pxa_status_t offline_read(void *context,void *stream,uint8_t *out,size_t capacity,size_t *size) {
    (void)context;(void)stream;(void)out;(void)capacity;*size=0;return PXA_STATUS_IO_ERROR;
}
static void offline_close(void *context,void *stream) {(void)context;(void)stream;}
int store_test_net_backend(pxa_net_backend_t *out,pxsys_desktop_net_notify_fn notify,void *context) {
    (void)notify;(void)context;memset(out,0,sizeof(*out));out->struct_size=sizeof(*out);
    out->start=offline_start;out->poll=offline_poll;out->cancel=offline_cancel;
    out->read_body=offline_read;out->close_body=offline_close;return 1;
}
typedef struct {
    product_host_t *host;
    const char *directory;
    uint64_t started,step_time;
    unsigned step,captured,baseline,background_loading,background_state,exit_loading,low_budget,initial_unavailable;
} capture_t;
static void save(capture_t *test,const char *name,const void *data,size_t size) {
    char path[2048];int n=snprintf(path,sizeof(path),"%s/%s",test->directory,name);
    assert(n>0 && (size_t)n<sizeof(path));FILE *f=fopen(path,"wb");
    assert(f && fwrite(data,1,size,f)==size && !fclose(f));
}
static void bind_capture(void *context,void (*focus)(void *,bool),void (*exit_app)(void *),void *runner) {
    (void)focus;(void)exit_app;((capture_t *)context)->host=runner;
}
static void click(capture_t *test,uint32_t node) {
    assert(!pxa_ui_queue_event(test->host->ui,test->host->active_component,PXA_UI_PRIMARY_SURFACE,node,
        PXA_UI_EVENT_ACTION,PXA_UI_EVENT_FLAG_RELIABLE,now_us(NULL),NULL,0));
    dispatch_component_events(test->host);
}
static void capture(capture_t *test) {
    pxa_ui_memory_snapshot_t ui;pxa_wamr_memory_snapshot_t wasm;pxa_memory_stats_t memory;
    assert(!pxa_ui_memory_snapshot(test->host->ui,test->host->active_component,&ui));
    assert(!pxa_wamr_engine_memory_snapshot(test->host->engine,&wasm));
    assert(!pxa_memory_budget_stats(&test->host->resource_budget,0,&memory));
    lv_draw_buf_t *snapshot=lv_snapshot_take(test->host->content_parent,LV_COLOR_FORMAT_ARGB8888);
    assert(snapshot && snapshot->header.w && snapshot->header.h);
    char name[80];snprintf(name,sizeof(name),"screen-%u.bgra",test->step);
    save(test,name,snapshot->data,(size_t)snapshot->header.stride*snapshot->header.h);
    char report[2048];
    int n=snprintf(report,sizeof(report),
        "{\"width\":%u,\"height\":%u,\"stride\":%u,"
        "\"linear_current\":%llu,\"linear_peak\":%llu,\"artifact_buffers\":%llu,"
        "\"wamr_current\":%u,\"wamr_peak\":%u,"
        "\"ui_current\":%zu,\"ui_peak\":%zu,\"ui_nodes\":%zu,\"ui_commits\":%llu,"
        "\"shared_internal\":%zu,\"shared_external\":%zu,"
        "\"shared_peak_internal\":%zu,\"shared_peak_external\":%zu,"
        "\"image_internal\":%zu,\"image_external\":%zu,"
        "\"legacy_lookups\":%u,\"ready\":%u,\"unavailable\":%u,"
        "\"scope\":\"offline static Store screen; shared budget excludes native LVGL heap and decoder allocations\"}\n",
        snapshot->header.w,snapshot->header.h,snapshot->header.stride,
        (unsigned long long)wasm.linear_current_bytes,(unsigned long long)wasm.linear_peak_bytes,
        (unsigned long long)wasm.artifact_buffer_bytes,wasm.current_bytes,wasm.peak_bytes,
        ui.current_bytes,ui.peak_bytes,ui.node_count,(unsigned long long)ui.commit_count,
        memory.charged[0],memory.charged[1],memory.peak[0],memory.peak[1],
        memory.by_kind[PXA_MEMORY_IMAGE][0],memory.by_kind[PXA_MEMORY_IMAGE][1],legacy_lookups,ready,unavailable);
    assert(n>0 && (size_t)n<sizeof(report));snprintf(name,sizeof(name),"memory-%u.json",test->step);
    save(test,name,report,(size_t)n);lv_draw_buf_destroy(snapshot);
}
static void pump_capture(void *context) {
    capture_t *test=context;product_host_t *host=test->host;
    if(!host || test->captured)return;
    uint64_t now=now_us(NULL),elapsed=now-test->started;
    assert(elapsed<UINT64_C(30000000));
    if(!host->active_component || !host->ui)return;
    pxa_ui_memory_snapshot_t ui;
    assert(!pxa_ui_memory_snapshot(host->ui,host->active_component,&ui));
    if(!ui.commit_count)return;
    if(test->exit_loading) {
        assert(!ready && !legacy_lookups);test->captured=1;host->exit_requested=1;return;
    }
    if(test->background_loading && !test->background_state) {
        assert(!ready);product_focus_changed(host,false);dispatch_lifecycle(host);
        test->background_state=1;test->step_time=now;return;
    }
    if(test->background_state==1) {
        assert(!ready);
        if(now-test->step_time<UINT64_C(300000))return;
        product_focus_changed(host,true);dispatch_lifecycle(host);test->background_state=2;
        test->step_time=now;return;
    }
    if(elapsed<UINT64_C(1000000) || now-test->step_time<UINT64_C(300000))return;
    if(!test->baseline) {
        if(test->low_budget) {
            if(!unavailable)return;
            // Startup foreground can retry if the initial load has already
            // failed. Snapshot that bounded race; navigation must not retry,
            // while the explicit foreground resume must add exactly one.
            if (!test->initial_unavailable) {
                assert(unavailable>=1 && unavailable<=2);
                test->initial_unavailable=unavailable;
            }
            const unsigned expected=test->initial_unavailable+(test->step>=5);
            assert(!ready && unavailable==expected);
        }
        else {if(!ready)return;assert(ready==1 && !unavailable);}
        assert(!legacy_lookups);
    }
    if(test->step<4) {
        capture(test);++test->step;test->step_time=now;
        if(test->step==1 || test->step==3)click(test,60);
        else if(test->step==2)click(test,40);
        else {product_focus_changed(host,false);dispatch_lifecycle(host);}
    } else if(test->step==4) {
        product_focus_changed(host,true);dispatch_lifecycle(host);++test->step;test->step_time=now;
    } else {
        capture(test);test->captured=1;host->exit_requested=1;
    }
}
int main(int argc,char **argv) {
    assert(argc==5);setenv("SDL_VIDEODRIVER","dummy",1);setenv("SDL_AUDIODRIVER","dummy",1);
    options_t options={0};options.package_path=argv[1];options.publisher_key=argv[2];
    options.width=296;options.height=240;options.locale="en-US";
    capture_t test={0};test.directory=argv[3];test.started=now_us(NULL);test.step_time=test.started;
    test.baseline=!strcmp(argv[4],"baseline");test.background_loading=!strcmp(argv[4],"background-loading");
    test.exit_loading=!strcmp(argv[4],"exit-loading");test.low_budget=!strcmp(argv[4],"low-budget");
    assert(test.baseline || test.background_loading || test.exit_loading || test.low_budget || !strcmp(argv[4],"ready"));
    assert(!run_product_simulator(&options,NULL,NULL,pump_capture,&test,NULL,&test,bind_capture,NULL,NULL,NULL));
    assert(test.captured && !test.host);
    if(test.baseline)assert(legacy_lookups);
    printf("Signed Store AOT passed: step=%u background=%u exit=%u low_budget=%u legacy=%u ready=%u unavailable=%u\n",
        test.step,test.background_state,test.exit_loading,test.low_budget,legacy_lookups,ready,unavailable);
    return 0;
}

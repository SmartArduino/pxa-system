/* Signed Weather AOT, real UI and asset services, deterministic HTTP bodies. */
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
static unsigned ready, unavailable, legacy_lookups;
static void observe_log(uint32_t component,unsigned level,const uint8_t *data,size_t size) {
    (void)component;(void)level;
    const char *ok="weather: images ready", *fail="weather: images unavailable";
    if(size==strlen(ok) && !memcmp(data,ok,size)) ++ready;
    if(size==strlen(fail) && !memcmp(data,fail,size)) ++unavailable;
}
static void observe_asset(const uint8_t *path,size_t size) {
    (void)path;(void)size;++legacy_lookups;
}
#define PXSYS_PRODUCT_LOG_OBSERVER observe_log
#define PXSYS_PRODUCT_ASSET_LOOKUP_OBSERVER observe_asset
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#define PXSYS_CLOCK_NOW_US() UINT64_C(0x5eed1234)
#define pxsys_desktop_net_backend weather_test_net_backend
#include "../product_runner.c"

static const char who[]="{\"success\":true,\"city\":\"Beijing\",\"latitude\":39.9042,\"longitude\":116.4074}";
static const char meteo[]="{\"current_units\":{\"time\":\"iso8601\",\"temperature_2m\":\"°C\"},"
    "\"current\":{\"time\":\"2026-09-24T18:30\",\"temperature_2m\":21.1,\"relative_humidity_2m\":71,"
    "\"apparent_temperature\":22.3,\"wind_speed_10m\":4.7,\"weather_code\":2,\"is_day\":1},"
    "\"hourly\":{\"time\":[\"2026-09-24T18:00\",\"2026-09-24T19:00\"],\"temperature_2m\":[20.5,19.4],"
    "\"precipitation_probability\":[44,60],\"weather_code\":[2,61]},"
    "\"daily\":{\"time\":[\"2026-09-24\",\"2026-09-25\"],\"weather_code\":[2,61],"
    "\"temperature_2m_max\":[29.3,26.1],\"temperature_2m_min\":[17.3,16.5],"
    "\"precipitation_probability_max\":[44,80],\"uv_index_max\":[5.3,3.1],"
    "\"sunrise\":[\"2026-09-24T06:02\",\"2026-09-25T06:03\"],"
    "\"sunset\":[\"2026-09-24T18:09\",\"2026-09-25T18:08\"]}}";
static const char air[]="{\"current_units\":{\"pm2_5\":\"µg/m³\"},\"current\":{\"pm2_5\":48.8,\"european_aqi\":60}}";
static const char history[]="{\"daily\":{\"time\":[\"2026-09-13\",\"2026-09-14\"],\"weather_code\":[3,51],"
    "\"temperature_2m_max\":[31.5,28.2],\"temperature_2m_min\":[19.4,21.2],\"precipitation_sum\":[0.00,1.10]}}";
typedef struct {const char *body;size_t offset;uint64_t operation;int pending;} reply_t;
static reply_t reply;
static uint64_t next_operation;
static unsigned requests[4],completed[4];
static pxsys_desktop_net_notify_fn notify_ready;
static void *notify_context;
static pxa_status_t offline_start(void *context,const pxa_net_request_t *request,uint64_t *operation) {
    (void)context;assert(!reply.pending && !reply.body && request->url.size<512);
    char url[512];memcpy(url,request->url.data,request->url.size);url[request->url.size]=0;
    unsigned id;
    if(strstr(url,"ipwho.is")) id=0;
    else if(strstr(url,"air-quality-api.open-meteo.com")) id=2;
    else if(strstr(url,"archive-api.open-meteo.com")) id=3;
    else if(strstr(url,"api.open-meteo.com")) id=1;
    else {fprintf(stderr,"unexpected Weather URL: %s\n",url);assert(0);return PXA_STATUS_NOT_FOUND;}
    static const char *const bodies[]={who,meteo,air,history};
    reply.body=bodies[id];reply.offset=0;reply.operation=++next_operation;reply.pending=1;
    ++requests[id];*operation=reply.operation;notify_ready(notify_context);return PXA_STATUS_OK;
}
static pxa_status_t offline_poll(void *context,uint64_t operation,pxa_net_response_t *response) {
    (void)context;assert(reply.pending && operation==reply.operation);
    reply.pending=0;response->struct_size=sizeof(*response);response->status_code=200;
    static const char json[]="application/json";
    response->content_type=(pxa_bytes_t){(const uint8_t *)json,sizeof(json)-1};
    response->body_stream=&reply;response->body_length=strlen(reply.body);
    response->flags=PXA_NET_RESPONSE_BODY_PRESENT|PXA_NET_RESPONSE_BODY_LENGTH_KNOWN;
    return PXA_STATUS_OK;
}
static void offline_cancel(void *context,uint64_t operation) {
    (void)context;assert(operation==reply.operation);reply.pending=0;reply.body=NULL;
}
static pxa_status_t offline_read(void *context,void *stream,uint8_t *output,size_t capacity,size_t *size) {
    (void)context;reply_t *r=stream;assert(r==&reply && r->body);
    size_t remaining=strlen(r->body)-r->offset;
    *size=remaining<capacity?remaining:capacity;
    memcpy(output,r->body+r->offset,*size);r->offset+=*size;return PXA_STATUS_OK;
}
static void offline_close(void *context,void *stream) {
    (void)context;reply_t *r=stream;assert(r==&reply && r->body);
    assert(r->offset==strlen(r->body));
    if(r->body==who)++completed[0];else if(r->body==meteo)++completed[1];
    else if(r->body==air)++completed[2];else if(r->body==history)++completed[3];
    r->body=NULL;
}
int weather_test_net_backend(pxa_net_backend_t *out,pxsys_desktop_net_notify_fn notify,void *context) {
    notify_ready=notify;notify_context=context;
    memset(out,0,sizeof(*out));out->struct_size=sizeof(*out);
    out->start=offline_start;out->poll=offline_poll;out->cancel=offline_cancel;
    out->read_body=offline_read;out->close_body=offline_close;return 1;
}

typedef struct {product_host_t *host;const char *directory;uint64_t started,phase_time;
    unsigned phase,finished,baseline,background,exit_loading,low_budget;} capture_t;
static void save(capture_t *test,const char *name,const void *data,size_t size) {
    char path[2048];int n=snprintf(path,sizeof(path),"%s/%s",test->directory,name);
    assert(n>0 && (size_t)n<sizeof(path));FILE *file=fopen(path,"wb");
    assert(file && fwrite(data,1,size,file)==size && !fclose(file));
}
static uint64_t monotonic_ns(void) {
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC,&now)==0);
    return (uint64_t)now.tv_sec*UINT64_C(1000000000)+(uint64_t)now.tv_nsec;
}
static void benchmark_snapshot(capture_t *test,unsigned index) {
    const char *setting=getenv("PXA_WEATHER_BENCH_FRAMES");
    if(!setting || !*setting)return;
    char *end=NULL;
    unsigned long frames=strtoul(setting,&end,10);
    assert(end && *end=='\0' && frames>0 && frames<=500);
    char name[80],path[2048];
    snprintf(name,sizeof(name),"draw-%u.csv",index);
    int n=snprintf(path,sizeof(path),"%s/%s",test->directory,name);
    assert(n>0 && (size_t)n<sizeof(path));
    FILE *file=fopen(path,"wb");assert(file);
    assert(fputs("frame,draw_ns\n",file)>=0);
    for(unsigned frame=0;frame<frames+20;++frame) {
        uint64_t started=monotonic_ns();
        lv_draw_buf_t *snapshot=lv_snapshot_take(test->host->content_parent,
                                                 LV_COLOR_FORMAT_ARGB8888);
        assert(snapshot);
        uint64_t finished=monotonic_ns();
        lv_draw_buf_destroy(snapshot);
        if(frame>=20)
            assert(fprintf(file,"%u,%llu\n",frame-20,
                           (unsigned long long)(finished-started))>0);
    }
    assert(fclose(file)==0);
}
static void bind_capture(void *context,void (*focus)(void *,bool),void (*exit_app)(void *),void *runner) {
    (void)focus;(void)exit_app;((capture_t *)context)->host=runner;
}
static void capture(capture_t *test,unsigned index) {
    pxa_ui_memory_snapshot_t ui;pxa_wamr_memory_snapshot_t wasm;pxa_memory_stats_t memory;
    assert(!pxa_ui_memory_snapshot(test->host->ui,test->host->active_component,&ui));
    assert(!pxa_wamr_engine_memory_snapshot(test->host->engine,&wasm));
    assert(!pxa_memory_budget_stats(&test->host->resource_budget,0,&memory));
    /* Process allocator diagnostic only: includes WAMR, LVGL, and unrelated
     * libc allocations; it is not an ESP RAM class or a subsystem budget. */
    size_t process_malloc_current=mallinfo2().uordblks;
    lv_draw_buf_t *snapshot=lv_snapshot_take(test->host->content_parent,LV_COLOR_FORMAT_ARGB8888);
    assert(snapshot && snapshot->header.w && snapshot->header.h);
    char name[80];snprintf(name,sizeof(name),"screen-%u.bgra",index);
    save(test,name,snapshot->data,(size_t)snapshot->header.stride*snapshot->header.h);
    char report[2048];int n=snprintf(report,sizeof(report),
        "{\"width\":%u,\"height\":%u,\"stride\":%u,\"linear_current\":%llu,\"linear_peak\":%llu,"
        "\"artifact_buffers\":%llu,\"wamr_current\":%u,\"wamr_peak\":%u,\"ui_current\":%zu,"
        "\"ui_peak\":%zu,\"ui_nodes\":%zu,\"ui_commits\":%llu,\"shared_internal\":%zu,"
        "\"shared_external\":%zu,\"shared_peak_internal\":%zu,\"shared_peak_external\":%zu,"
        "\"image_internal\":%zu,\"image_external\":%zu,\"legacy_lookups\":%u,"
        "\"ready\":%u,\"unavailable\":%u,\"process_malloc_current\":%zu,"
        "\"network_complete\":[%u,%u,%u,%u]}\n",
        snapshot->header.w,snapshot->header.h,snapshot->header.stride,
        (unsigned long long)wasm.linear_current_bytes,(unsigned long long)wasm.linear_peak_bytes,
        (unsigned long long)wasm.artifact_buffer_bytes,wasm.current_bytes,wasm.peak_bytes,
        ui.current_bytes,ui.peak_bytes,ui.node_count,(unsigned long long)ui.commit_count,
        memory.charged[0],memory.charged[1],memory.peak[0],memory.peak[1],
        memory.by_kind[PXA_MEMORY_IMAGE][0],memory.by_kind[PXA_MEMORY_IMAGE][1],
        legacy_lookups,ready,unavailable,process_malloc_current,
        completed[0],completed[1],completed[2],completed[3]);
    assert(n>0 && (size_t)n<sizeof(report));snprintf(name,sizeof(name),"memory-%u.json",index);
    save(test,name,report,(size_t)n);lv_draw_buf_destroy(snapshot);
    benchmark_snapshot(test,index);
}
static lv_obj_t *find_scroll(lv_obj_t *parent) {
    if(lv_obj_is_scrollable(parent) && lv_obj_get_scroll_bottom(parent)>40)
        return parent;
    uint32_t count=lv_obj_get_child_count(parent);
    for(uint32_t i=0;i<count;++i) {
        lv_obj_t *found=find_scroll(lv_obj_get_child(parent,(int32_t)i));
        if(found)return found;
    }
    return NULL;
}
static void pump_capture(void *context) {
    capture_t *test=context;product_host_t *host=test->host;
    if(!host || test->finished)return;
    uint64_t now=now_us(NULL);
    if(now-test->started>=UINT64_C(30000000)) {
        fprintf(stderr,"Weather timeout: phase=%u ready=%u unavailable=%u legacy=%u requests=%u,%u,%u,%u completed=%u,%u,%u,%u pending=%d body=%p\n",
            test->phase,ready,unavailable,legacy_lookups,requests[0],requests[1],requests[2],requests[3],
            completed[0],completed[1],completed[2],completed[3],reply.pending,(void *)reply.body);
        assert(0);
    }
    if(!host->active_component || !host->ui)return;
    pxa_ui_memory_snapshot_t ui;assert(!pxa_ui_memory_snapshot(host->ui,host->active_component,&ui));
    if(!ui.commit_count)return;
    if(test->exit_loading) {test->finished=1;host->exit_requested=1;return;}
    if(test->background && test->phase==0) {
        product_focus_changed(host,false);dispatch_lifecycle(host);
        test->phase=1;test->phase_time=now;return;
    }
    if(test->background && test->phase==1) {
        if(now-test->phase_time<UINT64_C(300000))return;
        product_focus_changed(host,true);dispatch_lifecycle(host);
        test->phase=2;test->phase_time=now;return;
    }
    if(now-test->phase_time<UINT64_C(1000000) || completed[3]<1)return;
    if(!test->baseline) {
        if(test->low_budget) {if(!unavailable)return;assert(!ready);}
        else {if(!ready)return;assert(!unavailable);}
        assert(!legacy_lookups);
    }
    capture(test,0);
    if(!test->low_budget) {
        lv_obj_t *scroll=find_scroll(host->content_parent);
        assert(scroll);
        int32_t bottom=lv_obj_get_scroll_bottom(scroll);assert(bottom>40);
        lv_obj_scroll_to_y(scroll,bottom/2,LV_ANIM_OFF);capture(test,1);
        lv_obj_scroll_to_y(scroll,bottom,LV_ANIM_OFF);capture(test,2);
        if(getenv("PXA_WEATHER_EXTRA_SCREENS")) {
            for(unsigned part=1;part<8;++part) {
                lv_obj_scroll_to_y(scroll,(int32_t)((int64_t)bottom*part/8),LV_ANIM_OFF);
                capture(test,part+2);
            }
        }
    }
    test->finished=1;host->exit_requested=1;
}
int main(int argc,char **argv) {
    assert(argc==5);setenv("SDL_VIDEODRIVER","dummy",1);setenv("SDL_AUDIODRIVER","dummy",1);
    options_t options={0};options.package_path=argv[1];options.publisher_key=argv[2];
    options.width=296;options.height=240;options.locale="en-US";
    char state[2048];int n=snprintf(state,sizeof(state),"%s/state",argv[3]);
    assert(n>0 && (size_t)n<sizeof(state));assert(!mkdir(state,0700) || errno==EEXIST);
    options.state_root=state;
    capture_t test={0};test.directory=argv[3];test.started=now_us(NULL);test.phase_time=test.started;
    test.baseline=!strcmp(argv[4],"baseline");test.background=!strcmp(argv[4],"background-loading");
    test.exit_loading=!strcmp(argv[4],"exit-loading");test.low_budget=!strcmp(argv[4],"low-budget");
    assert(test.baseline || test.background || test.exit_loading || test.low_budget || !strcmp(argv[4],"ready"));
    assert(!run_product_simulator(&options,NULL,NULL,pump_capture,&test,NULL,&test,bind_capture,NULL,NULL,NULL));
    assert(test.finished && !test.host);
    if(!test.exit_loading)assert(completed[0] && completed[1] && completed[2] && completed[3]);
    if(test.baseline)assert(legacy_lookups);
    printf("Signed Weather AOT passed: background=%u exit=%u low_budget=%u legacy=%u ready=%u unavailable=%u requests=%u,%u,%u,%u\n",
        test.background,test.exit_loading,test.low_budget,legacy_lookups,ready,unavailable,
        requests[0],requests[1],requests[2],requests[3]);
    return 0;
}

/* SPDX-License-Identifier: MIT
 * Browser platform adapter. Guest execution stays in the browser's Wasm VM;
 * this module owns the existing Core services and LVGL renderer. */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "pxa/runtime.h"
#include "pxa/service.h"
#include "pxa/clock.h"
#include "pxa/window.h"
#include "pxa/lvgl/pxa_lvgl_ui.h"
#include "fonts.h"
#include "services.h"
#include "pxa/audio.h"
#include "pxa/game_render.h"
#include "pxa/event_port.h"

static pxa_runtime_t *runtime;
static pxa_component_t component;
static pxa_ui_service_t *ui;
static pxa_lvgl_ui_t *adapter;
static pxa_window_service_t *window;
static lv_obj_t *editor;
static uint32_t editor_session;
static lv_display_t *display;
static lv_indev_t *pointer;
static uint8_t *frame;
static uint32_t width, height, revision, clock_period;
static uint64_t current_us, next_tick;
static int pointer_x, pointer_y, pointer_down;
static pxa_handle64_t event_port;
EM_JS(int, main_loop_enabled, (), {return !!Module.browserMainLoop;});
static void close_event_port(void *context) {(void)context;event_port=0;}
static int open_event_port(uint64_t token,uint32_t size) {
    if(!main_loop_enabled())return PXA_STATUS_UNSUPPORTED;
    pxa_status_t s=pxa_component_validate_import(runtime,component);
    if(s)return s;
    if(!token||size)return PXA_STATUS_INVALID_ARGUMENT;
    if(event_port)return PXA_STATUS_BAD_STATE;
    s=pxa_request_begin_reserved(runtime,component,token,PXA_SERVICE_CORE,PXA_CORE_OPEN_EVENT_PORT,0,8);
    if(s)return s;
    pxa_resource_t resource={NULL,NULL,close_event_port};
    s=pxa_handle_open64(runtime,component,PXA_RESOURCE_EVENT_PORT,0,&resource,&event_port);
    uint8_t result[8];pxa_write_u64(result,event_port);
    int completed=pxa_request_complete(runtime,component,token,s,s?NULL:result,s?0:8);
    if(completed&&event_port)pxa_handle_close64(runtime,component,event_port);
    return completed;
}
EMSCRIPTEN_KEEPALIVE int browser_event_wait(uint64_t handle,uint8_t *data,uint32_t size) {
    pxa_resource_t resource;
    pxa_status_t s=pxa_handle_get64(runtime,component,handle,PXA_RESOURCE_EVENT_PORT,&resource);
    if(s)return s;
    if(handle!=event_port)return PXA_STATUS_BAD_STATE;
    pxa_component_snapshot_t state;
    s=pxa_component_snapshot(runtime,component,&state);if(s)return s;
    if(state.state!=PXA_COMPONENT_RUNNING||state.callback!=PXA_GUEST_CALLBACK_EVENT)return PXA_STATUS_BAD_STATE;
    if(size<PXA_ENVELOPE_SIZE||size>PXA_MAX_CONTROL_MESSAGE)return PXA_STATUS_INVALID_ARGUMENT;
    if(pxa_read_u64(data)>PXA_EVENT_PORT_MAX_WAIT_US)return PXA_STATUS_LIMIT_EXCEEDED;
    pxa_event_view_t view;s=pxa_event_peek(runtime,component,&view);if(s)return s;
    if(view.size>size)return PXA_STATUS_RESOURCE_LIMIT;
    size_t written=0;s=pxa_event_pop(runtime,component,data,size,&written);
    return s?s:(int)written;
}
EM_JS(int, native_fonts, (), {return typeof Module.browserFontMetrics==='function';});
EM_JS(int, chinese_locale, (), {return String(Module.browserLocale||"en").toLowerCase().startsWith("zh");});
EM_JS(void, notify_editor, (uint32_t session, const char *text, int password,
                           int multiline, int cursor, int max_bytes), {
    Module.browserNotify?.({type:'ime',session,text:text?UTF8ToString(text):null,
        password:!!password,multiline:!!multiline,cursor,maxBytes:max_bytes});
});
EM_JS(void, notify_toast, (const char *text, uint32_t ms), {
    Module.browserNotify?.({type:'toast',text:UTF8ToString(text),duration:ms});
});
static void text_focus(void *context, void *textarea, bool focused) {
    (void)context;
    if (!focused && editor != textarea) return;
    editor=focused ? textarea : NULL;
    if (!++editor_session) ++editor_session;
    notify_editor(editor_session,editor ? lv_textarea_get_text(editor) : NULL,
        editor && lv_textarea_get_password_mode(editor),
        editor && !lv_textarea_get_one_line(editor),
        editor ? lv_textarea_get_cursor_pos(editor) : 0,
        editor ? pxa_lvgl_ui_text_input_max_bytes(adapter,editor) : 0);
}
static pxa_status_t window_toast(void *context, const char *text, uint32_t ms) {
    (void)context; notify_toast(text,ms); return PXA_STATUS_OK;
}
EM_JS(uint8_t *, read_image, (const uint8_t *path, size_t size, uint32_t *length), {
    const key=new TextDecoder('utf-8',{fatal:true}).decode(HEAPU8.subarray(path,path+size));
    const bytes=Module.browserAssets?.get(key);
    if(!bytes||bytes.byteLength<24||bytes.byteLength>1048576)return 0;
    const p=_malloc(bytes.byteLength);if(!p)return 0;
    HEAPU8.set(bytes,p);HEAPU32[length>>>2]=bytes.byteLength;return p;
});
static uint32_t big_u32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static const void *resolve_image(const uint8_t *path,size_t size,void *context) {
    (void)context;
    if(!path||size>512)return NULL;
    uint32_t length=0;uint8_t *bytes=read_image(path,size,&length);
    if(!bytes)return NULL;
    uint32_t w=big_u32(bytes+16),h=big_u32(bytes+20);
    if(memcmp(bytes,"\x89PNG\r\n\x1a\n",8)||memcmp(bytes+12,"IHDR",4)||!w||!h||w>1024||h>1024){free(bytes);return NULL;}
    lv_image_dsc_t *image=calloc(1,sizeof(*image));
    if(!image){free(bytes);return NULL;}
    image->header.magic=LV_IMAGE_HEADER_MAGIC;image->header.cf=LV_COLOR_FORMAT_RAW_ALPHA;
    image->header.w=w;image->header.h=h;image->data_size=length;image->data=bytes;
    return image;
}
static void release_image(const void *source,void *context) {
    (void)context;
    const lv_image_dsc_t *image=source;if(!image)return;
    lv_image_cache_drop(image);free((void *)image->data);free((void *)image);
}
static void *allocate(void *c, size_t n) { (void)c; return malloc(n); }
static void release(void *c, void *p) { (void)c; free(p); }
static uint64_t now_us(void *c) { (void)c; return current_us; }
static pxa_status_t execute(pxa_lvgl_ui_execute_callback_fn fn, void *data, void *c) {
    (void)c; fn(data); return PXA_STATUS_OK;
}
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *pixels) {
    (void)area; (void)pixels; ++revision; browser_graphics_presented();lv_display_flush_ready(d);
}
static void read_pointer(lv_indev_t *i, lv_indev_data_t *data) {
    (void)i; data->point.x = pointer_x; data->point.y = pointer_y;
    data->state = pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void ui_event(uint32_t surface, uint32_t node, pxa_ui_event_kind_t kind,
                     uint16_t flags, const void *value, size_t size, void *c) {
    (void)c;
    /* Events are drained by JS after returning from an import; never reenter. */
    pxa_ui_queue_event(ui, component, surface, node, kind, flags, current_us, value, size);
}
static pxa_status_t configure_clock(void *c, pxa_component_t id, uint32_t period) {
    (void)c; (void)id; clock_period = period; next_tick = current_us + period;
    return PXA_STATUS_OK;
}
EM_JS(void, wall_time, (uint8_t *out), {
    const now=new Date(),year=now.getFullYear();
    const standard=Math.max(new Date(year,0,1).getTimezoneOffset(),new Date(year,6,1).getTimezoneOffset());
    const view=new DataView(HEAPU8.buffer);
    view.setBigInt64(out,BigInt(now.getTime())*1000n,true);
    view.setInt32(out+16,-now.getTimezoneOffset()*60,true);
    view.setUint32(out+20,now.getTimezoneOffset()<standard?1:0,true);
});
static pxa_status_t clock_call(void *c, pxa_runtime_t *rt, pxa_component_t id,
                               const pxa_message_view_t *m) {
    (void)c;
    if (m->opcode==PXA_CLOCK_WALL_TIME) {
        if (m->payload.size || !m->request_token) return PXA_STATUS_INVALID_ARGUMENT;
        uint8_t bytes[24]={0};wall_time(bytes);pxa_write_u64(bytes+8,current_us);
        pxa_status_t s=pxa_request_begin_reserved(rt,id,m->request_token,4,m->opcode,0,sizeof(bytes));
        if(s)return s;s=pxa_request_commit(rt,id,m->request_token);
        return s ? s : pxa_request_complete(rt,id,m->request_token,PXA_STATUS_OK,bytes,sizeof(bytes));
    }
    if (m->opcode == PXA_CLOCK_CONFIGURE || m->opcode == PXA_CLOCK_LIMITS) {
        pxa_clock_timing_backend_t backend = {
            {1000, 60000000, 1, 1000, PXA_CLOCK_TIMING_COALESCED | PXA_CLOCK_TIMING_FOREGROUND},
            NULL, configure_clock};
        return pxa_clock_timing_control(rt, id, m, &backend);
    }
    if (m->opcode == PXA_CLOCK_SET_PERIOD && m->payload.size == 2 && !m->request_token) {
        uint32_t ms = pxa_read_u16(m->payload.data);
        if (ms && (ms < 16 || ms > 1000)) return PXA_STATUS_INVALID_ARGUMENT;
        return configure_clock(NULL, id, ms * 1000);
    }
    if (m->opcode == PXA_CLOCK_NOW && !m->payload.size && m->request_token) {
        uint8_t bytes[8]; pxa_write_u64(bytes, current_us);
        pxa_status_t s = pxa_request_begin_reserved(rt, id, m->request_token, 4, m->opcode, 0, 8);
        if (s) return s;
        s = pxa_request_commit(rt, id, m->request_token);
        return s ? s : pxa_request_complete(rt, id, m->request_token, PXA_STATUS_OK, bytes, 8);
    }
    return PXA_STATUS_UNSUPPORTED;
}
static pxa_status_t window_apply(void *c, const pxa_window_configuration_t *v) {
    (void)c; (void)v; return PXA_STATUS_OK;
}
#define CHECK(call) do { int s = (call); if (s) return s; } while (0)
#define WORKSPACE(type) calloc(1, pxa_##type##_workspace_size())

EMSCRIPTEN_KEEPALIVE int browser_init(uint32_t w, uint32_t h) {
    if (runtime || w < 64 || h < 64 || w > 1024 || h > 1024) return PXA_STATUS_INVALID_ARGUMENT;
    width = w; height = h;
    pxa_runtime_limits_t limits; pxa_runtime_limits_init(&limits);
    limits.max_components = 1;
    size_t size = pxa_runtime_workspace_size(&limits);
    void *workspace = calloc(1, size);
    CHECK(pxa_runtime_init(workspace, size, &limits, &runtime));
    CHECK(pxa_component_create(runtime, 1, &component));
    CHECK(pxa_component_set_wire_profile(runtime, component, PXA_WIRE_PROFILE_HANDLE64));
    lv_init();
    display = lv_display_create(w, h);
    if(display)lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);
    frame = calloc(w * h, 4);
    if (!display || !frame) return PXA_STATUS_RESOURCE_LIMIT;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, frame, NULL, w * h * 4, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer, display);
    lv_indev_set_read_cb(pointer, read_pointer);
    lv_indev_set_mode(pointer, LV_INDEV_MODE_EVENT);
    pxa_ui_config_t cfg; pxa_ui_config_init(&cfg);
    cfg.allocate = allocate; cfg.release = release; cfg.now_us = now_us;
    cfg.primary_width = w; cfg.primary_height = h;
    cfg.features = PXA_UI_FEATURE_CANVAS | PXA_UI_FEATURE_GRID |
        PXA_UI_FEATURE_CONTROLLER_INPUT | PXA_UI_FEATURE_RGB565_BITMAP |
        PXA_UI_FEATURE_CANVAS_STREAM_IO | PXA_UI_FEATURE_DYNAMIC_TEXT |
        PXA_UI_FEATURE_TEXT_INPUT_CONTROL | PXA_UI_FEATURE_PASSWORD_INPUT |
        PXA_UI_FEATURE_VIRTUAL_LIST | PXA_UI_FEATURE_MULTIPLE_SURFACES;
    if (native_fonts()) cfg.features |= PXA_UI_FEATURE_SIZED_TEXT;
    CHECK(pxa_ui_service_init(WORKSPACE(ui_service), pxa_ui_service_workspace_size(), runtime, &cfg, &ui));
    CHECK(pxa_ui_service_register(ui));
    pxa_lvgl_ui_config_t lc = {0};
    lc.struct_size = sizeof(lc); lc.allocate = allocate; lc.release = release;
    lc.execute = execute; lc.event_callback = ui_event; lc.now_us = now_us;
    lc.parent_object = lv_screen_active();
    lc.primary_environment.surface = PXA_UI_PRIMARY_SURFACE;
    lc.primary_environment.width = w; lc.primary_environment.height = h;
    lc.primary_environment.density_q16 = lc.primary_environment.font_scale_q16 = 65536;
    pxa_lvgl_ui_theme_init(&lc.theme);
    browser_fonts_init(&lc);
    lc.text_input_focus=text_focus;lc.resolve_asset=resolve_image;lc.release_asset=release_image;
    pxa_ui_backend_t backend;
    CHECK(pxa_lvgl_ui_init(WORKSPACE(lvgl_ui), pxa_lvgl_ui_workspace_size(), &lc, &adapter, &backend));
    CHECK(pxa_ui_bind(ui, component, &backend));
    size = pxa_window_service_workspace_size(1);
    CHECK(pxa_window_service_init(calloc(1, size), size, runtime, 1, &window));
    CHECK(pxa_window_service_register(window));
    pxa_window_backend_t wb = {.struct_size=sizeof(wb), .apply=window_apply,.show_toast=window_toast};
    CHECK(pxa_window_bind(window, component, &wb));
    pxa_window_snapshot_t snapshot = {.logical_width=w,.logical_height=h,
        .pixel_width=w,.pixel_height=h,.density_numerator=1,.density_denominator=1,.focused=1};
    CHECK(pxa_window_update_snapshot(window, component, &snapshot));
    pxa_service_ops_t clock = {.struct_size=sizeof(clock),.service_id=4,
        .major=PXA_CLOCK_SERVICE_MAJOR,.minor=PXA_CLOCK_SERVICE_MINOR,
        .features=PXA_CLOCK_FEATURE_TIMING,.control=clock_call};
    CHECK(pxa_service_register(runtime, &clock));
    CHECK(browser_services_init(runtime));
    CHECK(browser_graphics_init(runtime,w,h));
    return 0;
}
EMSCRIPTEN_KEEPALIVE int browser_begin(int event) {
    return event ? pxa_component_begin_event(runtime, component) : pxa_component_begin_start(runtime, component);
}
EMSCRIPTEN_KEEPALIVE int browser_configuration(uint8_t *data, uint32_t capacity) {
    pxa_ui_environment_t environment;
    uint8_t encoded[128]; size_t size;
    CHECK(pxa_ui_get_environment(ui, component, PXA_UI_PRIMARY_SURFACE, &environment));
    CHECK(pxa_ui_encode_environment(&environment, encoded, sizeof(encoded), &size));
    pxa_writer_t writer; pxa_writer_init(&writer, data, capacity);
    CHECK(pxa_writer_record(&writer, PXA_UI_CONFIG_ENVIRONMENT, encoded, size));
    uint8_t locale[] = {1,0,5,0,'e','n','-','U','S',2,128,1,0,0};
    if (native_fonts() && chinese_locale()) memcpy(locale+4,"zh-CN",5);
    CHECK(pxa_writer_record(&writer, 12, locale, sizeof(locale)));
    return (int)writer.size;
}
EMSCRIPTEN_KEEPALIVE int browser_finish(int event, int result) {
    int status=event ? pxa_component_finish_event(runtime, component, result) : pxa_component_finish_start(runtime, component, result);
    if(!status&&!event&&!result){
        const uint8_t foreground=1;
        status=pxa_event_post_message(runtime,component,17,0x8005,0,(pxa_bytes_t){&foreground,1},0,UINT64_C(0x001100008005));
    }
    return status;
}
EMSCRIPTEN_KEEPALIVE int browser_call(uint32_t route, uint64_t token, uint8_t *data, uint32_t size) {
    if((uint16_t)route==PXA_SERVICE_CORE&&(route>>16)==PXA_CORE_OPEN_EVENT_PORT)
        return open_event_port(token,size);
    pxa_message_view_t m = {(uint16_t)route, (uint16_t)(route >> 16), token, {data, size}};
    return pxa_runtime_control_view(runtime, component, &m);
}
EMSCRIPTEN_KEEPALIVE int browser_io(uint64_t handle, uint32_t op, uint8_t *data, uint32_t size) {
    return pxa_runtime_io64(runtime, component, handle, op, data, size);
}
EMSCRIPTEN_KEEPALIVE int browser_io_memory_kind(uint64_t handle,uint32_t op){
    pxa_resource_t r;
    if(op>=PXA_AUDIO_IO_MIX_PCM&&op<=PXA_AUDIO_IO_PSG_RENDER&&
        !pxa_handle_get64(runtime,component,handle,PXA_RESOURCE_AUDIO_GRAPH,&r))return 1;
    if((op>=PXA_GAME_RENDER_IO_BLIT_INDEXED_FRAME&&op<=PXA_GAME_RENDER_IO_EXECUTE_PRIORITY_TILES)&&
        !pxa_handle_get64(runtime,component,handle,PXA_RESOURCE_GAME_RENDER_CONTEXT,&r))return 2;
    return 0;
}
EMSCRIPTEN_KEEPALIVE int browser_io_memory(uint64_t handle,uint32_t op,uint8_t *data,uint32_t size,uint8_t *memory,uint32_t bytes){
    return pxa_runtime_io64_memory(runtime,component,handle,op,data,size,memory,bytes);
}
EMSCRIPTEN_KEEPALIVE int browser_event(uint8_t *data, uint32_t capacity) {
    pxa_event_view_t view;
    if (pxa_event_peek(runtime, component, &view) != PXA_STATUS_OK) return 0;
    size_t size = 0;
    int status = pxa_event_pop(runtime, component, data, capacity, &size);
    return status ? status : (int)size;
}
EMSCRIPTEN_KEEPALIVE void browser_tick(double milliseconds) {
    uint64_t us = (uint64_t)(milliseconds * 1000);
    if (us < current_us) return;
    lv_tick_inc((uint32_t)(us / 1000 - current_us / 1000)); current_us = us;
    browser_net_tick();
    if (clock_period && current_us >= next_tick) {
        next_tick += ((current_us - next_tick) / clock_period + 1) * clock_period;
        uint8_t bytes[8]; pxa_write_u64(bytes, current_us);
        pxa_event_post_message(runtime, component, 4, PXA_CLOCK_TICK, 0,
            (pxa_bytes_t){bytes, 8}, 0, UINT64_C(0x000400008001));
    }
}
EMSCRIPTEN_KEEPALIVE uint32_t browser_render(void) { browser_graphics_order();lv_timer_handler();return revision; }
EMSCRIPTEN_KEEPALIVE uint8_t *browser_pixels(void) { return frame; }
EMSCRIPTEN_KEEPALIVE void browser_pointer(int x, int y, int down) {
    pointer_x = x < 0 ? 0 : x >= (int)width ? (int)width - 1 : x;
    pointer_y = y < 0 ? 0 : y >= (int)height ? (int)height - 1 : y;
    pointer_down = !!down; lv_indev_read(pointer);
}
EMSCRIPTEN_KEEPALIVE int browser_controller(uint32_t buttons) {
    uint32_t node=pxa_ui_event_root(ui,component,1,PXA_UI_EVENT_CONTROLLER_STATE);
    if (!node) return 0;
    uint8_t data[8] = {0, 1}; pxa_write_u32(data + 4, buttons & 4095);
    return pxa_ui_queue_event(ui, component, 1, node, PXA_UI_EVENT_CONTROLLER_STATE,
        0, current_us, data, sizeof(data));
}

EMSCRIPTEN_KEEPALIVE int browser_back(void) {
    return pxa_window_queue_back(window,component);
}
EMSCRIPTEN_KEEPALIVE int browser_volume_key(uint32_t key) {
    if(key<1||key>4)return PXA_STATUS_INVALID_ARGUMENT;
    uint32_t node=pxa_ui_event_root(ui,component,1,PXA_UI_EVENT_KEY);
    if(!node)return 0;
    uint8_t value[4];pxa_write_u32(value,key);
    return pxa_ui_queue_event(ui,component,1,node,PXA_UI_EVENT_KEY,
        PXA_UI_EVENT_FLAG_RELIABLE,current_us,value,sizeof(value));
}
EMSCRIPTEN_KEEPALIVE int browser_begin_stop(void) {
    CHECK(pxa_component_request_stop(runtime,component,PXA_STOP_NORMAL));
    return pxa_component_begin_stop(runtime,component);
}
EMSCRIPTEN_KEEPALIVE int browser_finish_stop(void) {
    return pxa_component_finish_stop(runtime,component);
}
EMSCRIPTEN_KEEPALIVE void browser_focus(int focused) {
    pxa_window_snapshot_t snapshot;
    if (!pxa_window_get_snapshot(window,component,&snapshot)) {
        if(snapshot.focused!=!!focused){
            const uint8_t life=!!focused;
            pxa_event_post_message(runtime,component,17,0x8005,0,(pxa_bytes_t){&life,1},0,UINT64_C(0x001100008005));
        }
        snapshot.focused=!!focused;
        pxa_window_update_snapshot(window,component,&snapshot);
        pxa_window_flush_metrics(window,component);
    }
}
EMSCRIPTEN_KEEPALIVE int browser_edit(uint32_t session, const char *text,
                                      uint32_t bytes, uint32_t cursor, int action) {
    if (!editor || session!=editor_session) return PXA_STATUS_CANCELLED;
    uint32_t maximum=pxa_lvgl_ui_text_input_max_bytes(adapter,editor);
    if (bytes>maximum || bytes>PXA_UI_EVENT_TEXT_MAX_BYTES || strlen(text)!=bytes)
        return PXA_STATUS_LIMIT_EXCEEDED;
    /* The trusted bridge supplies valid, complete UTF-8; the UI service also
     * validates the event before it can reach Guest memory. */
    if (strcmp(lv_textarea_get_text(editor),text)) lv_textarea_set_text(editor,text);
    lv_textarea_set_cursor_pos(editor,(int32_t)cursor);
    if (action) {
        lv_obj_t *target=editor;
        lv_obj_remove_state(target,LV_STATE_FOCUSED);
        text_focus(NULL,target,false);
        if (action==1) lv_obj_send_event(target,LV_EVENT_READY,NULL);
    }
    return PXA_STATUS_OK;
}

/* SPDX-License-Identifier: MIT */
/* Real LVGL differential fixtures and repeatable Canvas microbenchmarks. */
#undef NDEBUG
#include "pxa/lvgl/pxa_lvgl_ui.h"
#include "pxa/wire.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WIDTH = 320, HEIGHT = 240, PIXEL_BYTES = 2, FRAME_CAPACITY = 32768 };
typedef enum { PARTIAL_STATIC, FULL_STATIC, PARTIAL_MOVING, FULL_MOVING } workload_t;
typedef struct { uint8_t data[FRAME_CAPACITY]; size_t size; } frame_t;
typedef struct { size_t bytes, allocations, large, rejected; int reference; } memory_t;
static uint8_t screen[HEIGHT][WIDTH * PIXEL_BYTES];
static uint64_t flushed_pixels;
static pxa_asset_object_t *asset;
static unsigned releases, acquires;

static uint64_t now_ns(void) {
    struct timespec t;
    /* LVGL draws synchronously here. Exclude host scheduler delays so other
     * tasks on the machine do not dominate the short Canvas measurements. */
    assert(!clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t));
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static void *allocate(void *context, size_t bytes) {
    memory_t *m = context;
    /* Optional index allocations fail; required node/image/clip allocations in
     * these fixtures fit below this bound. Exercise the production fallback. */
    if (m && m->reference && bytes >= 2048) { ++m->rejected; return NULL; }
    size_t *p = malloc(sizeof(*p) + bytes);
    if (!p) return NULL;
    *p = bytes;
    if (m) { m->bytes += bytes; ++m->allocations; if (bytes >= 2048) ++m->large; }
    return p + 1;
}
static void release(void *context, void *data) {
    if (!data) return;
    size_t *p = (size_t *)data - 1;
    if (context) { memory_t *m = context; assert(m->bytes >= *p); m->bytes -= *p; }
    free(p);
}
static pxa_status_t execute(pxa_lvgl_ui_execute_callback_fn fn, void *data, void *context) {
    (void)context; fn(data); return PXA_STATUS_OK;
}
static void release_frame(void *context, void *data) { (void)context; (void)data; ++releases; }
static pxa_status_t acquire_image(pxa_component_t component, pxa_handle64_t handle,
    pxa_asset_object_t **out, void *context) {
    (void)component; (void)context;
    if (handle != 7) return PXA_STATUS_NOT_FOUND;
    ++acquires; pxa_asset_object_retain(asset); *out = asset; return PXA_STATUS_OK;
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    size_t width = (size_t)lv_area_get_width(area);
    size_t stride = lv_draw_buf_width_to_stride((uint32_t)width, LV_COLOR_FORMAT_RGB565);
    assert(area->x1 >= 0 && area->y1 >= 0 && area->x2 < WIDTH && area->y2 < HEIGHT);
    for (int32_t y = area->y1; y <= area->y2; ++y)
        memcpy(&screen[y][area->x1 * PIXEL_BYTES], pixels + (size_t)(y-area->y1)*stride,
               width * PIXEL_BYTES);
    flushed_pixels += width * (size_t)lv_area_get_height(area);
    lv_display_flush_ready(display);
}
static uint8_t *record(frame_t *frame, uint8_t type, uint16_t length) {
    assert(frame->size + length + 4 <= sizeof(frame->data));
    uint8_t *p = frame->data + frame->size;
    memset(p, 0, length + 4); p[0] = type; pxa_write_u16(p+2, length);
    frame->size += length + 4;
    return p + 4;
}
static void box(uint8_t *p, int x, int y, unsigned w, unsigned h) {
    pxa_write_u32(p, (uint32_t)x); pxa_write_u32(p+4, (uint32_t)y);
    pxa_write_u32(p+8, w); pxa_write_u32(p+12, h);
}
static void rectangle(frame_t *f, int x, int y, unsigned w, unsigned h, uint32_t color) {
    uint8_t *p = record(f, PXA_UI_CANVAS_RECT, 28);
    box(p,x,y,w,h); pxa_write_u32(p+16,color);
}
static void ellipse(frame_t *f, int x, int y, unsigned w, unsigned h, uint32_t color,
                    uint16_t border_width, uint32_t border) {
    uint8_t *p = record(f, PXA_UI_CANVAS_ELLIPSE, 26);
    box(p, x, y, w, h);
    pxa_write_u32(p+16, color);
    pxa_write_u16(p+20, border_width);
    pxa_write_u32(p+22, border);
}

static void scene(frame_t *f, unsigned kind, unsigned revision, workload_t workload) {
    f->size = 0;
    rectangle(f,0,0,320,400,0x102030ff);
    /* More than one word of candidates; arbitrary paint order and overlapping
     * translucent shapes ensure an index cannot sort the draw stream. */
    for (unsigned i = 0; i < 192; ++i) {
        int x = (int)(i * 47 % 350) - 15, y = (int)(i * 29 % 430) - 20;
        if (workload == FULL_MOVING) x += (int)(revision & 1) * 6;
        if (kind == 1) {
            uint8_t *p = record(f,PXA_UI_CANVAS_LINE,22);
            box(p,x,y,(uint32_t)(x+37),(uint32_t)(y+3));
            pxa_write_u32(p+16,0x38a0ff90 + (i << 16)); pxa_write_u16(p+20,3+i%7);
        } else if (kind == 0 && i % 5 == 0) {
            ellipse(f, x, y, 23, 13, 0x50a08090, 2, 0xffa050ff);
        } else {
            rectangle(f,x,y,17+i%11,8+i%13,0x50907080 + (i << 16));
        }
        if (kind == 2 && i == 35) {
            for (unsigned d=0;d<12;++d) box(record(f,PXA_UI_CANVAS_CLIP_PUSH,16),
                (int)d,(int)d,300-d*2,220-d*2);
        }
        if (kind == 2 && i == 145)
            for (unsigned d=0;d<12;++d) record(f,PXA_UI_CANVAS_CLIP_POP,0);
        if (kind == 2 && (i == 48 || i == 150)) {
            uint8_t *p = record(f,PXA_UI_CANVAS_IMAGE_HANDLE,26);
            box(p,i==48?5000:40,i==48?5000:35,21,15);
            p[16]=170; p[17]=PXA_UI_IMAGE_FIT_STRETCH; pxa_write_u64(p+18,7);
            p = record(f,PXA_UI_CANVAS_BITMAP_RGB565,28);
            box(p,i==48?5000:75,i==48?5000:40,2,2); pxa_write_u32(p+16,4);
            pxa_write_u16(p+20,0xf800); pxa_write_u16(p+22,0x07e0);
            pxa_write_u16(p+24,0x001f); pxa_write_u16(p+26,0xffff);
        }
        if (kind == 3 && i % 12 == 0) {
            uint8_t *p = record(f,PXA_UI_CANVAS_ARC,22);
            box(p,x,y,19,0); pxa_write_u16(p+12,35); pxa_write_u16(p+14,280);
            pxa_write_u32(p+16,0xffa030ff); pxa_write_u16(p+20,4);
        }
    }
    /* Reversed/empty clips must restore their enclosing scope correctly. */
    box(record(f,PXA_UI_CANVAS_CLIP_PUSH,16),20,20,20,20);
    box(record(f,PXA_UI_CANVAS_CLIP_PUSH,16),200,200,10,10);
    rectangle(f,0,0,320,240,0xff0000ff);
    record(f,PXA_UI_CANVAS_CLIP_POP,0);
    rectangle(f,25,25,10,10,0x40ff4090);
    record(f,PXA_UI_CANVAS_CLIP_POP,0);
    if (workload == PARTIAL_MOVING)
        rectangle(f, 70 + (int)(revision & 1) * 4, 22, 18, 4, 0x4080ffff);
    const char *label = revision & 1 ? "Value 123" : "Value 456";
    uint8_t *p = record(f,PXA_UI_CANVAS_TEXT_BOX,33);
    box(p,2,2,110,24); pxa_write_u32(p+16,0xffffffff);
    p[20]=1; p[21]=1; p[22]=1; memcpy(p+24,label,9);
}
typedef struct {
    memory_t memory;
    void *workspace, *node, *root_node;
    pxa_lvgl_ui_t *ui;
    pxa_ui_backend_t backend;
    pxa_ui_environment_t environment;
    lv_obj_t *root, *object;
} canvas_fixture_t;

static void setup_canvas(canvas_fixture_t *fixture, int reference, uint32_t density) {
    fixture->memory.reference = reference;
    pxa_lvgl_ui_config_t config = {0};
    config.struct_size=sizeof(config); config.allocate=allocate; config.release=release;
    config.allocator_context=&fixture->memory; config.execute=execute; config.acquire_image=acquire_image;
    config.primary_environment=(pxa_ui_environment_t){.surface=1,.width=WIDTH,.height=HEIGHT,
        .density_q16=density,.font_scale_q16=65536};
    pxa_lvgl_ui_theme_init(&config.theme);
    config.theme.body_font=&lv_font_montserrat_14;
    fixture->environment = config.primary_environment;
    void *workspace=malloc(pxa_lvgl_ui_workspace_size()), *transaction=NULL, *handles[2]={0};
    pxa_lvgl_ui_t *ui; pxa_ui_backend_t backend;
    assert(workspace && !pxa_lvgl_ui_init(workspace,pxa_lvgl_ui_workspace_size(),&config,&ui,&backend));
    pxa_ui_transaction_info_t info={.surface=1,.generation=1,.kind=PXA_UI_REPLACE_SURFACE};
    assert(!backend.begin(backend.context,&info,&transaction));
    for (unsigned i=0;i<2;++i) {
        pxa_ui_command_view_t c={.command=PXA_UI_COMMAND_CREATE,.node=i+1,.parent=i,
            .parent_handle=i?handles[0]:NULL,.type=i?PXA_UI_NODE_CANVAS:PXA_UI_NODE_ROOT};
        assert(!backend.apply(backend.context,transaction,&c,&handles[i]));
    }
    assert(!backend.commit(backend.context,transaction));
    lv_obj_t *root=lv_obj_get_child(lv_screen_active(),0), *canvas=lv_obj_get_child(root,0);
    lv_obj_set_layout(root,LV_LAYOUT_NONE); lv_obj_set_style_pad_all(root,0,0);
    lv_obj_set_size(root,WIDTH,HEIGHT); lv_obj_set_pos(root,0,0);
    lv_obj_set_size(canvas,WIDTH,400); lv_obj_set_pos(canvas,0,0);
    lv_obj_update_layout(root);
    fixture->workspace=workspace; fixture->node=handles[1]; fixture->root_node=handles[0];
    fixture->ui=ui; fixture->backend=backend;
    fixture->root=root; fixture->object=canvas;
}

static void finish_canvas(canvas_fixture_t *fixture) {
    pxa_lvgl_ui_deinit(fixture->ui);
    free(fixture->workspace);
    assert(fixture->memory.bytes == 0);
}

static void render_case(lv_display_t *display, unsigned kind, uint32_t density,
    workload_t workload, int reference, uint8_t *expected, unsigned iterations) {
    canvas_fixture_t fixture = {0};
    setup_canvas(&fixture,reference,density);
    pxa_ui_backend_t backend=fixture.backend;
    lv_obj_t *canvas=fixture.object, *root=fixture.root;
    frame_t frames[2];
    scene(&frames[0],kind,0,workload); scene(&frames[1],kind,1,workload);
    pxa_ui_canvas_view_t view={.surface=1,.node=2,.node_handle=fixture.node};
    uint64_t submit_ns=0, draw_ns=0;
    size_t warm_allocations=0;
    unsigned release_before=releases;
    for (unsigned i=0;i<iterations;++i) {
        /* Alternate content after warmup, with or without geometry changes. */
        view.display_list=(pxa_bytes_t){frames[i%2].data,frames[i%2].size}; view.frame=i+1;
        bool full = workload == FULL_STATIC || workload == FULL_MOVING;
        view.dirty_count=i>1 && (!full || i%2)?1:0;
        view.dirty_rects[0][0]=0; view.dirty_rects[0][1]=0;
        view.dirty_rects[0][2]=full?WIDTH:120; view.dirty_rects[0][3]=full?400:28;
        uint64_t before=now_ns();
        assert(!backend.present_canvas(backend.context,&view,release_frame,NULL));
        if(i>=4) submit_ns+=now_ns()-before;
        before=now_ns(); lv_refr_now(display);
        if(i>=4) draw_ns+=now_ns()-before;
        if(i==3) warm_allocations=fixture.memory.allocations;
        if(i>3 && !reference) assert(fixture.memory.allocations==warm_allocations);
        /* Capture and compare every completed frame, not a single final hash. */
        if(reference) memcpy(expected+(size_t)i*sizeof(screen),screen,sizeof(screen));
        else assert(!memcmp(expected+(size_t)i*sizeof(screen),screen,sizeof(screen)));
    }
    size_t retained_bytes=fixture.memory.bytes;
    pxa_lvgl_ui_canvas_index_stats_t stats;
    assert(!pxa_lvgl_ui_canvas_index_stats(fixture.ui,&stats));
#ifdef PXA_CANVAS_TEST_REFERENCE_ONLY
    assert(!stats.builds && !stats.indexed_draws && !fixture.memory.rejected);
#else
    if(workload==PARTIAL_STATIC) {
        if(reference) {
            assert(!stats.builds && stats.allocation_fallbacks && fixture.memory.rejected);
        } else {
            assert(stats.builds==1 && stats.reuses && stats.indexed_draws);
            assert(stats.current_bytes && stats.peak_bytes==stats.current_bytes);
            assert(stats.candidate_commands && stats.skipped_commands);
        }
    } else {
        assert(!stats.builds && !stats.allocation_fallbacks && !stats.current_bytes);
        assert(stats.streaming_draws && !stats.indexed_draws && !fixture.memory.large);
    }
#endif
    /* Repaint the same frame after moving it and after changing density without
     * resubmitting commands. The latter must take the reference parser. */
    for(unsigned state=0;state<5;++state) {
        if(state==0) lv_obj_set_y(canvas,-70);
        if(state==1) {
            fixture.environment.density_q16=density+16384;
            assert(!backend.environment_changed(backend.context,&fixture.environment));
        }
        if(state==2) { lv_obj_set_size(canvas,280,350); lv_obj_set_pos(canvas,11,-18); }
        if(state==3) {
            frame_t *changed=&frames[(iterations-1)%2 ^ 1u];
            scene(changed,(kind+1)%4,1,workload);
            view.display_list=(pxa_bytes_t){changed->data,changed->size};
            view.frame++; view.dirty_count=0;
            assert(!backend.present_canvas(backend.context,&view,release_frame,NULL));
            pxa_lvgl_ui_canvas_index_stats_t after;
            assert(!pxa_lvgl_ui_canvas_index_stats(fixture.ui,&after));
            assert(!after.current_bytes);
        }
        if(state==4) {
            view.display_list=(pxa_bytes_t){NULL,0}; view.frame++;
            assert(!backend.present_canvas(backend.context,&view,release_frame,NULL));
        }
        lv_obj_invalidate(root); lv_refr_now(display);
        if(reference) memcpy(expected+(size_t)(iterations+state)*sizeof(screen),screen,sizeof(screen));
        else assert(!memcmp(expected+(size_t)(iterations+state)*sizeof(screen),screen,sizeof(screen)));
    }
    printf("CANVAS_BENCH {\"workload\":%d,\"scene\":%u,\"density\":%u,\"reference\":%s,\"frames\":%u,"
        "\"submit_ns\":%llu,\"draw_ns\":%llu,\"retained_bytes\":%zu,\"index_bytes\":%zu,"
        "\"index_builds\":%llu,\"index_reuses\":%llu,\"indexed_draws\":%llu,\"skipped_commands\":%llu}\n",
        (int)workload,kind,density,reference?"true":"false",iterations-4,
        (unsigned long long)submit_ns,(unsigned long long)draw_ns,retained_bytes,stats.current_bytes,
        (unsigned long long)stats.builds,(unsigned long long)stats.reuses,
        (unsigned long long)stats.indexed_draws,(unsigned long long)stats.skipped_commands);
    finish_canvas(&fixture);
    /* The empty display list owns no bytes and needs no release callback. */
    assert(releases-release_before==iterations+1);
}

#ifndef PXA_CANVAS_TEST_REFERENCE_ONLY
static uint16_t rgb565_at(unsigned x, unsigned y) {
    uint16_t pixel;
    memcpy(&pixel, &screen[y][x * PIXEL_BYTES], sizeof(pixel));
    return pixel;
}

static void verify_ellipse(lv_display_t *display) {
    canvas_fixture_t fixture = {0};
    setup_canvas(&fixture,0,65536);
    frame_t frame = {0};
    rectangle(&frame,0,0,WIDTH,HEIGHT,0x000000ff);
    ellipse(&frame,20,24,80,40,0xff0000ff,4,0x0000ffff);
    ellipse(&frame,120,24,80,40,0xff000080,4,0x0000ff80);
    ellipse(&frame,22,96,40,80,0x00ff00ff,0,0);
    box(record(&frame,PXA_UI_CANVAS_CLIP_PUSH,16),150,112,20,32);
    ellipse(&frame,122,96,80,64,0xffffffff,0,0);
    record(&frame,PXA_UI_CANVAS_CLIP_POP,0);
    ellipse(&frame,210,24,80,40,0x00000000,4,0x00ffffff);
    ellipse(&frame,-100000,214,200000,40,0xe0ff00ff,0,0);
    pxa_ui_canvas_view_t view = {.surface=1,.node=2,.frame=1,.node_handle=fixture.node,
        .display_list={frame.data,frame.size}};
    assert(!fixture.backend.present_canvas(fixture.backend.context,&view,release_frame,NULL));
    lv_refr_now(display);

    /* In these top/left rows the old capsule had a straight, colored edge. */
    assert(rgb565_at(48,24)==0 && rgb565_at(71,24)==0);
    assert(rgb565_at(60,24)==0x001f && rgb565_at(60,44)==0xf800);
    assert(rgb565_at(22,120)==0 && rgb565_at(22,135)==0x07e0);
    uint16_t edge = rgb565_at(68,24);
    assert(edge > 0 && edge < 0x001f && edge == rgb565_at(51,24));

    uint16_t center = rgb565_at(160,44), stroke = rgb565_at(160,24);
    assert((center >> 11) >= 15 && (center >> 11) <= 16 && !(center & 0x07ff));
    assert((stroke >> 11) >= 7 && (stroke >> 11) <= 8 &&
           (stroke & 31) >= 15 && (stroke & 31) <= 16);
    assert(rgb565_at(160,125)==0xffff && rgb565_at(149,125)==0 &&
           rgb565_at(170,125)==0 && rgb565_at(160,111)==0 && rgb565_at(160,144)==0);
    assert(rgb565_at(250,24)==0x07ff && rgb565_at(250,44)==0);
    assert(rgb565_at(0,234)!=0 && rgb565_at(WIDTH-1,234)==rgb565_at(0,234));
    finish_canvas(&fixture);
}

static void verify_index_budget(void) {
    canvas_fixture_t fixture = {0};
    setup_canvas(&fixture,0,65536);
    pxa_ui_transaction_info_t info = {.surface=1,.generation=2,.kind=PXA_UI_PATCH};
    pxa_ui_backend_t backend = fixture.backend;
    void *transaction, *second_handle;
    assert(!backend.begin(backend.context,&info,&transaction));
    pxa_ui_command_view_t command = {.command=PXA_UI_COMMAND_CREATE,.node=3,.parent=1,
        .parent_handle=fixture.root_node,.type=PXA_UI_NODE_CANVAS};
    assert(!backend.apply(backend.context,transaction,&command,&second_handle));
    assert(!backend.commit(backend.context,transaction));
    lv_obj_t *second = lv_obj_get_child(fixture.root,1);
    lv_obj_set_size(second,WIDTH,400);
    lv_obj_update_layout(fixture.root);

    frame_t frame = {0};
    for (unsigned i=0;i<800;++i)
        rectangle(&frame,(int)(i%300),(int)(i%220),5,5,0xff0000ff);
    pxa_ui_canvas_view_t first = {.surface=1,.node=2,.node_handle=fixture.node,.dirty_count=1,
        .dirty_rects={{0,0,10,10}},.display_list={frame.data,frame.size}};
    pxa_ui_canvas_view_t other = first;
    other.node = 3;
    other.node_handle = second_handle;
    for (unsigned i=0;i<2;++i) {
        first.frame = other.frame = i+1;
        assert(!backend.present_canvas(backend.context,&first,release_frame,NULL));
        assert(!backend.present_canvas(backend.context,&other,release_frame,NULL));
    }
    pxa_lvgl_ui_canvas_index_stats_t stats;
    assert(!pxa_lvgl_ui_canvas_index_stats(fixture.ui,&stats));
    assert(stats.builds==1 && stats.allocation_fallbacks==1);
    assert(stats.current_bytes>32*1024 && stats.current_bytes<=64*1024 &&
           stats.peak_bytes==stats.current_bytes);

    first.frame++;
    first.dirty_count = 0;
    assert(!backend.present_canvas(backend.context,&first,release_frame,NULL));
    assert(!pxa_lvgl_ui_canvas_index_stats(fixture.ui,&stats) && !stats.current_bytes);
    for (unsigned i=0;i<5;++i) {
        other.frame++;
        assert(!backend.present_canvas(backend.context,&other,release_frame,NULL));
    }
    assert(!pxa_lvgl_ui_canvas_index_stats(fixture.ui,&stats));
    assert(stats.builds==2 && stats.current_bytes==stats.peak_bytes && stats.reuses);
    finish_canvas(&fixture);
}
#endif

int main(void) {
    static uint8_t draw_buffer[sizeof(screen)];
    unsigned iterations=getenv("PXA_CANVAS_BENCHMARK")?200:8;
    uint8_t *expected=malloc((iterations+5)*sizeof(screen)); assert(expected);
    lv_init(); lv_display_t *display=lv_display_create(WIDTH,HEIGHT); assert(display);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display,flush);
    lv_display_set_buffers(display,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    pxa_asset_info_t asset_info={.kind=PXA_ASSET_IMAGE,.encoding=PXA_ASSET_ENCODING_RGB565,
        .width=2,.height=2,.format_version=1,.payload_offset=32,.stored_bytes=40,.decoded_bytes=8};
    uint8_t *pixels; assert(!pxa_asset_object_create(&asset_info,allocate,release,NULL,&asset,&pixels));
    for(unsigned i=0;i<4;++i) pxa_write_u16(pixels+i*2,(uint16_t)(0x07e0+i*1024));
    pxa_asset_object_finish_loading(asset);
    for(workload_t workload=PARTIAL_STATIC;workload<=FULL_MOVING;++workload)
      for(unsigned kind=0;kind<4;++kind)
        for(unsigned d=0;d<2;++d) {
            render_case(display,kind,d?98304:65536,workload,1,expected,iterations);
#ifndef PXA_CANVAS_TEST_REFERENCE_ONLY
            render_case(display,kind,d?98304:65536,workload,0,expected,iterations);
#endif
            assert(pxa_asset_object_reference_count(asset)==1);
        }
#ifndef PXA_CANVAS_TEST_REFERENCE_ONLY
    verify_ellipse(display);
    verify_index_budget();
#endif
    pxa_asset_object_release(asset); free(expected);
    assert(acquires>0 && flushed_pixels>0);
    lv_display_delete(display); lv_deinit();
    puts("Canvas: byte-identical frames, nested clips, image indices, fractional DPI, movement, resize, density fallback and zero warm allocations OK");
    return 0;
}

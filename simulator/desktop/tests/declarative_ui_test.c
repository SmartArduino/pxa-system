/* Signed C++ AOT, actual Host transactions, LVGL controls and clock tasks. */
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include <assert.h>

typedef struct {
    product_host_t *host;
    const char *artifacts;
    unsigned stage, profile;
    uint64_t started, settle_at;
} declarative_test_t;
typedef struct { lv_point_t point; bool pressed; } pointer_probe_t;
static lv_obj_t *find_label(lv_obj_t *object, const char *text);
static lv_obj_t *find_switch(lv_obj_t *object);
static void read_probe(lv_indev_t *input, lv_indev_data_t *data) {
    pointer_probe_t *probe=lv_indev_get_user_data(input);
    data->point=probe->point; data->timestamp=lv_tick_get();
    data->state=probe->pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
static void check_locked_switch(declarative_test_t *test) {
    lv_obj_t *label=find_label(lv_screen_active(),"Locked"); assert(label);
    lv_obj_t *object=find_switch(lv_obj_get_parent(label)); assert(object);
    assert(lv_obj_has_state(object,LV_STATE_DISABLED));
    lv_obj_scroll_to_view_recursive(object,LV_ANIM_OFF); lv_obj_update_layout(lv_screen_active());
    lv_area_t area; lv_obj_get_coords(object,&area);
    pointer_probe_t probe={{(area.x1+area.x2)/2,(area.y1+area.y2)/2},true};
    lv_indev_t *input=lv_indev_create(); assert(input);
    lv_indev_set_type(input,LV_INDEV_TYPE_POINTER); lv_indev_set_display(input,lv_display_get_default());
    lv_indev_set_user_data(input,&probe); lv_indev_set_read_cb(input,read_probe);
    lv_indev_read(input); probe.pressed=false; lv_indev_read(input);
    dispatch_component_events(test->host);
    assert(!lv_obj_has_state(object,LV_STATE_CHECKED)); lv_indev_delete(input);
}
static lv_obj_t *find_label(lv_obj_t *object, const char *text) {
    if (lv_obj_check_type(object,&lv_label_class) && !strcmp(lv_label_get_text(object),text)) return object;
    for (uint32_t i=0;i<lv_obj_get_child_count(object);++i) {
        lv_obj_t *found=find_label(lv_obj_get_child(object,i),text); if (found) return found;
    }
    return NULL;
}
static lv_obj_t *find_switch(lv_obj_t *object) {
    if (lv_obj_check_type(object,&lv_switch_class)) return object;
    for (uint32_t i=0;i<lv_obj_get_child_count(object);++i) {
        lv_obj_t *found=find_switch(lv_obj_get_child(object,i)); if (found) return found;
    }
    return NULL;
}
static lv_obj_t *button(const char *text) {
    lv_obj_t *label=find_label(lv_screen_active(),text); assert(label);
    lv_obj_t *object=lv_obj_get_parent(label); assert(lv_obj_check_type(object,&lv_button_class));
    return object;
}
static void click(declarative_test_t *test,const char *text) {
    lv_obj_t *object=button(text); assert(!lv_obj_has_state(object,LV_STATE_DISABLED));
    lv_obj_scroll_to_view_recursive(object,LV_ANIM_OFF);
    lv_obj_send_event(object,LV_EVENT_CLICKED,NULL); dispatch_component_events(test->host);
}
static void toggle(declarative_test_t *test,const char *text,bool enabled) {
    lv_obj_t *label=find_label(lv_screen_active(),text); assert(label);
    lv_obj_t *object=find_switch(lv_obj_get_parent(label)); assert(object);
    if (enabled) lv_obj_add_state(object,LV_STATE_CHECKED); else lv_obj_remove_state(object,LV_STATE_CHECKED);
    lv_obj_send_event(object,LV_EVENT_VALUE_CHANGED,NULL); dispatch_component_events(test->host);
}
static void bind_runner(void *context,void (*focus)(void *,bool),
                        void (*exit_app)(void *),void *runner) {
    (void)focus; (void)exit_app; ((declarative_test_t *)context)->host=runner;
}
static void capture(declarative_test_t *test) {
    static uint16_t baseline_height;
    if (!test->profile) baseline_height=test->host->body_font->line_height;
    if (test->profile==1) assert(test->host->body_font->line_height>baseline_height*3/2);
    lv_obj_t *title=find_label(lv_screen_active(),"UI Workshop"); assert(title);
    lv_obj_t *enabled_label=find_label(lv_screen_active(),"Enabled"); assert(enabled_label);
    assert(lv_obj_has_state(find_switch(lv_obj_get_parent(enabled_label)),LV_STATE_CHECKED));
    assert(lv_tick_get()>0); // Repeat launches must reinstall the SDL tick source.
    lv_obj_scroll_to_view_recursive(title,LV_ANIM_OFF);
    lv_obj_update_layout(test->host->content_parent);
    lv_area_t area; lv_obj_get_coords(title,&area);
    if (test->profile==2) assert(area.x1>=67 && area.y1>=67);
    if (test->profile==3) assert(area.x1>=32 && area.y1>=18);
    // Capture the display buffer, including the actual system-layer shape mask.
    lv_display_t *display=lv_display_get_default(); lv_refr_now(display);
    lv_draw_buf_t *buffer=lv_display_get_buf_active(display); assert(buffer && buffer->data);
    const unsigned width=lv_display_get_horizontal_resolution(display);
    const unsigned height=lv_display_get_vertical_resolution(display), stride=width*4;
    const unsigned source_stride=lv_draw_buf_width_to_stride(width,lv_display_get_color_format(display));
    assert(buffer->data_size>=(size_t)source_stride*height);
    const size_t size=(size_t)stride*height; uint8_t *pixels=malloc(size); assert(pixels);
    assert(!SDL_ConvertPixels(width,height,SDL_PIXELFORMAT_RGB888,buffer->data,(int)source_stride,
                              SDL_PIXELFORMAT_BGRA32,pixels,(int)stride));
    char path[2048]; int n=snprintf(path,sizeof(path),"%s/profile-%u.bgra",test->artifacts,test->profile);
    assert(n>0 && (size_t)n<sizeof(path)); FILE *file=fopen(path,"wb");
    assert(file && fwrite(pixels,1,size,file)==size && !fclose(file)); free(pixels);
    n=snprintf(path,sizeof(path),"%s/profile-%u.json",test->artifacts,test->profile);
    assert(n>0 && (size_t)n<sizeof(path)); file=fopen(path,"w"); assert(file);
    pxa_wamr_memory_snapshot_t memory={0};
    assert(pxa_wamr_engine_memory_snapshot(test->host->engine,&memory)==PXA_STATUS_OK);
    fprintf(file,"{\"width\":%u,\"height\":%u,\"stride\":%u,\"title_x\":%d,\"title_y\":%d,\"body_line_height\":%u,"
        "\"wamr_current_bytes\":%u,\"wamr_peak_bytes\":%u,\"linear_current_bytes\":%llu,\"linear_peak_bytes\":%llu,"
        "\"artifact_buffer_bytes\":%llu,\"event_buffer_bytes\":%llu}\n",
        width,height,stride,area.x1,area.y1,test->host->body_font->line_height,memory.current_bytes,memory.peak_bytes,
        (unsigned long long)memory.linear_current_bytes,(unsigned long long)memory.linear_peak_bytes,
        (unsigned long long)memory.artifact_buffer_bytes,(unsigned long long)memory.event_buffer_bytes);
    assert(!fclose(file));
}
static void pump(void *context) {
    declarative_test_t *test=context; product_host_t *host=test->host; if (!host) return;
    assert(now_us(NULL)-test->started<UINT64_C(20000000));
    lv_obj_t *root=lv_screen_active();
    if (!test->stage) {
        if (!find_label(root,"UI Workshop") || !find_label(root,"Count: 0")) return;
        lv_obj_t *switch_object=find_switch(root); assert(switch_object);
        assert(lv_obj_get_style_bg_opa(switch_object,LV_PART_MAIN)==LV_OPA_COVER);
        assert(lv_obj_get_style_radius(switch_object,LV_PART_MAIN)==LV_RADIUS_CIRCLE);
        click(test,"+ local"); toggle(test,"Enabled",false); test->stage=1;
    } else if (test->stage==1) {
        assert(find_label(root,"Paused: 0") && find_label(root,"1"));
        assert(lv_obj_has_state(button("Add"),LV_STATE_DISABLED));
        toggle(test,"Enabled",true); click(test,"Add"); test->stage=2;
    } else if (test->stage==2) {
        assert(find_label(root,"Count: 1")); click(test,"Again"); test->stage=3;
    } else if (test->stage==3) {
        assert(find_label(root,"Count: 2")); click(test,"Async");
        test->stage=4;
    } else if (test->stage==4) {
        if (!find_label(root,"Async complete")) return;
        assert(find_label(root,"Count: 3") && find_label(root,"1"));
        product_focus_changed(host,false); dispatch_lifecycle(host);
        assert(find_label(root,"Background"));
        product_focus_changed(host,true); dispatch_lifecycle(host);
        assert(find_label(root,"Ready")); test->stage=5;
    } else if (test->stage==5) {
        assert(find_label(root,"Count: 3")); toggle(test,"Details",true); test->stage=6;
    } else if (test->stage==6) {
        assert(find_label(root,"Details shown") && find_label(root,"0.375")); click(test,"Fail");
        assert(find_label(root,"Action error handled")); click(test,"+ local"); test->stage=7;
    } else if (test->stage==7) {
        assert(find_label(root,"Count: 3") && find_label(root,"2"));
        toggle(test,"Details",false); test->stage=8;
    } else if (test->stage==8) {
        assert(find_label(root,"Details hidden") && find_label(root,"Action error handled"));
        check_locked_switch(test);
        lv_obj_scroll_to_view_recursive(find_label(root,"UI Workshop"),LV_ANIM_OFF);
        test->settle_at=now_us(NULL)+UINT64_C(250000); test->stage=9;
    } else if (test->stage==9 && now_us(NULL)>=test->settle_at) {
        capture(test); test->stage=10; host->exit_requested=1;
    }
}
int main(int argc,char **argv) {
    assert(argc==5); setenv("SDL_VIDEODRIVER","dummy",1); setenv("SDL_AUDIODRIVER","dummy",1);
    for (unsigned profile=0;profile<5;++profile) {
        options_t options={0}; options.package_path=argv[1]; options.publisher_key=argv[2];
        options.state_root=argv[3]; options.locale="en-US";
        options.width=profile==1?480:profile==2?454:profile==3?320:profile==4?200:240;
        options.height=profile==1?640:profile==2?454:profile==3?240:profile==4?200:320;
        options.density_dpi=profile==1?305:160;
        if (profile==2) {
            options.display_shape=PXA_UI_DISPLAY_SHAPE_CIRCLE;
            for (unsigned i=0;i<4;++i) options.safe_insets[i]=67;
        } else if (profile==3) {
            options.display_shape=PXA_UI_DISPLAY_SHAPE_ROUNDED_RECTANGLE; options.corner_radius=24;
            options.safe_insets[0]=18; options.safe_insets[1]=8;
            options.safe_insets[2]=12; options.safe_insets[3]=32;
        }
        declarative_test_t test={0}; test.started=now_us(NULL); test.profile=profile; test.artifacts=argv[4];
        assert(!run_product_simulator(&options,NULL,NULL,pump,&test,NULL,&test,bind_runner,NULL,NULL,NULL));
        assert(!test.host && test.stage==10);
    }
    puts("Declarative UI AOT: bindings/disabled controls, coalesced text, stable component, conditional UI, action errors, async actions/focus, 5 display profiles passed");
    return 0;
}

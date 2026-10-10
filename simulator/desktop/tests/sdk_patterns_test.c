/* Real signed C++ AOT, actual LVGL actions and filesystem/storage backends. */
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include <assert.h>

typedef struct {
    product_host_t *host;
    const char *artifacts;
    unsigned stage, repeat;
    uint64_t started;
} patterns_test_t;
static lv_obj_t *label(lv_obj_t *object, const char *text) {
    if (lv_obj_check_type(object,&lv_label_class) && !strcmp(lv_label_get_text(object),text)) return object;
    for (uint32_t i=0;i<lv_obj_get_child_count(object);++i) {
        lv_obj_t *found=label(lv_obj_get_child(object,i),text); if (found) return found;
    }
    return NULL;
}
static void click(patterns_test_t *test,const char *text) {
    lv_obj_t *target=label(lv_screen_active(),text); assert(target);
    lv_obj_t *button=lv_obj_get_parent(target);
    assert(lv_obj_check_type(button,&lv_button_class));
    lv_obj_send_event(button,LV_EVENT_CLICKED,NULL);
    dispatch_component_events(test->host);
}
static void bind_test(void *context,void (*focus)(void *,bool),
                      void (*exit_app)(void *),void *runner) {
    (void)focus; (void)exit_app; ((patterns_test_t *)context)->host=runner;
}
static void capture(patterns_test_t *test) {
    static uint16_t baseline_height;
    assert(test->host->body_font);
    if (!test->repeat) baseline_height=test->host->body_font->line_height;
    if (test->repeat==1) assert(test->host->body_font->line_height>baseline_height*3/2);
    lv_obj_t *title=label(lv_screen_active(),"SDK patterns"); assert(title);
    lv_obj_update_layout(test->host->content_parent);
    lv_area_t area; lv_obj_get_coords(title,&area);
    if (test->repeat==2) assert(area.x1>=67 && area.y1>=67);
    lv_draw_buf_t *snapshot=lv_snapshot_take(test->host->content_parent,LV_COLOR_FORMAT_ARGB8888);
    assert(snapshot);
    char path[2048];
    int n=snprintf(path,sizeof(path),"%s/profile-%u.bgra",test->artifacts,test->repeat);
    assert(n>0 && (size_t)n<sizeof(path));
    FILE *file=fopen(path,"wb");
    size_t size=(size_t)snapshot->header.stride*snapshot->header.h;
    assert(file && fwrite(snapshot->data,1,size,file)==size && !fclose(file));
    n=snprintf(path,sizeof(path),"%s/profile-%u.json",test->artifacts,test->repeat);
    assert(n>0 && (size_t)n<sizeof(path)); file=fopen(path,"w"); assert(file);
    fprintf(file,"{\"width\":%u,\"height\":%u,\"stride\":%u}\n",
        snapshot->header.w,snapshot->header.h,snapshot->header.stride);
    assert(!fclose(file)); lv_draw_buf_destroy(snapshot);
}
static void pump_test(void *context) {
    patterns_test_t *test=context; product_host_t *host=test->host; if (!host) return;
    assert(now_us(NULL)-test->started<UINT64_C(15000000));
    lv_obj_t *root=lv_screen_active(); char count[16];
    snprintf(count,sizeof(count),"%u",test->repeat+1);
    if (!test->stage) {
        if (!label(root,"SDK patterns") || !label(root,test->repeat?"Loaded":"New save")) return;
        char previous[16]; snprintf(previous,sizeof(previous),"%u",test->repeat);
        assert(label(root,previous)); click(test,"Add and save"); test->stage=1;
    } else if (test->stage==1) {
        if (!label(root,"Saved") || !label(root,count)) return;
        click(test,"File round trip"); test->stage=2;
    } else if (test->stage==2) {
        if (!label(root,"File round trip OK")) return;
        click(test,"Fail task"); test->stage=3;
    } else if (test->stage==3) {
        if (!label(root,"Task error handled")) return;
        // A task error must preserve the UI, callback ownership and resources.
        product_focus_changed(host,false); dispatch_lifecycle(host);
        product_focus_changed(host,true); dispatch_lifecycle(host);
        click(test,"File round trip"); test->stage=4;
    } else if (test->stage==4) {
        if (!label(root,"File round trip OK") || !label(root,count)) return;
        capture(test); test->stage=5; host->exit_requested=1;
    }
}
int main(int argc,char **argv) {
    assert(argc==5);
    setenv("SDL_VIDEODRIVER","dummy",1); setenv("SDL_AUDIODRIVER","dummy",1);
    for (unsigned repeat=0;repeat<3;++repeat) {
        options_t options={0}; options.package_path=argv[1]; options.publisher_key=argv[2];
        options.state_root=argv[3]; options.locale="en-US";
        options.width=repeat==1?480:repeat==2?454:240;
        options.height=repeat==1?640:repeat==2?454:320;
        options.density_dpi=repeat==1?305:160;
        if (repeat==2) {
            options.display_shape=PXA_UI_DISPLAY_SHAPE_CIRCLE;
            for (unsigned i=0;i<4;++i) options.safe_insets[i]=67;
        }
        patterns_test_t test={0}; test.started=now_us(NULL); test.repeat=repeat; test.artifacts=argv[4];
        assert(!run_product_simulator(&options,NULL,NULL,pump_test,&test,NULL,&test,bind_test,NULL,NULL,NULL));
        assert(!test.host && test.stage==5);
    }
    puts("SDK patterns AOT: typed save/restart, chunked file IO, application task errors/recovery, focus, 3 profiles incl 305 DPI passed");
    return 0;
}

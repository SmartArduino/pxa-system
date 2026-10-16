/* A real independently signed provider plus an ordinary C++ TextInput Guest. */
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "pxa/wamr/pxa_wamr_engine.h"
#include "pxa/ime.h"
static pxa_status_t test_engine_init(void *,size_t,const pxa_wamr_engine_config_t *,
    pxa_wamr_engine_t **,pxa_component_engine_t *);
static pxa_status_t test_ime_init(void *,size_t,pxa_runtime_t *,const pxa_ime_config_t *,pxa_ime_service_t **);
static pxa_status_t observed_delivery(pxa_wamr_engine_t *,pxa_runtime_t *,pxa_component_t,pxa_wamr_event_result_t *);
#define pxa_wamr_engine_deliver_event_result observed_delivery
#define pxa_wamr_engine_init test_engine_init
#define pxa_ime_service_init test_ime_init
#include "../product_runner.c"
#undef pxa_wamr_engine_init
#undef pxa_ime_service_init
#undef pxa_wamr_engine_deliver_event_result
#include <assert.h>
#include <png.h>
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h"

static pxa_status_t test_engine_init(void *memory,size_t bytes,
    const pxa_wamr_engine_config_t *config,pxa_wamr_engine_t **engine,pxa_component_engine_t *ops) {
    pxa_wamr_engine_config_t bounded=*config; bounded.call_timeout_us=1000000;
    return pxa_wamr_engine_init(memory,bytes,&bounded,engine,ops);
}
static unsigned foreground_deliveries;
static pxa_component_t observed_foreground;
static pxa_status_t observed_delivery(pxa_wamr_engine_t *engine,pxa_runtime_t *runtime,
    pxa_component_t component,pxa_wamr_event_result_t *result) {
    pxa_status_t status=pxa_wamr_engine_deliver_event_result(engine,runtime,component,result);
    if (component==observed_foreground && result->event_consumed) ++foreground_deliveries;
    return status;
}
static void settle_ime(product_host_t *host) {
    for (unsigned step=0;step<2048;++step) {
        dispatch_component_events(host);
        if (!pxa_posix_ime_provider_loading(host->ime_provider) && !host->ime_dirty) {
            pxa_component_snapshot_t snapshot;
            if (host->ime_component==PXA_COMPONENT_INVALID ||
                pxa_component_snapshot(host->runtime,host->ime_component,&snapshot)!=PXA_STATUS_OK ||
                !snapshot.queued_events) return;
        }
    }
    assert(!"IME verification did not finish within its bounded fixture");
}
static bool expire_provider;
static pxa_status_t deadline_present(void *context,const pxa_ime_frame_t *frame) {
    product_host_t *host=context;
    if (frame) {
        pxa_wamr_event_result_t nested;
        assert(pxa_wamr_engine_deliver_event_result(host->engine,host->runtime,
            host->active_component,&nested)==PXA_STATUS_WOULD_BLOCK);
        assert(!nested.event_consumed);
    }
    if (expire_provider && frame && frame->sequence>1) {
        assert(pxa_wamr_engine_busy(host->engine));
        assert(!pxa_wamr_engine_poll_component_deadline(host->engine,host->active_component,UINT64_MAX));
        assert(!pxa_wamr_engine_poll_component_deadline(host->engine,host->ime_component,now_us(NULL)));
        assert(pxa_wamr_engine_poll_component_deadline(host->engine,host->ime_component,UINT64_MAX));
        expire_provider=false;
    }
    return ime_present(context,frame);
}
static pxa_status_t test_ime_init(void *memory,size_t bytes,pxa_runtime_t *runtime,
    const pxa_ime_config_t *config,pxa_ime_service_t **output) {
    pxa_ime_config_t observed=*config; observed.present=deadline_present;
    return pxa_ime_service_init(memory,bytes,runtime,&observed,output);
}

typedef struct {
    product_host_t *host; unsigned stage, repeat; uint64_t started;
    const char *artifacts;
    bool expect_load_failure;
    bool single_character_provider;
    pxa_wamr_memory_snapshot_t idle, loaded, closed;
} ime_test_t;
static lv_obj_t *find_textarea(lv_obj_t *root) {
    if (lv_obj_check_type(root,&lv_textarea_class)) return root;
    for (uint32_t i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *object=find_textarea(lv_obj_get_child(root,i)); if(object) return object;
    }
    return NULL;
}
static lv_obj_t *find_matrix(lv_obj_t *root,const char *key) {
    if (lv_obj_is_hidden(root)) return NULL;
    if (lv_obj_has_class(root,&lv_buttonmatrix_class)) {
        for (uint32_t i=0;lv_buttonmatrix_get_button_text(root,i);++i)
            if (!strcmp(lv_buttonmatrix_get_button_text(root,i),key)) return root;
    }
    for (uint32_t i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *object=find_matrix(lv_obj_get_child(root,i),key); if(object) return object;
    }
    return NULL;
}
static void assert_key_label_fits(const char *text) {
    lv_obj_t *object=find_matrix(lv_layer_top(),text); assert(object);
    lv_obj_update_layout(object);
    unsigned index=0;
    while (strcmp(lv_buttonmatrix_get_button_text(object,index),text)) ++index;
    lv_area_t cell=((lv_buttonmatrix_t *)object)->button_areas[index];
    lv_point_t size;
    lv_text_get_size(&size,text,lv_obj_get_style_text_font(object,LV_PART_ITEMS),
        lv_obj_get_style_text_letter_space(object,LV_PART_ITEMS),0,INT32_MAX,LV_TEXT_FLAG_NONE);
    if (size.x>lv_area_get_width(&cell))
        fprintf(stderr,"Clipped key %s: text=%d cell=%d matrix=%d,%d font=%u\n",text,size.x,
            lv_area_get_width(&cell),lv_obj_get_width(object),lv_obj_get_height(object),
            lv_obj_get_style_text_font(object,LV_PART_ITEMS)->line_height);
    assert(size.x<=lv_area_get_width(&cell));
}
static void key(ime_test_t *test,const char *text) {
    if (!strcmp(text,"中/英")) text=find_matrix(lv_layer_top(),"中")?"中":"EN";
    lv_obj_t *object=find_matrix(lv_layer_top(),text);
    if (!object && !strcmp(text,"全键") && find_matrix(lv_layer_top(),"q")) return;
    if (!object && !strcmp(text,"九键") && find_matrix(lv_layer_top(),"2 ABC")) return;
    if (!object && (!strcmp(text,"全键") || !strcmp(text,"九键"))) {
        key(test,LV_SYMBOL_SETTINGS); object=find_matrix(lv_layer_top(),text);
    }
    if (!object) {
        fprintf(stderr,"Missing key: %s\n",text);
        lv_obj_t *root=pxa_lvgl_ime_root(test->host->ime_keyboard);
        fprintf(stderr,"profile=%u root hidden=%d top hidden=%d size=%d,%d font=%d\n",test->repeat,lv_obj_is_hidden(root),lv_obj_is_hidden(lv_layer_top()),lv_obj_get_width(root),lv_obj_get_height(root),lv_obj_get_style_text_font(root,0)->line_height);
        for (unsigned c=0;c<lv_obj_get_child_count(root);++c) {
            lv_obj_t *child=lv_obj_get_child(root,c);
            if (!lv_obj_check_type(child,&lv_buttonmatrix_class)) continue;
            fprintf(stderr,"matrix hidden=%d:",lv_obj_is_hidden(child));
            for(unsigned b=0;lv_buttonmatrix_get_button_text(child,b);++b) fprintf(stderr," %s",lv_buttonmatrix_get_button_text(child,b));
            fputc('\n',stderr);
        }
    }
    assert(object);
    for (uint32_t i=0;lv_buttonmatrix_get_button_text(object,i);++i)
        if (!strcmp(lv_buttonmatrix_get_button_text(object,i),text)) {
            assert(!lv_buttonmatrix_has_button_ctrl(object,i,LV_BUTTONMATRIX_CTRL_HIDDEN));
            lv_buttonmatrix_set_selected_button(object,i);
            lv_obj_send_event(object,LV_EVENT_VALUE_CHANGED,NULL);
            settle_ime(test->host);
            return;
        }
    assert(0);
}
static void queued_key(const char *text) {
    if (!strcmp(text,"中/英")) text=find_matrix(lv_layer_top(),"中")?"中":"EN";
    lv_obj_t *object=find_matrix(lv_layer_top(),text);
    if (!object && (!strcmp(text,"中/英") || !strcmp(text,"123") || !strcmp(text,"符"))) {
        queued_key(LV_SYMBOL_SETTINGS); object=find_matrix(lv_layer_top(),text);
    }
    assert(object);
    for (uint32_t i=0;lv_buttonmatrix_get_button_text(object,i);++i) {
        if (strcmp(lv_buttonmatrix_get_button_text(object,i),text)) continue;
        lv_buttonmatrix_set_selected_button(object,i);
        lv_obj_send_event(object,LV_EVENT_VALUE_CHANGED,NULL); return;
    }
    assert(0);
}
static lv_point_t pointer_position;
static bool pointer_down;
static void pointer_read(lv_indev_t *indev,lv_indev_data_t *data) {
    (void)indev; data->point=pointer_position; data->state=pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void pointer_matrix_key(ime_test_t *test,lv_obj_t *object,const char *text,unsigned hold_ms) {
    assert(object);
    lv_obj_update_layout(lv_layer_top());
    unsigned index=0; while(strcmp(lv_buttonmatrix_get_button_text(object,index),text)) ++index;
    // Test-only access to the rendered cells: exercise real LVGL hit testing,
    // press/release and repeat rather than synthesizing VALUE_CHANGED.
    lv_area_t box; lv_obj_get_coords(object,&box);
    lv_area_t cell=((lv_buttonmatrix_t *)object)->button_areas[index];
    pointer_position=(lv_point_t){box.x1+(cell.x1+cell.x2)/2,box.y1+(cell.y1+cell.y2)/2};
    lv_indev_t *pointer=lv_indev_create(); lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer,pointer_read);
    lv_indev_set_long_press_time(pointer,60); lv_indev_set_long_press_repeat_time(pointer,25);
    pointer_down=true; lv_indev_read(pointer); settle_ime(test->host);
    for(unsigned elapsed=0;elapsed<hold_ms;elapsed+=10) {
        SDL_Delay(10); lv_indev_read(pointer); settle_ime(test->host);
    }
    pointer_down=false; lv_indev_read(pointer); settle_ime(test->host); lv_indev_delete(pointer);
}
static void pointer_key(ime_test_t *test,const char *text,unsigned hold_ms) {
    pointer_matrix_key(test,find_matrix(lv_layer_top(),text),text,hold_ms);
}
static void assert_candidate_toggle_width(ime_test_t *test) {
    lv_obj_t *matrix=find_matrix(lv_layer_top(),LV_SYMBOL_UP);assert(matrix);
    lv_obj_update_layout(matrix);unsigned index=0;
    while(strcmp(lv_buttonmatrix_get_button_text(matrix,index),LV_SYMBOL_UP)) ++index;
    int minimum=(int)(((uint64_t)test->host->ime_config.environment.density_q16*44+32768)/65536);
    int width=lv_obj_get_content_width(matrix);
    if (minimum>width/3) minimum=width/3;
    lv_area_t cell=((lv_buttonmatrix_t *)matrix)->button_areas[index];
    if(lv_area_get_width(&cell)<minimum)
        fprintf(stderr,"Squeezed candidate toggle: profile=%u width=%d required=%d\n",test->repeat,lv_area_get_width(&cell),minimum);
    assert(lv_area_get_width(&cell)>=minimum);
}
static void assert_keyboard_restored(ime_test_t *test) {
    lv_obj_t *keys=find_matrix(lv_layer_top(),"q");
    if(!keys) keys=find_matrix(lv_layer_top(),"2 ABC");
    assert(keys);assert(!find_matrix(lv_layer_top(),"键盘"));
    lv_obj_t *candidates=find_matrix(lv_layer_top(),LV_SYMBOL_SETTINGS);assert(candidates);
    lv_obj_update_layout(lv_layer_top());lv_area_t a,b;
    lv_obj_get_coords(candidates,&a);lv_obj_get_coords(keys,&b);assert(a.y2<b.y1);
    assert(pxa_lvgl_ime_active(test->host->ime_keyboard));
}
static void immediate_backspace(ime_test_t *test,lv_obj_t *target) {
    lv_textarea_set_text(target,"abcd"); settle_ime(test->host);
    lv_obj_t *object=find_matrix(lv_layer_top(),LV_SYMBOL_BACKSPACE); assert(object);
    lv_obj_update_layout(object);
    unsigned index=0;
    while(strcmp(lv_buttonmatrix_get_button_text(object,index),LV_SYMBOL_BACKSPACE)) ++index;
    lv_area_t area; lv_obj_get_coords(object,&area);
    lv_area_t cell=((lv_buttonmatrix_t *)object)->button_areas[index];
    pointer_position=(lv_point_t){area.x1+(cell.x1+cell.x2)/2,area.y1+(cell.y1+cell.y2)/2};
    lv_indev_t *pointer=lv_indev_create(); lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer,pointer_read);
    pointer_down=true; lv_indev_read(pointer); settle_ime(test->host);
    assert(!strcmp(lv_textarea_get_text(target),"abc"));
    pointer_down=false; lv_indev_read(pointer); settle_ime(test->host);
    assert(!strcmp(lv_textarea_get_text(target),"abc")); // no extra release edit
    lv_indev_delete(pointer); lv_textarea_set_text(target,""); settle_ime(test->host);
}
static void swipe_candidates(ime_test_t *test,lv_obj_t *object,bool next,bool cross_boundary) {
    lv_obj_update_layout(lv_layer_top());
    lv_area_t area; lv_obj_get_coords(object,&area);
    pointer_position=(lv_point_t){(area.x1+area.x2)/2,area.y1+(area.y2-area.y1)/4};
    lv_indev_t *pointer=lv_indev_create(); lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer,pointer_read);
    pointer_down=true; lv_indev_read(pointer);
    int distance=(int)(((uint64_t)test->host->ime_config.environment.density_q16*12+32768)/65536);
    if (distance>(area.x2-area.x1+1)/6) distance=(area.x2-area.x1+1)/6;
    if (distance<10) distance=10;
    distance+=2;
    if (cross_boundary) distance=(area.x2-area.x1)/2+4;
    pointer_position.x+=next?-distance:distance;
    lv_indev_read(pointer); settle_ime(test->host);
    pointer_down=false; lv_indev_read(pointer); settle_ime(test->host); lv_indev_delete(pointer);
}
static void candidate_vertical_swipe(ime_test_t *test,bool expand) {
    lv_obj_t *object=find_matrix(lv_layer_top(),LV_SYMBOL_SETTINGS); assert(object);
    lv_obj_update_layout(object);
    lv_area_t area; lv_obj_get_coords(object,&area);
    pointer_position=(lv_point_t){(area.x1+area.x2)/2,(area.y1+area.y2)/2};
    lv_indev_t *pointer=lv_indev_create(); lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer,pointer_read);
    pointer_down=true; lv_indev_read(pointer);
    int distance=(int)(((uint64_t)test->host->ime_config.environment.density_q16*12+32768)/65536);
    if (distance>(area.x2-area.x1+1)/6) distance=(area.x2-area.x1+1)/6;
    if (distance<10) distance=10;
    pointer_position.y+=expand?-distance-2:distance+2;
    lv_indev_read(pointer); settle_ime(test->host);
    pointer_down=false; lv_indev_read(pointer); settle_ime(test->host); lv_indev_delete(pointer);
    assert(expand ? find_matrix(lv_layer_top(),"键盘")!=NULL : find_matrix(lv_layer_top(),"2 ABC")!=NULL);
}
static bool physical_point(const pxa_ui_environment_t *e,int x,int y) {
    if (x<(int)e->safe_insets[3] || x>=(int)e->width-(int)e->safe_insets[1] ||
        y<(int)e->safe_insets[0] || y>=(int)e->height-(int)e->safe_insets[2]) return false;
    if (e->display_shape==2) {
        double cx=(e->width-1)*0.5,cy=(e->height-1)*0.5,r=(e->width<e->height?e->width:e->height)*0.5;
        return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=r*r;
    }
    if (e->display_shape==1) {
        unsigned corner=(y>=(int)e->height/2?(x<(int)e->width/2?3:2):(x<(int)e->width/2?0:1));
        int radius=(int)e->corner_radii[corner];
        if (radius>(int)e->width/2) radius=e->width/2;
        if (radius>(int)e->height/2) radius=e->height/2;
        int dx=x<(int)e->width/2?x:(int)e->width-1-x;
        int dy=y<(int)e->height/2?y:(int)e->height-1-y;
        if (dx<radius && dy<radius) return (radius-dx)*(radius-dx)+(radius-dy)*(radius-dy)<=radius*radius;
    }
    return true;
}
static void assert_keyboard_bounds(ime_test_t *test) {
    lv_obj_t *root=pxa_lvgl_ime_root(test->host->ime_keyboard); assert(root);
    lv_area_t box; lv_obj_get_coords(root,&box);
    const pxa_ui_environment_t *e=&test->host->ime_config.environment;
    assert(box.x2>=box.x1 && box.y2>=box.y1);
    if (e->display_shape==0) {
        assert(physical_point(e,box.x1,box.y1) && physical_point(e,box.x2,box.y1));
        assert(physical_point(e,box.x1,box.y2) && physical_point(e,box.x2,box.y2));
    }
    for (unsigned i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *child=lv_obj_get_child(root,i);
        if (lv_obj_is_hidden(child)) continue;
        lv_area_t area; lv_obj_get_coords(child,&area);
        if (area.y1<box.y1 || area.y2>box.y2) fprintf(stderr,"outside child %u y=%d..%d root=%d..%d\n",i,area.y1,area.y2,box.y1,box.y2);
        assert(area.y1>=box.y1 && area.y2<=box.y2);
        if (lv_obj_has_class(child,&lv_buttonmatrix_class)) {
            // A circular footer has hidden side cells on its lower row.
            // Validate each actual hit target instead of its transparent box.
            const lv_buttonmatrix_t *matrix=(lv_buttonmatrix_t *)child;
            for (unsigned k=0;lv_buttonmatrix_get_button_text(child,k);++k) {
                if (lv_buttonmatrix_has_button_ctrl(child,k,LV_BUTTONMATRIX_CTRL_HIDDEN)) continue;
                lv_area_t cell=matrix->button_areas[k];
                cell.x1+=area.x1; cell.x2+=area.x1; cell.y1+=area.y1; cell.y2+=area.y1;
                assert(physical_point(e,cell.x1,cell.y1) && physical_point(e,cell.x2,cell.y1));
                assert(physical_point(e,cell.x1,cell.y2) && physical_point(e,cell.x2,cell.y2));
            }
            continue;
        }
        assert(physical_point(e,area.x1,area.y1) && physical_point(e,area.x2,area.y1));
        assert(physical_point(e,area.x1,area.y2) && physical_point(e,area.x2,area.y2));
    }
}
static unsigned count_key(lv_obj_t *object,const char *label) {
    if (lv_obj_is_hidden(object)) return 0;
    unsigned count=0;
    if (lv_obj_has_class(object,&lv_buttonmatrix_class))
        for (unsigned i=0;lv_buttonmatrix_get_button_text(object,i);++i)
            if (!strcmp(lv_buttonmatrix_get_button_text(object,i),label) &&
                !lv_buttonmatrix_has_button_ctrl(object,i,LV_BUTTONMATRIX_CTRL_HIDDEN)) ++count;
    for (unsigned i=0;i<lv_obj_get_child_count(object);++i) count+=count_key(lv_obj_get_child(object,i),label);
    return count;
}
static void assert_edit_controls(ime_test_t *test) {
    lv_obj_t *root=pxa_lvgl_ime_root(test->host->ime_keyboard);
    assert(count_key(root,LV_SYMBOL_BACKSPACE)==1);
    assert(count_key(root,"完成")+count_key(root,LV_SYMBOL_NEW_LINE)==1);
    assert(count_key(root,LV_SYMBOL_DOWN)==1);
    assert(count_key(root,"空格")==1 && count_key(root,"中")+count_key(root,"EN")==1);
}
static void drag_space(ime_test_t *test) {
    lv_obj_t *object=find_matrix(lv_layer_top(),"空格"); assert(object);
    lv_obj_update_layout(object); unsigned index=0;
    while(strcmp(lv_buttonmatrix_get_button_text(object,index),"空格")) ++index;
    lv_area_t area; lv_obj_get_coords(object,&area);
    lv_area_t cell=((lv_buttonmatrix_t *)object)->button_areas[index];
    pointer_position=(lv_point_t){area.x1+(cell.x1+cell.x2)/2,area.y1+(cell.y1+cell.y2)/2};
    lv_indev_t *pointer=lv_indev_create(); lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer,pointer_read); lv_indev_set_long_press_time(pointer,40);
    pointer_down=true; lv_indev_read(pointer);
    for (unsigned i=0;i<8;++i) { SDL_Delay(10); lv_indev_read(pointer); }
    int distance=lv_obj_get_style_text_font(object,0)->line_height+2;
    if (distance<24) distance=24;
    pointer_position.x-=distance; lv_indev_read(pointer);
    // Continue across neighboring keys and above the toolbar. This remains
    // one captured caret gesture, never a language switch or keyboard hide.
    pointer_position.x=area.x1+2; pointer_position.y=area.y1-4;
    lv_indev_read(pointer);
    pointer_down=false; lv_indev_read(pointer); settle_ime(test->host); lv_indev_delete(pointer);
    assert(pxa_lvgl_ime_active(test->host->ime_keyboard));
    assert(!lv_obj_is_press_lock(object));
}
static unsigned blocked;
static pxa_status_t blocked_input(void *context,uint64_t generation,uint16_t action,
    uint8_t layout,uint8_t language,uint32_t value,pxa_bytes_t text) {
    if (blocked) { --blocked; return PXA_STATUS_WOULD_BLOCK; }
    return ime_input(context,generation,action,layout,language,value,text);
}
static void bind_test(void *context,void (*focus)(void *,bool),
    void (*exit_app)(void *),void *runner) {
    (void)focus; (void)exit_app; ((ime_test_t *)context)->host=runner;
}
static void capture(ime_test_t *test,const char *name) {
    lv_refr_now(NULL);
    assert_keyboard_bounds(test);
    if (!strcmp(name,"nine")) {
        const char *labels[]={"2 ABC","4 GHI","7 PQRS"};
        char path[1536]; snprintf(path,sizeof(path),"%s/profile-%u-key-rows.json",test->artifacts,test->repeat);
        FILE *geometry=fopen(path,"w"); assert(geometry); fputs("[",geometry);
        for (unsigned row=0;row<3;++row) {
            lv_obj_t *object=find_matrix(lv_layer_top(),labels[row]); assert(object);
            lv_area_t area; lv_obj_get_coords(object,&area);
            if (test->host->ime_config.environment.display_shape)
                assert(row==0 || object!=find_matrix(lv_layer_top(),labels[row-1]));
            unsigned index=0; while(strcmp(lv_buttonmatrix_get_button_text(object,index),labels[row])) ++index;
            lv_area_t cell=((lv_buttonmatrix_t *)object)->button_areas[index];
            fprintf(geometry,"%s{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"button_width\":%d}",
                row?",":"",area.x1,area.y1,lv_obj_get_width(object),lv_obj_get_height(object),cell.x2-cell.x1+1);
        }
        fputs("]\n",geometry); assert(!fclose(geometry));
    }
    lv_display_t *display=lv_display_get_default();
    lv_draw_buf_t *source=lv_display_get_buf_active(display); assert(source);
    unsigned w=lv_display_get_horizontal_resolution(display),h=lv_display_get_vertical_resolution(display);
    unsigned stride=lv_draw_buf_width_to_stride(w,lv_display_get_color_format(display));
    uint8_t *pixels=malloc((size_t)w*h*4); assert(pixels);
    assert(!SDL_ConvertPixels(w,h,SDL_PIXELFORMAT_RGB888,source->data,stride,SDL_PIXELFORMAT_RGBA32,pixels,w*4));
    png_image image={0}; image.version=PNG_IMAGE_VERSION; image.width=w; image.height=h; image.format=PNG_FORMAT_RGBA;
    char path[1536]; assert(snprintf(path,sizeof(path),"%s/profile-%u-%s.png",test->artifacts,test->repeat,name)>0);
    assert(png_image_write_to_file(&image,path,0,pixels,w*4,NULL)); free(pixels);
}
static void pump_test(void *context) {
    ime_test_t *test=context; product_host_t *host=test->host; if(!host) return;
    assert(now_us(NULL)-test->started<UINT64_C(15000000));
    lv_obj_t *target=find_textarea(lv_screen_active()); if(!target) return;
    if (!test->stage) {
        assert(pxa_wamr_engine_memory_snapshot(host->engine,&test->idle)==PXA_STATUS_OK);
        if (!test->expect_load_failure) {
            pxa_posix_ime_provider_t *probe=NULL;
            assert(pxa_posix_ime_provider_open_begin(host->ime_installer,host->ime_directory,
                host->runtime,&host->engine_ops,host->ime_capabilities,host->ime_profile,&probe)==PXA_STATUS_OK);
            assert(pxa_posix_ime_provider_loading(probe));
            pxa_component_t component=PXA_COMPONENT_INVALID;
            assert(pxa_posix_ime_provider_start(probe,++host->next_instance_id,&component)==PXA_STATUS_BAD_STATE);
            assert(component==PXA_COMPONENT_INVALID);
            assert(pxa_posix_ime_provider_open_step(probe,0)==PXA_STATUS_INVALID_ARGUMENT);
            // Authentication, inventory and the first one-byte hash step are
            // individually cancellable, and none may create a Guest.
            assert(pxa_posix_ime_provider_open_step(probe,1)==PXA_STATUS_WOULD_BLOCK);
            assert(pxa_posix_ime_provider_open_step(probe,1)==PXA_STATUS_WOULD_BLOCK);
            assert(pxa_posix_ime_provider_open_step(probe,1)==PXA_STATUS_WOULD_BLOCK);
            assert(pxa_posix_ime_provider_start(probe,++host->next_instance_id,&component)==PXA_STATUS_BAD_STATE);
            assert(component==PXA_COMPONENT_INVALID);
            pxa_posix_ime_provider_close(probe);
        }
        assert(!memcmp(host->ime_config.environment.safe_insets,host->safe_insets,sizeof(host->safe_insets)));
        if (test->repeat==11) {
            uint32_t corners[]={0,96,56,4};
            memcpy(host->ime_config.environment.corner_radii,corners,sizeof(corners));
        }
        lv_obj_send_event(target,LV_EVENT_CLICKED,NULL);
        assert(!host->ime_provider && !pxa_lvgl_ime_loading_allowed(host->ime_keyboard));
        capture(test,"loading");
        pxa_lvgl_ime_timing_t opening; pxa_lvgl_ime_timing(host->ime_keyboard,&opening);
        assert(opening.drawn && !opening.ready);
        if (!test->expect_load_failure) {
            assert(!host->ime_service && !host->ime_workspace);
            dispatch_component_events(host);
            assert(pxa_posix_ime_provider_loading(host->ime_provider));
            assert(host->ime_component==PXA_COMPONENT_INVALID && !host->ime_session);
            pxa_wamr_memory_snapshot_t verifying;
            assert(pxa_wamr_engine_memory_snapshot(host->engine,&verifying)==PXA_STATUS_OK);
            assert(verifying.linear_current_bytes==test->idle.linear_current_bytes);
            // The foreground can receive events while signed inventory hashing is unfinished.
            observed_foreground=host->active_component;
            unsigned delivered=foreground_deliveries; uint8_t tick[8]={0};
            pxa_status_t event_status=pxa_event_post_message(host->runtime,host->active_component,
                PRODUCT_CLOCK_SERVICE,PRODUCT_CLOCK_TICK,0,(pxa_bytes_t){tick,sizeof(tick)},0,UINT64_C(0x000400008001));
            if (event_status!=PXA_STATUS_OK) fprintf(stderr,"foreground event status=%d\n",event_status);
            assert(event_status==PXA_STATUS_OK);
            dispatch_component_events(host);
            assert(foreground_deliveries>delivered && pxa_posix_ime_provider_loading(host->ime_provider));
            // Switching to English cancels, releases provisional metadata and never starts Guest.
            key(test,"中/英");
            assert(!host->ime_provider && !host->ime_session && host->ime_keyboard);
            key(test,"a"); assert(!strcmp(lv_textarea_get_text(target),"a"));
            key(test,LV_SYMBOL_BACKSPACE);
            key(test,"中/英");
        }
        settle_ime(host);
        if (test->expect_load_failure) {
            assert(!host->ime_provider && host->ime_keyboard);
            assert(find_matrix(lv_layer_top(),"2 ABC") || find_matrix(lv_layer_top(),"q"));
            capture(test,"rejected-provider");
            pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
            assert(pxa_wamr_engine_memory_snapshot(host->engine,&test->closed)==PXA_STATUS_OK);
            assert(test->closed.linear_current_bytes==test->idle.linear_current_bytes);
            test->stage=2; host->exit_requested=1; return;
        }
        assert(pxa_wamr_engine_memory_snapshot(host->engine,&test->loaded)==PXA_STATUS_OK);
        assert(host->ime_provider && host->ime_session);
        // The provider can return a short page with more results. Visibility
        // must follow the current reply, even when the map size is unchanged.
        const uint8_t one_candidate[]={3,0,0xe4,0xbd,0xa0};
        pxa_ime_frame_t page={.sequence=2,.candidate_count=1,
            .flags=PXA_IME_MORE_CANDIDATES,
            .preedit={(const uint8_t *)"ni",2},
            .items={one_candidate,sizeof(one_candidate)}};
        uint64_t generation=pxa_lvgl_ime_generation(host->ime_keyboard);
        assert(pxa_lvgl_ime_present(host->ime_keyboard,generation,&page)==PXA_STATUS_OK);
        key(test,LV_SYMBOL_UP);
        lv_obj_t *page_strip=find_matrix(lv_layer_top(),">"); assert(page_strip);
        unsigned next_index=0; while(strcmp(lv_buttonmatrix_get_button_text(page_strip,next_index),">")) ++next_index;
        assert(!lv_buttonmatrix_has_button_ctrl(page_strip,next_index,LV_BUTTONMATRIX_CTRL_DISABLED));
        page.flags=0;
        assert(pxa_lvgl_ime_present(host->ime_keyboard,generation,&page)==PXA_STATUS_OK);
        assert(find_matrix(lv_layer_top(),"键盘")==page_strip);
        assert(lv_buttonmatrix_has_button_ctrl(page_strip,next_index,LV_BUTTONMATRIX_CTRL_DISABLED));
        key(test,"键盘");
        key(test,LV_SYMBOL_UP);
        page.candidate_count=0;page.items=(pxa_bytes_t){0};
        assert(pxa_lvgl_ime_present(host->ime_keyboard,generation,&page)==PXA_STATUS_OK);
        assert_keyboard_restored(test);
        // A dictionary word may have the same text as a navigation control.
        const uint8_t reserved_word[]={6,0,0xe9,0x94,0xae,0xe7,0x9b,0x98};
        page.candidate_count=1;page.items=(pxa_bytes_t){reserved_word,sizeof(reserved_word)};
        assert(pxa_lvgl_ime_present(host->ime_keyboard,generation,&page)==PXA_STATUS_OK);
        key(test,"键盘");
        assert(find_matrix(lv_layer_top(),"q") || find_matrix(lv_layer_top(),"2 ABC"));
        lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); settle_ime(host);
        if (find_matrix(lv_layer_top(),"2 ABC")) key(test,"全键");
        assert_edit_controls(test);
        capture(test,"idle");
        if (test->repeat==4) {
            const lv_font_t *font=lv_obj_get_style_text_font(pxa_lvgl_ime_root(host->ime_keyboard),0);
            lv_font_glyph_dsc_t glyph; assert(lv_font_get_glyph_dsc(font,&glyph,0x4e2d,0));
            assert(glyph.adv_w==12 && lv_font_has_static_bitmap(glyph.resolved_font));
            lv_font_glyph_release_draw_data(&glyph);
            assert(lv_font_get_glyph_dsc(font,&glyph,0xb7,0));
            assert(lv_font_has_static_bitmap(glyph.resolved_font));
            lv_font_glyph_release_draw_data(&glyph);
        }
        lv_area_t idle_keys,composing_keys;
        lv_obj_get_coords(find_matrix(lv_layer_top(),"q"),&idle_keys);
        immediate_backspace(test,target);
        key(test,"n"); key(test,"i"); capture(test,"full");
        assert_candidate_toggle_width(test);
        assert_key_label_fits("分词"); assert_key_label_fits("完成");
        lv_obj_get_coords(find_matrix(lv_layer_top(),"q"),&composing_keys);
        assert(composing_keys.y1==idle_keys.y1);
        assert(!strcmp(lv_textarea_get_text(target),"")); // preedit stays outside the editor
        assert(find_matrix(lv_layer_top(),"你")); key(test,"你");
        assert(!strcmp(lv_textarea_get_text(target),"你"));
        key(test,"n"); key(test,"i"); key(test,"h"); key(test,"a"); key(test,"o");
        pointer_key(test,LV_SYMBOL_UP,0);
        if (test->single_character_provider) {
            assert(!find_matrix(lv_layer_top(),"你好"));
            pointer_key(test,"你",0);assert_keyboard_restored(test);
            pointer_key(test,LV_SYMBOL_UP,0);pointer_key(test,"好",0);
        } else { assert(find_matrix(lv_layer_top(),"你好")); pointer_key(test,"你好",0); }
        assert_keyboard_restored(test);capture(test,"expanded-word-selected");
        assert(!strcmp(lv_textarea_get_text(target),"你你好"));
        lv_textarea_set_text(target,""); settle_ime(host);
        key(test,"九键");
        pointer_key(test,"6 MNO",0); pointer_key(test,"4 GHI",0);
        lv_obj_t *candidate_strip=find_matrix(lv_layer_top(),LV_SYMBOL_SETTINGS); assert(candidate_strip);
        swipe_candidates(test,candidate_strip,true,true);
        assert(!strcmp(lv_textarea_get_text(target),""));
        capture(test,"spelling-swipe");
        for(unsigned page=0;page<32 && !find_matrix(lv_layer_top(),"ni");++page) swipe_candidates(test,candidate_strip,true,false);
        assert(find_matrix(lv_layer_top(),"ni"));
        pointer_key(test,LV_SYMBOL_UP,0);pointer_key(test,"ni",0);
        assert(find_matrix(lv_layer_top(),"键盘")); // Spelling choices keep browsing open.
        key(test,"键盘");
        // Spellings and words remain visible together. A spelling choice
        // filters the word row; neither a swipe nor a spelling commits text.
        assert(find_matrix(lv_layer_top(),"你"));
        swipe_candidates(test,candidate_strip,true,false); swipe_candidates(test,candidate_strip,false,true);
        assert(find_matrix(lv_layer_top(),"你"));
        assert(!strcmp(lv_textarea_get_text(target),""));
        assert_edit_controls(test);
        capture(test,"nine");
        static const char *const nine_labels[]={"2 ABC","3 DEF","4 GHI","5 JKL",
            "6 MNO","7 PQRS","8 TUV","9 WXYZ","分词","完成","空格"};
        for (unsigned i=0;i<sizeof(nine_labels)/sizeof(nine_labels[0]);++i)
            assert_key_label_fits(nine_labels[i]);
        lv_obj_t *toolbar=find_matrix(lv_layer_top(),"空格");
        const char *const *toolbar_map=lv_buttonmatrix_get_map(toolbar);
        bool arc_footer=false;
        for (unsigned i=0;*toolbar_map[i];++i) arc_footer|=!strcmp(toolbar_map[i],"\n");
        if (arc_footer) {
            assert(host->ime_config.environment.display_shape==2);
            const lv_buttonmatrix_t *matrix=(lv_buttonmatrix_t *)toolbar;
            assert(matrix->button_areas[4].y1>matrix->button_areas[0].y2);
            // Invisible chord spacers consume neither text nor a close action.
            pointer_matrix_key(test,toolbar," ",0);
            assert(pxa_lvgl_ime_active(host->ime_keyboard));
            assert(!strcmp(lv_textarea_get_text(target),""));
            assert(find_matrix(lv_layer_top(),"ni"));
        }
        candidate_vertical_swipe(test,true);
        assert(!strcmp(lv_textarea_get_text(target),""));
        candidate_vertical_swipe(test,false);
        assert(!strcmp(lv_textarea_get_text(target),""));
        key(test,LV_SYMBOL_UP); capture(test,"expanded-candidates");
        if (test->repeat==4) {
            const char *const *map=lv_buttonmatrix_get_map(find_matrix(lv_layer_top(),"键盘"));
            unsigned rows=1; for (unsigned i=0;*map[i];++i) rows+=!strcmp(map[i],"\n");
            assert(rows>=4); // At least two word rows, plus reading and navigation.
        }
        pointer_key(test,"你",0);assert_keyboard_restored(test);
        capture(test,"expanded-character-selected");
        assert(!strcmp(lv_textarea_get_text(target),"你"));
        key(test,"123"); assert_edit_controls(test); key(test,"7"); assert(!strcmp(lv_textarea_get_text(target),"你7"));
        capture(test,"number"); key(test,"返回");
        key(test,LV_SYMBOL_BACKSPACE); assert(!strcmp(lv_textarea_get_text(target),"你"));
        key(test,"符"); assert_edit_controls(test); key(test,"，"); assert(!strcmp(lv_textarea_get_text(target),"你，"));
        capture(test,"symbols"); key(test,LV_SYMBOL_BACKSPACE);
        assert_key_label_fits("下一页"); assert_key_label_fits("完成");
        key(test,"下一页"); key(test,"@"); assert(!strcmp(lv_textarea_get_text(target),"你@"));
        capture(test,"symbols-second-page"); key(test,LV_SYMBOL_BACKSPACE); key(test,"返回");
        assert(!strcmp(lv_textarea_get_text(target),"你"));
        lv_textarea_set_text(target,""); settle_ime(host);
        key(test,"6 MNO"); key(test,"4 GHI");
        for (unsigned p=0;p<32 && !find_matrix(lv_layer_top(),"ni");++p)
            swipe_candidates(test,find_matrix(lv_layer_top(),LV_SYMBOL_SETTINGS),true,false);
        key(test,"ni"); key(test,"分词");
        key(test,"4 GHI"); key(test,"2 ABC"); key(test,"6 MNO");
        for (unsigned p=0;p<32 && !find_matrix(lv_layer_top(),"ni'hao");++p)
            swipe_candidates(test,find_matrix(lv_layer_top(),LV_SYMBOL_SETTINGS),true,false);
        key(test,"ni'hao");
        if (test->single_character_provider) {
            assert(!find_matrix(lv_layer_top(),"你好"));
            key(test,"你"); key(test,"好");
        } else { assert(find_matrix(lv_layer_top(),"你好")); key(test,"你好"); }
        assert(!strcmp(lv_textarea_get_text(target),"你好"));
        lv_textarea_set_text(target,"你"); settle_ime(host);
        // The UI can switch a caret while a provider reply is still queued.
        assert(ime_input(host,host->ime_generation,PXA_IME_TEXT,0,0,0,(pxa_bytes_t){(const uint8_t *)"ni",2})==0);
        lv_textarea_set_text(target,"外部"); settle_ime(host);
        assert(!strcmp(lv_textarea_get_text(target),"外部")); assert(host->ime_provider && host->ime_session);
        lv_textarea_set_text(target,"你"); settle_ime(host);
        key(test,"6 MNO"); key(test,"4 GHI");
        for (unsigned p=0;p<32 && !find_matrix(lv_layer_top(),"ni");++p)
            swipe_candidates(test,find_matrix(lv_layer_top(),LV_SYMBOL_SETTINGS),true,false);
        key(test,"ni"); key(test,"中/英");
        assert(!strcmp(lv_textarea_get_text(target),"你你") && !host->ime_provider);
        lv_textarea_set_text(target,"你"); settle_ime(host);
        key(test,"h"); key(test,"i");
        assert(!strcmp(lv_textarea_get_text(target),"你hi"));
        key(test,LV_SYMBOL_BACKSPACE); assert(!strcmp(lv_textarea_get_text(target),"你h"));
        key(test,"完成"); settle_ime(host);
        assert(!host->ime_provider && !host->ime_session && !host->ime_keyboard);
        assert(pxa_wamr_engine_memory_snapshot(host->engine,&test->closed)==PXA_STATUS_OK);
        assert(test->closed.linear_current_bytes==test->idle.linear_current_bytes);
        test->stage=1;
    } else if (test->stage==1) {
        // A loaded dictionary must be freed before a wider English map is
        // allocated. An old owner's acknowledgement cannot unlock new UI.
        lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); lv_refr_now(NULL); settle_ime(host);
        key(test,"九键"); queued_key("中/英");
        lv_obj_t *old_keys=find_matrix(lv_layer_top(),"2 ABC");
        assert(host->ime_provider && old_keys && lv_obj_has_state(old_keys,LV_STATE_DISABLED));
        pxa_lvgl_ime_provider_released(host->ime_keyboard,host->ime_generation-1);
        assert(find_matrix(lv_layer_top(),"2 ABC"));
        settle_ime(host); assert(!host->ime_provider && find_matrix(lv_layer_top(),"q"));
        pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
        lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); lv_refr_now(NULL); settle_ime(host);
        queued_key("中/英"); queued_key(LV_SYMBOL_DOWN); settle_ime(host);
        assert(!host->ime_provider && !host->ime_keyboard);
        for (unsigned i=0;i<8;++i) {
            lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); lv_refr_now(NULL); settle_ime(host);
            assert(host->ime_provider);
            pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
            assert(!host->ime_provider);
        }
        // Closing or destroying the target midway through verification cancels it.
        lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); lv_refr_now(NULL); dispatch_component_events(host);
        assert(pxa_posix_ime_provider_loading(host->ime_provider));
        pxa_lvgl_ime_close(host->ime_keyboard); dispatch_component_events(host);
        assert(!host->ime_provider && !host->ime_keyboard);
        lv_obj_t *temporary=lv_textarea_create(lv_screen_active());
        ime_editor_focus(host,temporary,true); lv_refr_now(NULL); dispatch_component_events(host);
        assert(pxa_posix_ime_provider_loading(host->ime_provider));
        lv_obj_delete(temporary); dispatch_component_events(host);
        assert(!host->ime_provider && !host->ime_keyboard);
        // A tap arriving before the first draw must be editable without loading Chinese.
        lv_textarea_set_text(target,"");
        lv_obj_send_event(target,LV_EVENT_CLICKED,NULL);
        queued_key("中/英"); queued_key("a");
        assert(!host->ime_provider && !strcmp(lv_textarea_get_text(target),"a"));
        lv_refr_now(NULL); settle_ime(host);
        assert(!host->ime_provider && !host->ime_session && !strcmp(lv_textarea_get_text(target),"a"));
        pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
        // Release-only keys and held backspace through actual pointer events.
        lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); lv_refr_now(NULL); settle_ime(host);
        key(test,"中/英");
        lv_textarea_set_text(target,""); settle_ime(host);
        pointer_key(test,"a",0); assert(!strcmp(lv_textarea_get_text(target),"a"));
        pointer_key(test,"b",0); assert(!strcmp(lv_textarea_get_text(target),"ab"));
        pointer_key(test,LV_SYMBOL_BACKSPACE,150); assert(!strcmp(lv_textarea_get_text(target),""));
        immediate_backspace(test,target);
        assert_edit_controls(test);
        key(test,LV_SYMBOL_UP); key(test,"A");
        assert(!strcmp(lv_textarea_get_text(target),"A") && find_matrix(lv_layer_top(),"a"));
        key(test,LV_SYMBOL_UP); key(test,LV_SYMBOL_UP); key(test,"B");
        assert(!strcmp(lv_textarea_get_text(target),"AB") && find_matrix(lv_layer_top(),"A"));
        key(test,LV_SYMBOL_UP); key(test,"c"); assert(!strcmp(lv_textarea_get_text(target),"ABc"));
        key(test,"九键"); lv_textarea_set_text(target,""); settle_ime(host);
        pointer_key(test,"2 abc",0); pointer_key(test,"2 abc",0); pointer_key(test,"3 def",0);
        assert(!strcmp(lv_textarea_get_text(target),"bd")); capture(test,"english-nine");
        pointer_key(test,"6 mno",100); assert(!strcmp(lv_textarea_get_text(target),"bd6"));
        key(test,LV_SYMBOL_UP); key(test,"2 ABC"); key(test,"2 ABC"); key(test,"3 DEF");
        assert(!strcmp(lv_textarea_get_text(target),"bd6Bd") && find_matrix(lv_layer_top(),"2 abc"));
        // A rejected tap must not arm replacement of an existing character.
        lv_textarea_set_text(target,"X"); lv_textarea_set_max_length(target,1);
        key(test,"2 abc"); key(test,"2 abc");
        assert(!strcmp(lv_textarea_get_text(target),"X"));
        lv_textarea_set_max_length(target,0); lv_textarea_set_text(target,"");
        lv_textarea_set_accepted_chars(target,"a");
        key(test,"2 abc"); key(test,"2 abc");
        assert(!strcmp(lv_textarea_get_text(target),"a"));
        lv_textarea_set_accepted_chars(target,NULL);
        // External model changes invalidate a pending multi-tap replacement.
        lv_textarea_set_text(target,""); key(test,"2 abc");
        lv_textarea_set_text(target,"外部"); key(test,"2 abc");
        assert(!strcmp(lv_textarea_get_text(target),"外部a"));
        key(test,"全键"); capture(test,"english-full");
        lv_textarea_set_text(target,""); settle_ime(host);
        pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
        // Rapid input is queued and retried, while stale candidates are inert.
        host->ime_config.input=blocked_input;
        lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); lv_refr_now(NULL); settle_ime(host);
        key(test,"全键"); blocked=3;
        key(test,"n"); key(test,"i");
        assert(!strcmp(lv_textarea_get_text(target),""));
        for(unsigned retry=0;retry<12;++retry) { SDL_Delay(10); lv_timer_handler(); settle_ime(host); }
        assert(!blocked && find_matrix(lv_layer_top(),"你")); key(test,"你");
        assert(!strcmp(lv_textarea_get_text(target),"你"));
        pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
        host->ime_config.input=ime_input;
        // A native password field never starts a provider or forwards its text.
        lv_obj_t *password=lv_textarea_create(lv_screen_active()); lv_textarea_set_password_mode(password,true);
        ime_editor_focus(host,password,true); lv_refr_now(NULL); settle_ime(host); assert(!host->ime_provider);
        key(test,"a"); assert(!strcmp(lv_textarea_get_text(password),"a")); lv_obj_delete(password);
        settle_ime(host); assert(!host->ime_keyboard);
        // A provider fault leaves an independent native keyboard usable.
        lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); lv_refr_now(NULL); settle_ime(host); assert(host->ime_provider);
        assert(pxa_component_abort(host->runtime,host->ime_component,PXA_STOP_FAULT)==PXA_STATUS_OK);
        settle_ime(host); assert(!host->ime_provider && host->ime_keyboard);
        assert(find_matrix(lv_layer_top(),"2 ABC") || find_matrix(lv_layer_top(),"q")); capture(test,"fallback");
        pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
        // An expired provider call cannot terminate the foreground component.
        pxa_component_snapshot_t before_expiration;
        assert(pxa_component_snapshot(host->runtime,host->active_component,&before_expiration)==PXA_STATUS_OK);
        assert(before_expiration.state==PXA_COMPONENT_RUNNING);
        host->ime_failed=0;
        lv_obj_send_event(target,LV_EVENT_CLICKED,NULL); lv_refr_now(NULL); settle_ime(host);
        assert(host->ime_provider); key(test,"全键"); expire_provider=true;
        key(test,"n"); settle_ime(host);
        assert(!expire_provider && !host->ime_provider && host->ime_keyboard);
        pxa_component_snapshot_t foreground;
        assert(pxa_component_snapshot(host->runtime,host->active_component,&foreground)==PXA_STATUS_OK);
        assert(foreground.state==PXA_COMPONENT_RUNNING);
        pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
        // Move an occluded editor in an existing scroll container, then restore.
        host->ime_failed=0;
        lv_obj_t *form=lv_obj_create(lv_screen_active());
        lv_obj_set_size(form,host->width,host->height); lv_obj_set_pos(form,0,0);
        lv_obj_t *bottom_editor=lv_textarea_create(form);
        lv_obj_set_size(bottom_editor,host->width/2,30);
        lv_obj_set_pos(bottom_editor,0,host->height-50);
        lv_obj_update_layout(form); int32_t previous_scroll=lv_obj_get_scroll_y(form);
        ime_editor_focus(host,bottom_editor,true); lv_refr_now(NULL); settle_ime(host);
        lv_obj_update_layout(form);
        lv_area_t field,keyboard;
        lv_obj_get_coords(bottom_editor,&field); lv_obj_get_coords(pxa_lvgl_ime_root(host->ime_keyboard),&keyboard);
        assert(field.y2<keyboard.y1 || pxa_lvgl_ime_editor_view(host->ime_keyboard));
        pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
        assert(lv_obj_get_scroll_y(form)==previous_scroll);
        // Deleting the form before keyboard close must clear the borrowed parent.
        ime_editor_focus(host,bottom_editor,true); lv_refr_now(NULL); settle_ime(host);
        lv_obj_delete(form); settle_ime(host); assert(!host->ime_keyboard);
        // A fixed multi-line editor cannot scroll out from under the keyboard.
        form=lv_obj_create(lv_screen_active()); lv_obj_set_size(form,host->width,host->height);
        lv_obj_set_scrollable(form,false);
        bottom_editor=lv_textarea_create(form); lv_obj_set_floating(bottom_editor,true);
        lv_obj_set_size(bottom_editor,host->width-30,60);
        lv_obj_set_pos(bottom_editor,15,host->height-70);
        lv_textarea_set_text(bottom_editor,"前面的内容\n这里是正在输入的这一行，测试光标与键盘。\n后面的内容");
        lv_textarea_set_cursor_pos(bottom_editor,12);
        // A full-screen overlay parent may itself have padding. Display-safe
        // coordinates must remain absolute instead of inheriting that offset.
        lv_obj_t *keyboard_parent=lv_obj_create(lv_layer_top());
        lv_obj_set_size(keyboard_parent,host->width,host->height);
        lv_obj_set_pos(keyboard_parent,0,0); lv_obj_set_style_pad_all(keyboard_parent,11,0);
        lv_obj_set_style_border_width(keyboard_parent,0,0);
        lv_obj_set_style_bg_opa(keyboard_parent,LV_OPA_TRANSP,0);
        lv_obj_set_scrollable(keyboard_parent,false);
        host->ime_config.parent=keyboard_parent;
        ime_editor_focus(host,bottom_editor,true); lv_refr_now(NULL); settle_ime(host);
        lv_obj_t *view=pxa_lvgl_ime_editor_view(host->ime_keyboard); assert(view);
        capture(test,"fixed-multiline");
        key(test,"中/英"); key(test,"x");
        assert(strstr(lv_textarea_get_text(bottom_editor),"x"));
        assert(strstr(lv_label_get_text(lv_obj_get_child(view,0)),"x"));
        key(test,LV_SYMBOL_NEW_LINE); assert(pxa_lvgl_ime_active(host->ime_keyboard));
        assert(strstr(lv_textarea_get_text(bottom_editor),"x\n"));
        unsigned cursor_before=lv_textarea_get_cursor_pos(bottom_editor);
        const char *original_text=lv_textarea_get_text(bottom_editor);
        char saved_text[256]; assert(strlen(original_text)<sizeof(saved_text)); strcpy(saved_text,original_text);
        drag_space(test);
        assert(lv_textarea_get_cursor_pos(bottom_editor)<cursor_before);
        assert(!strcmp(lv_textarea_get_text(bottom_editor),saved_text));
        lv_textarea_set_cursor_pos(bottom_editor,3);
        unsigned before=lv_textarea_get_cursor_pos(bottom_editor);
        pointer_matrix_key(test,find_matrix(view,LV_SYMBOL_LEFT),LV_SYMBOL_LEFT,0);
        assert(lv_textarea_get_cursor_pos(bottom_editor)==before-1);
        pointer_matrix_key(test,find_matrix(view,LV_SYMBOL_RIGHT),LV_SYMBOL_RIGHT,100);
        assert(lv_textarea_get_cursor_pos(bottom_editor)>before-1);
        lv_textarea_set_cursor_pos(bottom_editor,0);
        for (unsigned i=0;i<12;++i) { SDL_Delay(10); lv_timer_handler(); settle_ime(host); }
        assert(!strncmp(lv_label_get_text(lv_obj_get_child(view,0)),"前",3));
        capture(test,"fixed-caret-start");
        pxa_ui_environment_t changed=host->ime_config.environment;
        changed.font_scale_q16=65536; changed.density_q16=65536;
        pxa_lvgl_ime_set_environment(host->ime_keyboard,&changed); lv_refr_now(NULL);
        assert(pxa_lvgl_ime_editor_view(host->ime_keyboard));
        capture(test,"environment-update");
        // A live font/theme refresh must immediately restore usable key maps,
        // without waiting for a decoder reply (English has no provider).
        pxa_lvgl_ui_theme_t compact=host->ime_config.theme;
        compact.body_font=compact.label_font=compact.caption_font=LV_FONT_DEFAULT;
        pxa_lvgl_ime_set_theme(host->ime_keyboard,&compact);
        assert_keyboard_bounds(test);
        pointer_matrix_key(test,find_matrix(lv_layer_top(),"q"),"q",0);
        assert(lv_textarea_get_text(bottom_editor)[0]=='q');
        pxa_lvgl_ime_set_theme(host->ime_keyboard,&host->ime_config.theme);
        assert_keyboard_bounds(test);
        // Model updates can replace UTF-8 text without moving the caret.
        // The current-line aid must refresh changed text and clear old tails.
        lv_textarea_set_text(bottom_editor,"正文abc"); lv_textarea_set_cursor_pos(bottom_editor,0);
        SDL_Delay(100); lv_timer_handler(); settle_ime(host);
        assert(!strncmp(lv_label_get_text(lv_obj_get_child(view,0)),"正",3));
        lv_textarea_set_text(bottom_editor,"内容xyz"); lv_textarea_set_cursor_pos(bottom_editor,0);
        SDL_Delay(100); lv_timer_handler(); settle_ime(host);
        assert(!strncmp(lv_label_get_text(lv_obj_get_child(view,0)),"内",3));
        lv_textarea_set_text(bottom_editor,""); settle_ime(host);
        assert(!*lv_label_get_text(lv_obj_get_child(view,0)));
        pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
        host->ime_config.parent=lv_layer_top(); lv_obj_delete(keyboard_parent);
        lv_obj_delete(form);
        // Password context is always masked, including the current-line aid.
        password=lv_textarea_create(lv_screen_active()); lv_obj_set_floating(password,true);
        lv_obj_set_size(password,host->width-30,32); lv_obj_set_pos(password,15,host->height-45);
        lv_textarea_set_password_mode(password,true); lv_textarea_set_text(password,"secret-password");
        ime_editor_focus(host,password,true); lv_refr_now(NULL); settle_ime(host);
        view=pxa_lvgl_ime_editor_view(host->ime_keyboard); assert(view && !host->ime_provider);
        assert(!strstr(lv_label_get_text(lv_obj_get_child(view,0)),"secret"));
        assert(strchr(lv_label_get_text(lv_obj_get_child(view,0)),'*'));
        capture(test,"password-masked");
        // Changing the property while focused invalidates queued Chinese
        // replies, unloads the provider and masks the existing current line.
        lv_textarea_set_password_mode(password,false);
        assert(pxa_lvgl_ime_focus(host->ime_keyboard,password)==PXA_STATUS_OK);
        settle_ime(host); assert(host->ime_provider);
        uint64_t previous_generation=pxa_lvgl_ime_generation(host->ime_keyboard);
        lv_textarea_set_password_mode(password,true);
        pxa_ime_frame_t stale={.sequence=2,.edit=PXA_IME_EDIT_INSERT,
            .commit={(const uint8_t *)"你",3}};
        assert(pxa_lvgl_ime_present(host->ime_keyboard,previous_generation,&stale)==PXA_STATUS_BAD_STATE);
        settle_ime(host); assert(!host->ime_provider);
        assert(!strcmp(lv_textarea_get_text(password),"secret-password"));
        assert(!strstr(lv_label_get_text(lv_obj_get_child(view,0)),"secret"));
        assert(strchr(lv_label_get_text(lv_obj_get_child(view,0)),'*'));
        key(test,"a"); assert(!strcmp(lv_textarea_get_text(password),"secret-passworda"));
        lv_obj_delete(password); settle_ime(host); assert(!host->ime_keyboard);
        // Persisted defaults apply to ordinary Guest TextInputs without loading
        // Chinese for English; switching back restores the selected layout.
        char preferences_path[1536];
        snprintf(preferences_path,sizeof(preferences_path),"%s/input-method-preferences",host->ime_preferences_root);
        assert(pxa_posix_ime_preferences_write(preferences_path,(pxa_ime_preferences_t){PXA_IME_LAYOUT_NINE,PXA_IME_ENGLISH})==PXA_STATUS_OK);
        lv_textarea_set_text(target,"");
        ime_editor_focus(host,target,true); lv_refr_now(NULL); settle_ime(host);
        assert(!host->ime_provider && find_matrix(lv_layer_top(),LV_SYMBOL_UP));
        pointer_key(test,"a",0); assert(!strcmp(lv_textarea_get_text(target),"a"));
        pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
        assert(pxa_posix_ime_preferences_write(preferences_path,(pxa_ime_preferences_t){PXA_IME_LAYOUT_NINE,PXA_IME_CHINESE})==PXA_STATUS_OK);
        ime_editor_focus(host,target,true); lv_refr_now(NULL); settle_ime(host);
        assert(host->ime_provider && find_matrix(lv_layer_top(),"2 ABC"));
        pxa_lvgl_ime_close(host->ime_keyboard); settle_ime(host);
        assert(unlink(preferences_path)==0);
        // Include all reopen, password and environment-update paths in this
        // fresh engine's peak, rather than stopping at its first close.
        assert(pxa_wamr_engine_memory_snapshot(host->engine,&test->closed)==PXA_STATUS_OK);
        assert(test->closed.linear_current_bytes==test->idle.linear_current_bytes);
        char path[1536]; snprintf(path,sizeof(path),"%s/profile-%u-memory.json",test->artifacts,test->repeat);
        FILE *file=fopen(path,"w"); assert(file);
        fprintf(file,"{\"idle_wamr\":%u,\"loaded_wamr\":%u,\"closed_wamr\":%u,\"run_wamr_peak\":%u,"
            "\"idle_linear\":%llu,\"loaded_linear\":%llu,\"closed_linear\":%llu,\"run_linear_peak\":%llu,"
            "\"loaded_artifact_buffers\":%llu,\"closed_artifact_buffers\":%llu,\"loaded_event_buffers\":%llu}\n",
            test->idle.current_bytes,test->loaded.current_bytes,test->closed.current_bytes,test->closed.peak_bytes,
            (unsigned long long)test->idle.linear_current_bytes,(unsigned long long)test->loaded.linear_current_bytes,
            (unsigned long long)test->closed.linear_current_bytes,(unsigned long long)test->closed.linear_peak_bytes,
            (unsigned long long)test->loaded.artifact_buffer_bytes,
            (unsigned long long)test->closed.artifact_buffer_bytes,
            (unsigned long long)test->loaded.event_buffer_bytes);
        assert(!fclose(file));
        test->stage=2; host->exit_requested=1;
    }
}
int main(int argc,char **argv) {
    assert(argc>=6);
    bool expect_load_failure=false, single_character_provider=false;
    for (int i=6;i<argc;++i) {
        if (!strcmp(argv[i],"--expect-load-failure")) expect_load_failure=true;
        else if (!strcmp(argv[i],"--single-character-provider")) single_character_provider=true;
        else assert(!"Unknown IME test option");
    }
    setenv("SDL_VIDEODRIVER","dummy",1); setenv("SDL_AUDIODRIVER","dummy",1);
    setenv("PXA_IME_PACKAGE",argv[3],1);
    options_t options={0}; options.package_path=argv[1]; options.publisher_key=argv[2];
    options.state_root=argv[4]; options.locale="zh-CN";
    static const unsigned profiles[][6]={
        {240,320,160,0,0,0}, {480,480,320,1,48,0}, {454,454,160,2,0,24},
        {390,844,160,0,0,0}, {296,240,160,1,58,8},
        {160,160,160,1,40,4}, {240,240,480,2,0,4}, {320,180,320,1,20,4},
        {1280,800,160,0,0,8}, {720,720,480,2,0,16}, {320,320,640,1,80,8},
        {800,480,240,1,56,8}, {160,320,320,2,0,4},
        {480,480,305,1,58,12} // Actual esp-mosaico density and safe contour.
    };
    for (unsigned profile=0;profile<sizeof(profiles)/sizeof(profiles[0]);++profile) {
        options.width=profiles[profile][0]; options.height=profiles[profile][1];
        options.density_dpi=profiles[profile][2]; options.display_shape=profiles[profile][3];
        options.corner_radius=profiles[profile][4];
        for (unsigned i=0;i<4;++i) options.safe_insets[i]=profiles[profile][5];
        if (profile==4) options.safe_insets[1]=options.safe_insets[3]=10;
        ime_test_t test={0}; test.started=now_us(NULL); test.repeat=profile; test.artifacts=argv[5];
        test.expect_load_failure=expect_load_failure;
        test.single_character_provider=single_character_provider;
        assert(!run_product_simulator(&options,NULL,NULL,pump_test,&test,NULL,&test,bind_test,NULL,NULL,NULL));
        assert(!test.host && test.stage==2);
    }
    puts(expect_load_failure ? "PXA IME: rejected provider, native fallback and memory recovery passed" :
        "PXA IME: separate signed provider, composition, Chinese/English edits, submit and 8 unload/restart cycles passed");
}

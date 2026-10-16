#include "pxsys/reference_ime.h"
#include "pxa_lvgl_ime_builtin.h"
#include "pxa/lvgl/pxa_lvgl_ime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
static lv_point_t pointer_point;
static bool pointer_down;
static void read_pointer(lv_indev_t *indev,lv_indev_data_t *data) {
    (void)indev; data->point=pointer_point;
    data->state=pointer_down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
static void check_immediate_delete(lv_obj_t *keyboard,lv_obj_t *input) {
    lv_obj_update_layout(keyboard);
    unsigned index=0;
    while (lv_buttonmatrix_get_button_text(keyboard,index) &&
           strcmp(lv_buttonmatrix_get_button_text(keyboard,index),LV_SYMBOL_BACKSPACE)) ++index;
    assert(lv_buttonmatrix_get_button_text(keyboard,index));
    lv_area_t area; lv_obj_get_coords(keyboard,&area);
    lv_area_t cell=((lv_buttonmatrix_t *)keyboard)->button_areas[index];
    pointer_point=(lv_point_t){area.x1+(cell.x1+cell.x2)/2,area.y1+(cell.y1+cell.y2)/2};
    lv_indev_t *pointer=lv_indev_create(); lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer,read_pointer);
    lv_textarea_set_text(input,"abcd");
    pointer_down=true; lv_indev_read(pointer);
    assert(!strcmp(lv_textarea_get_text(input),"abc"));
    pointer_down=false; lv_indev_read(pointer);
    assert(!strcmp(lv_textarea_get_text(input),"abc"));
    lv_indev_delete(pointer);
}


static unsigned closed,submitted;
static void protect_after_edit(lv_event_t *event) {
    lv_obj_t *input=lv_event_get_target(event);
    if (*lv_textarea_get_text(input)) lv_textarea_set_password_mode(input,true);
}
static void on_submit(lv_event_t *event) { (void)event; ++submitted; }
static void on_close(void *context) { ++*(unsigned *)context; }
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *pixels) {
    (void)area; (void)pixels; lv_display_flush_ready(d);
}
static lv_obj_t *find(lv_obj_t *root,const char *text,unsigned *index) {
    if (lv_obj_check_type(root,&lv_buttonmatrix_class)) {
        for (unsigned i=0;lv_buttonmatrix_get_button_text(root,i);++i)
            if (!strcmp(lv_buttonmatrix_get_button_text(root,i),text)) { *index=i; return root; }
    }
    for (unsigned i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *found=find(lv_obj_get_child(root,i),text,index); if (found) return found;
    }
    return NULL;
}
static void root_key(lv_obj_t *root,const char *text) {
    unsigned index=0; lv_obj_t *matrix=find(root,text,&index);
    if (!matrix) fprintf(stderr,"Missing native key: %s\n",text);
    assert(matrix);
    lv_buttonmatrix_set_selected_button(matrix,index);
    lv_obj_send_event(matrix,LV_EVENT_VALUE_CHANGED,NULL);
}
static void key(pxsys_reference_ime_t *ime,const char *text) {
    root_key(pxsys_reference_ime_get_root(ime),text);
}
static void assert_keyboard_restored(pxsys_reference_ime_t *ime) {
    lv_obj_t *root=pxsys_reference_ime_get_root(ime);unsigned index=0;
    lv_obj_t *letters=find(root,"q",&index);
    if (!letters) letters=find(root,"2 ABC",&index);
    assert(letters && !lv_obj_is_hidden(letters));
    lv_obj_t *candidates=find(root,LV_SYMBOL_SETTINGS,&index);assert(candidates);
    lv_obj_update_layout(root);lv_area_t words,keys;
    lv_obj_get_coords(candidates,&words);lv_obj_get_coords(letters,&keys);
    assert(words.y2<keys.y1);
}
static void tool_cursor(pxsys_reference_ime_t *ime,bool right) {
    unsigned index=0;
    lv_obj_t *matrix=find(pxsys_reference_ime_get_root(ime),"返回",&index);assert(matrix);
    const char *arrow=right?LV_SYMBOL_RIGHT:LV_SYMBOL_LEFT;
    for(index=0;lv_buttonmatrix_get_button_text(matrix,index);++index)
        if(!strcmp(lv_buttonmatrix_get_button_text(matrix,index),arrow)) break;
    assert(lv_buttonmatrix_get_button_text(matrix,index));
    lv_buttonmatrix_set_selected_button(matrix,index);
    lv_obj_send_event(matrix,LV_EVENT_VALUE_CHANGED,NULL);
}
static void ignore_focus(void *context,uint64_t generation,bool active,bool provider) {
    (void)context;(void)generation;(void)active;(void)provider;
}
static void check_cursor_boundary(lv_obj_t *input) {
    pxa_lvgl_ime_config_t config={.struct_size=sizeof(config),.parent=lv_screen_active(),
        .environment={.width=296,.height=240,.density_q16=65536},
        .theme={.body_font=LV_FONT_DEFAULT,.icon_font=LV_FONT_DEFAULT},
        .preferences={.language=PXA_IME_ENGLISH},.focus=ignore_focus};
    pxa_lvgl_ime_t *ime=pxa_lvgl_ime_create(&config);assert(ime);
    lv_textarea_set_text(input,"你𠀀");lv_textarea_set_cursor_pos(input,0);
    assert(pxa_lvgl_ime_focus(ime,input)==PXA_STATUS_OK);
    lv_obj_t *root=pxa_lvgl_ime_root(ime);root_key(root,LV_SYMBOL_SETTINGS);
    uint64_t generation=pxa_lvgl_ime_generation(ime);
    root_key(root,LV_SYMBOL_LEFT);assert(pxa_lvgl_ime_generation(ime)==generation);
    root_key(root,LV_SYMBOL_RIGHT);assert(pxa_lvgl_ime_generation(ime)>generation);
    root_key(root,LV_SYMBOL_RIGHT);generation=pxa_lvgl_ime_generation(ime);
    root_key(root,LV_SYMBOL_RIGHT);assert(pxa_lvgl_ime_generation(ime)==generation);
    assert(lv_textarea_get_cursor_pos(input)==2);
    pxa_lvgl_ime_destroy(ime);lv_textarea_set_text(input,"");
}
static void check_cursor_hold(pxsys_reference_ime_t *ime,lv_obj_t *input) {
    lv_textarea_set_text(input,"A你𠀀BCDE");key(ime,LV_SYMBOL_SETTINGS);
    unsigned index=0;lv_obj_t *matrix=find(pxsys_reference_ime_get_root(ime),"返回",&index);assert(matrix);
    for(index=0;strcmp(lv_buttonmatrix_get_button_text(matrix,index),LV_SYMBOL_LEFT);++index) {}
    lv_obj_update_layout(matrix);lv_area_t area;lv_obj_get_coords(matrix,&area);
    lv_area_t cell=((lv_buttonmatrix_t *)matrix)->button_areas[index];
    pointer_point=(lv_point_t){area.x1+(cell.x1+cell.x2)/2,area.y1+(cell.y1+cell.y2)/2};
    lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer,read_pointer);lv_indev_set_long_press_time(pointer,100);
    lv_indev_set_long_press_repeat_time(pointer,50);
    pointer_down=true;lv_indev_read(pointer);assert(lv_textarea_get_cursor_pos(input)==6);
    lv_tick_inc(120);lv_indev_read(pointer);
    lv_tick_inc(60);lv_indev_read(pointer);assert(lv_textarea_get_cursor_pos(input)==5);
    lv_tick_inc(60);lv_indev_read(pointer);assert(lv_textarea_get_cursor_pos(input)==4);
    pointer_down=false;lv_indev_read(pointer);assert(lv_textarea_get_cursor_pos(input)==4);
    assert(!strcmp(lv_textarea_get_text(input),"A你𠀀BCDE"));
    lv_indev_delete(pointer);key(ime,"返回");
}
int main(void) {
    lv_init();
    lv_display_t *display=lv_display_create(296,240);
    static uint8_t buffer[1280*10*4];
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    pxsys_display_profile_t profile; pxsys_display_profile_init(&profile,296,240);
    pxsys_theme_snapshot_t colors={0};
    colors.colors[PXSYS_COLOR_ON_SURFACE]=0xffffff;
    colors.colors[PXSYS_COLOR_SURFACE_CONTAINER]=0x202830;
    colors.colors[PXSYS_COLOR_SURFACE_CONTAINER_HIGHEST]=0x344454;
    colors.colors[PXSYS_COLOR_PRIMARY_CONTAINER]=0x1565c0;
    colors.colors[PXSYS_COLOR_ON_PRIMARY_CONTAINER]=0xffffff;
    pxsys_reference_ime_config_t config={.display=&profile,.theme=&colors,
        .caption_font=LV_FONT_DEFAULT,.body_font=LV_FONT_DEFAULT,.icon_font=LV_FONT_DEFAULT,
        .closed=on_close,.context=&closed};
    lv_obj_t *input=lv_textarea_create(lv_screen_active());
    lv_obj_set_size(input,240,36); lv_textarea_set_text(input,"");
    lv_textarea_set_one_line(input,true); lv_obj_add_event_cb(input,on_submit,LV_EVENT_READY,NULL);
    check_cursor_boundary(input);
    pxsys_reference_ime_t *ime=pxsys_reference_ime_create(lv_screen_active(),input,&config);
    assert(ime && pxsys_reference_ime_is_visible(ime));
    if(!pxa_lvgl_ime_builtin_scripts()) {
        key(ime,"q");assert(!strcmp(lv_textarea_get_text(input),"q"));key(ime,"完成");
        pxsys_reference_ime_destroy(ime);lv_obj_delete(input);lv_display_delete(display);lv_deinit();
        puts("Builtin dictionaries disabled: local English keyboard passed");return 0;
    }
    key(ime,"6 MNO"); key(ime,"4 GHI"); key(ime,"ni");
    key(ime,LV_SYMBOL_UP);key(ime,"你");assert_keyboard_restored(ime);
    assert(!strcmp(lv_textarea_get_text(input),"你"));
    key(ime,LV_SYMBOL_SETTINGS); key(ime,"全键");
    key(ime,"h"); key(ime,"a"); key(ime,"o"); key(ime,"好");
    assert(!strcmp(lv_textarea_get_text(input),"你好"));
    // A partial single-character commit must also restore typing keys while
    // retaining the remaining syllable and its candidates.
    lv_textarea_set_text(input,"");
    key(ime,"n");key(ime,"i");key(ime,"h");key(ime,"a");key(ime,"o");
    key(ime,LV_SYMBOL_UP);key(ime,"你");assert_keyboard_restored(ime);
    assert(!strcmp(lv_textarea_get_text(input),"你"));
    key(ime,LV_SYMBOL_UP);key(ime,"好");assert_keyboard_restored(ime);
    assert(!strcmp(lv_textarea_get_text(input),"你好"));
    // Tool arrows move by Unicode characters and stay open for repeated taps.
    lv_textarea_set_text(input,"A你𠀀B");lv_textarea_set_cursor_pos(input,0);
    key(ime,LV_SYMBOL_SETTINGS);tool_cursor(ime,false);
    assert(lv_textarea_get_cursor_pos(input)==0);
    for(unsigned i=1;i<=4;++i) {tool_cursor(ime,true);assert(lv_textarea_get_cursor_pos(input)==i);}
    tool_cursor(ime,true);assert(lv_textarea_get_cursor_pos(input)==4);
    tool_cursor(ime,false);assert(lv_textarea_get_cursor_pos(input)==3);
    tool_cursor(ime,false);assert(lv_textarea_get_cursor_pos(input)==2);
    assert(!strcmp(lv_textarea_get_text(input),"A你𠀀B"));
    key(ime,"返回");key(ime,"a");key(ime,"i");
    key(ime,LV_SYMBOL_SETTINGS);tool_cursor(ime,false);
    assert(!strcmp(lv_textarea_get_text(input),pxa_lvgl_ime_builtin_scripts()&1?"A你爱𠀀B":"A你愛𠀀B"));
    assert(lv_textarea_get_cursor_pos(input)==2);
    key(ime,"返回");lv_textarea_set_text(input,"你好");
    check_cursor_hold(ime,input);lv_textarea_set_text(input,"你好");
    key(ime,"123"); key(ime,"7"); key(ime,"返回");
    key(ime,"符"); key(ime,"，"); key(ime,"下一页"); key(ime,"@"); key(ime,"返回");
    assert(!strcmp(lv_textarea_get_text(input),"你好7，@"));
    key(ime,"中"); key(ime,"a"); key(ime,"b"); key(ime,"c");
    unsigned index=0; lv_obj_t *matrix=find(pxsys_reference_ime_get_root(ime),"a",&index);
    assert(matrix); check_immediate_delete(matrix,input);
    key(ime,"完成"); assert(submitted==1 && closed==1 && !pxsys_reference_ime_is_visible(ime));
    pxsys_reference_ime_destroy(ime);
    // An editor observer can make a committed field sensitive while PRESENT
    // is still running; the reply must survive local decoder destruction.
    lv_textarea_set_text(input,""); config.layout=1; config.language=0;
    lv_obj_add_event_cb(input,protect_after_edit,LV_EVENT_VALUE_CHANGED,NULL);
    ime=pxsys_reference_ime_create(lv_screen_active(),input,&config); assert(ime);
    key(ime,"n"); key(ime,"i"); key(ime,"h"); key(ime,"a"); key(ime,"o"); key(ime,"你");
    assert(lv_textarea_get_password_mode(input) && !strcmp(lv_textarea_get_text(input),"你"));
    assert(!find(pxsys_reference_ime_get_root(ime),"好",&index));
    key(ime,"q"); assert(!strcmp(lv_textarea_get_text(input),"你q"));
    lv_obj_remove_event_cb(input,protect_after_edit); pxsys_reference_ime_destroy(ime);
    lv_textarea_set_password_mode(input,false);
    // Both dictionaries start simplified; switching confirms pending text in
    // its original script and applies the next script to subsequent input.
    lv_textarea_set_text(input,"");config.layout=1;config.language=0;
    ime=pxsys_reference_ime_create(lv_screen_active(),input,&config);assert(ime);
    key(ime,"a");key(ime,"i");
    if(pxa_lvgl_ime_builtin_scripts()==3) {
        key(ime,LV_SYMBOL_SETTINGS);key(ime,"繁體");
        assert(!strcmp(lv_textarea_get_text(input),"爱"));
        key(ime,"a");key(ime,"i");key(ime,"愛");
        assert(!strcmp(lv_textarea_get_text(input),"爱愛"));
        key(ime,LV_SYMBOL_SETTINGS);key(ime,"简体");
        key(ime,"a");key(ime,"i");key(ime,"爱");
        assert(!strcmp(lv_textarea_get_text(input),"爱愛爱"));
    } else key(ime,pxa_lvgl_ime_builtin_scripts()&1?"爱":"愛");
    pxsys_reference_ime_destroy(ime);
    static const unsigned profiles[][4]={{240,320,160,0},{480,480,320,1},{454,454,160,2},
        {390,844,160,0},{296,240,160,1},{160,160,160,1},{240,240,480,2},
        {320,180,320,1},{1280,800,160,0},{720,720,480,2},{320,320,640,1},
        {800,480,240,1},{160,320,320,2}};
    for (unsigned n=0;n<sizeof(profiles)/sizeof(*profiles);++n) {
        lv_display_set_resolution(display,profiles[n][0],profiles[n][1]);
        pxsys_display_profile_init(&profile,profiles[n][0],profiles[n][1]);
        profile.density_dpi=(uint16_t)profiles[n][2]; profile.shape=(pxsys_display_shape_t)profiles[n][3];
        profile.corner_radii=(pxsys_corner_radii_t){20,20,20,20};
        profile.safe_insets=(pxsys_insets_t){4,4,4,4};
        for (unsigned pass=0;pass<4;++pass) {
            config.language=(uint8_t)(pass&1); config.layout=(uint8_t)(pass%3);
            lv_textarea_set_password_mode(input,pass==3); lv_textarea_set_text(input,"");
            ime=pxsys_reference_ime_create(lv_screen_active(),input,&config); assert(ime);
            colors.colors[PXSYS_COLOR_SURFACE_CONTAINER]^=0x101010;
            pxsys_reference_ime_update(ime,&config);
            if(pass==0) {key(ime,LV_SYMBOL_SETTINGS);tool_cursor(ime,false);key(ime,"返回");}
            lv_obj_t *root=pxsys_reference_ime_get_root(ime); lv_obj_update_layout(root);
            lv_area_t box; lv_obj_get_coords(root,&box);
            assert(box.x1>=0 && box.y1>=0 && box.x2<(int)profile.width && box.y2<(int)profile.height);
            if (pass==3) {
                key(ime,"q"); assert(!strcmp(lv_textarea_get_text(input),"q"));
                key(ime,LV_SYMBOL_SETTINGS);tool_cursor(ime,false);
                assert(lv_textarea_get_cursor_pos(input)==0);
                tool_cursor(ime,true);assert(lv_textarea_get_cursor_pos(input)==1);
                key(ime,"返回");
            }
            key(ime,LV_SYMBOL_DOWN); assert(!pxsys_reference_ime_is_visible(ime));
            pxsys_reference_ime_destroy(ime);
        }
    }
    lv_textarea_set_password_mode(input,false);
    ime=pxsys_reference_ime_create(lv_screen_active(),input,&config); assert(ime);
    lv_obj_delete(input); assert(!pxsys_reference_ime_is_visible(ime));
    pxsys_reference_ime_destroy(ime); lv_display_delete(display); lv_deinit();
    puts("Native shared keyboard: Chinese/English, QWERTY/nine, symbols, immediate delete, submit, password, 13 screen profiles and editor deletion passed");
}

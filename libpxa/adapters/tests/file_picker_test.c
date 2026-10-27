/* SPDX-License-Identifier: MIT */
#include "pxa/lvgl/pxa_lvgl_file_picker.h"
#include "pxa/ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned notifications;
static char current_path[1025];
static size_t current_skip;
static uint64_t current_epoch;
static void overlay(lv_obj_t *object,int action) {(void)object;(void)action;}
static void changed(void *context) {(void)context;++notifications;}
static pxa_status_t browse(void *context,uint64_t epoch,const char *path,const char *filter,size_t skip,
    pxa_file_picker_entry_t *entries,size_t capacity,size_t *count,uint8_t *more) {
    (void)context;(void)capacity;assert(!strcmp(filter,"gb,gbc"));*more=0;*count=0;
    strcpy(current_path,path);current_skip=skip;current_epoch=epoch;
    if(skip)return 0;
    if(!epoch) {entries[0]=(pxa_file_picker_entry_t){42,2,strdup("Storage")};*count=1;return 0;}
    assert(epoch==42);
    if(!*path) {
        entries[0]=(pxa_file_picker_entry_t){0,1,"one.gb"};
        entries[1]=(pxa_file_picker_entry_t){0,1,"two.gbc"};
        entries[2]=(pxa_file_picker_entry_t){0,2,"Folder"};*count=3;
    } else {assert(!strcmp(path,"Folder"));entries[0]=(pxa_file_picker_entry_t){0,1,"three.gb"};*count=1;}
    for(size_t i=0;i<*count;++i)entries[i].name=strdup(entries[i].name);
    return 0;
}
static void press(pxa_lvgl_file_picker_t *p,uint32_t key) {
    static uint64_t clock=1;pxa_lvgl_file_picker_controller(p,key,clock++);
    pxa_lvgl_file_picker_controller(p,0,clock++);
}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data) {
    (void)area;(void)data;lv_display_flush_ready(display);
}
static lv_obj_t *find_text(lv_obj_t *root,const char *text) {
    if(lv_obj_has_class(root,&lv_label_class) && !strcmp(lv_label_get_text(root),text))return root;
    for(uint32_t i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *found=find_text(lv_obj_get_child(root,(int32_t)i),text);if(found)return found;
    }
    return NULL;
}
static void click_text(const char *text) {
    lv_obj_t *label=find_text(lv_layer_top(),text);assert(label);
    lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);
}
static void test(unsigned multiple) {
    pxa_lvgl_file_picker_config_t config={.browse=browse,.changed=changed,.multiple=multiple,
        .app_name="Test",.purpose="Copy ROMs",.extensions="gb,gbc"};
    config.style.label_font=LV_FONT_DEFAULT;config.style.spacing=3;config.style.radius=3;
    pxa_lvgl_file_picker_t *p=pxa_lvgl_file_picker_create(&config);assert(p);
    press(p,PXA_UI_CONTROLLER_A); /* volume */
    press(p,PXA_UI_CONTROLLER_A); /* one.gb */
    size_t count;const pxa_file_choice_t *choices=pxa_lvgl_file_picker_selection(p,&count);
    assert(count==1 && !strcmp(choices[0].path,"one.gb"));
    press(p,PXA_UI_CONTROLLER_DOWN);press(p,PXA_UI_CONTROLLER_A); /* two.gbc */
    choices=pxa_lvgl_file_picker_selection(p,&count);
    assert(count==(multiple?2u:1u));
    if(!multiple)assert(!strcmp(choices[0].path,"two.gbc"));
    press(p,PXA_UI_CONTROLLER_DOWN);press(p,PXA_UI_CONTROLLER_A); /* folder */
    press(p,PXA_UI_CONTROLLER_A); /* three.gb */
    choices=pxa_lvgl_file_picker_selection(p,&count);
    assert(count==(multiple?3u:1u) && !strcmp(choices[count-1].path,"Folder/three.gb"));
    press(p,PXA_UI_CONTROLLER_B); /* back preserves selection */
    assert(!current_path[0] && current_epoch==42);
    /* A re-enters the exact folder we just left: Back restored its focus. */
    press(p,PXA_UI_CONTROLLER_A);assert(!strcmp(current_path,"Folder"));
    click_text("Up");assert(!current_path[0] && current_epoch==42);
    choices=pxa_lvgl_file_picker_selection(p,&count);assert(count==(multiple?3u:1u));
    press(p,PXA_UI_CONTROLLER_START);assert(pxa_lvgl_file_picker_state(p)==1);
    pxa_lvgl_file_picker_progress(p,1,count,8192);
    press(p,PXA_UI_CONTROLLER_B);assert(pxa_lvgl_file_picker_state(p)==2);
    pxa_lvgl_file_picker_destroy(p);
}
static pxa_status_t paged(void *context,uint64_t epoch,const char *path,const char *filter,size_t skip,
    pxa_file_picker_entry_t *entries,size_t capacity,size_t *count,uint8_t *more) {
    if(!epoch)return browse(context,epoch,path,filter,skip,entries,capacity,count,more);
    strcpy(current_path,path);current_skip=skip;current_epoch=epoch;*count=0;*more=0;
    if(*path)return PXA_STATUS_UNAVAILABLE;
    if(skip==0) {
        *count=capacity;*more=1;
        for(size_t i=0;i<capacity;++i) {char name[32];snprintf(name,sizeof(name),"game-%zu.gb",i);
            entries[i]=(pxa_file_picker_entry_t){0,1,strdup(name)};}
    } else {assert(skip==PXA_FILE_PICKER_PAGE);*count=2;
        entries[0]=(pxa_file_picker_entry_t){0,1,strdup("last.gb")};
        entries[1]=(pxa_file_picker_entry_t){0,2,strdup("Unavailable")};}
    return 0;
}
static void test_paging(void) {
    pxa_lvgl_file_picker_config_t config={.browse=paged,.app_name="Test",.purpose="Copy ROMs",.extensions="gb,gbc"};
    config.style.spacing=config.style.radius=4;
    pxa_lvgl_file_picker_t *p=pxa_lvgl_file_picker_create(&config);assert(p);
    press(p,PXA_UI_CONTROLLER_A);click_text("Next page");assert(current_skip==12);
    click_text("Unavailable");assert(!strcmp(current_path,"Unavailable"));
    /* Empty/error pages retain a reachable, clearly named parent action. */
    assert(find_text(lv_layer_top(),"Up"));pxa_lvgl_file_picker_back(p);
    assert(current_skip==12 && !current_path[0]);
    press(p,PXA_UI_CONTROLLER_A);assert(!strcmp(current_path,"Unavailable"));
    pxa_lvgl_file_picker_back(p);click_text("Storage");assert(!current_epoch);
    pxa_lvgl_file_picker_back(p);assert(pxa_lvgl_file_picker_state(p)==2);
    pxa_lvgl_file_picker_destroy(p);
}
static void test_geometry(lv_display_t *display,int width,int height,unsigned chinese,unsigned light,int unit) {
    lv_display_set_resolution(display,width,height);
    pxa_lvgl_file_picker_config_t c={.browse=browse,.app_name="Test emulator",.purpose="Select game files",.extensions="gb,gbc",
        .body_font=&lv_font_montserrat_16,.title_font=&lv_font_montserrat_20,.caption_font=&lv_font_montserrat_14,.chinese=chinese};
    c.style.spacing=c.style.radius=unit;c.style.label_font=&lv_font_montserrat_14;
    c.style.panel=lv_color_hex(light?0xf5f5f5:0x202830);c.style.text=lv_color_hex(light?0x202830:0xf5f5f5);
    c.style.primary=lv_color_hex(0x087f5b);c.style.outline=lv_color_hex(0x667788);
    pxa_lvgl_file_picker_t *p=pxa_lvgl_file_picker_create(&c);assert(p);
    lv_obj_t *title=find_text(lv_layer_top(),chinese?"导入文件":"Import files");assert(title);
    lv_obj_t *panel=lv_obj_get_parent(lv_obj_get_parent(title));lv_obj_update_layout(panel);
    lv_area_t bounds;lv_obj_get_coords(panel,&bounds);
    assert(bounds.x1>0 && bounds.y1>0 && bounds.x2<width-1 && bounds.y2<height-1);
    assert(lv_obj_get_style_radius(panel,0)==unit*3);
    assert(lv_color_eq(lv_obj_get_style_bg_color(panel,0),c.style.panel));
    assert(lv_obj_get_style_text_font(title,0)==c.title_font);
    lv_obj_t *action=find_text(panel,chinese?"导入":"Import");assert(action);
    lv_obj_t *button=lv_obj_get_parent(action);lv_area_t area;lv_obj_get_coords(button,&area);
    assert(area.x1>bounds.x1 && area.x2<bounds.x2 && area.y1>bounds.y1 && area.y2<bounds.y2);
    assert(lv_obj_get_height(button)>=c.style.label_font->line_height+unit*4);
    assert(lv_obj_get_style_text_font(action,0)==c.style.label_font);
    assert(lv_obj_has_state(button,LV_STATE_DISABLED));
    press(p,PXA_UI_CONTROLLER_A);press(p,PXA_UI_CONTROLLER_A);
    assert(!lv_obj_has_state(button,LV_STATE_DISABLED));
    assert(lv_color_eq(lv_obj_get_style_bg_color(button,0),c.style.primary));
    pxa_lvgl_file_picker_destroy(p);
}
int main(void) {
    lv_init();lv_display_t *display=lv_display_create(320,240);assert(display);
    static uint8_t pixels[320*40*4];lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    test(0);test(1);assert(notifications==4);
    /* Native shells route Back independently of the desktop focus group. */
    pxa_lvgl_file_picker_config_t config={.browse=browse,.changed=changed,.overlay=overlay,
        .app_name="Native",.purpose="Copy ROMs",.extensions="gb,gbc"};
    pxa_lvgl_file_picker_t *picker=pxa_lvgl_file_picker_create(&config);assert(picker);
    pxa_lvgl_file_picker_back(picker);assert(pxa_lvgl_file_picker_state(picker)==2);
    assert(notifications==5);pxa_lvgl_file_picker_destroy(picker);
    test_paging();
    test_geometry(display,320,240,1,0,4);test_geometry(display,240,320,0,1,4);
    test_geometry(display,480,320,0,1,4);test_geometry(display,800,480,1,0,6);
    lv_display_delete(display);lv_deinit();
    puts("PASS: single/multi, parent navigation, restored page/focus, errors, native Back, system theme/fonts and four display geometries");
}

/* SPDX-License-Identifier: MIT */
#include "pxa/lvgl/pxa_lvgl_file_picker.h"
#include "pxa/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { size_t skip,row; int32_t scroll; } picker_location_t;
struct pxa_lvgl_file_picker {
    pxa_lvgl_file_picker_config_t config;
    lv_obj_t *mask,*panel,*list,*path_label,*folder_label,*status,*use,*up,*cancel,*current_name,*progress;
    char *volume_name;
    lv_group_t *group;
    pxa_file_picker_entry_t entries[PXA_FILE_PICKER_PAGE];
    pxa_file_choice_t *choices;
    picker_location_t *history;
    size_t count,capacity,entry_count,skip,depth,history_capacity;
    uint64_t epoch,repeat_at;
    uint32_t previous;
    uint32_t progress_at;
    size_t progress_done;
    unsigned state;
    uint8_t directory_ready;
    int32_t unit,row_height,action_height;
    char path[PXA_FS_SCOPED_MAX_PATH_BYTES+1];
    char extensions[PXA_FS_IMPORT_FILTER_MAX+1];
    char purpose[PXA_FS_IMPORT_PURPOSE_MAX+1];
};
static void render(pxa_lvgl_file_picker_t *p,const picker_location_t *restore);
static const char *tr(pxa_lvgl_file_picker_t *p,const char *zh,const char *en) {
    return p->config.chinese?zh:en;
}
static void changed(pxa_lvgl_file_picker_t *p) {
    if(p->config.changed)p->config.changed(p->config.context);
}
static void refresh_overlay(pxa_lvgl_file_picker_t *p) {
    if(p->config.overlay)p->config.overlay(p->mask,0);
}
static void notice(pxa_lvgl_file_picker_t *p,const char *text) {
    lv_label_set_text(p->status,text);refresh_overlay(p);
}
static void cancel(lv_event_t *e) {
    pxa_lvgl_file_picker_t *p=lv_event_get_user_data(e);p->state=2;changed(p);
}
static void ascend(pxa_lvgl_file_picker_t *p) {
    if(p->state)return;
    if(!p->epoch) {p->state=2;changed(p);return;}
    picker_location_t restore={0};
    if(p->depth)restore=p->history[--p->depth];
    p->skip=restore.skip;
    char *slash=strrchr(p->path,'/');
    if(!p->path[0])p->epoch=0;
    else if(slash)*slash=0;
    else p->path[0]=0;
    render(p,&restore);
}
static void up(lv_event_t *e) {ascend(lv_event_get_user_data(e));}
static void key(lv_event_t *e) {
    if(lv_event_get_key(e)==LV_KEY_ESC) {
        lv_event_stop_bubbling(e);pxa_lvgl_file_picker_back(lv_event_get_user_data(e));
    }
}
static lv_obj_t *label(lv_obj_t *parent,const lv_font_t *font) {
    lv_obj_t *l=lv_label_create(parent);
    lv_obj_set_style_text_font(l,font,0);lv_obj_set_style_text_line_space(l,0,0);
    lv_obj_set_height(l,font->line_height);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
    return l;
}
static lv_obj_t *box(lv_obj_t *parent,lv_flex_flow_t flow,int32_t gap) {
    lv_obj_t *b=lv_obj_create(parent);lv_obj_remove_style_all(b);
    lv_obj_set_width(b,LV_PCT(100));lv_obj_set_height(b,LV_SIZE_CONTENT);
    lv_obj_set_scrollable(b,false);lv_obj_set_flex_flow(b,flow);
    lv_obj_set_style_pad_row(b,gap,0);lv_obj_set_style_pad_column(b,gap,0);
    return b;
}
static lv_obj_t *button(pxa_lvgl_file_picker_t *p,lv_obj_t *parent,const char *text,lv_event_cb_t cb) {
    lv_obj_t *b=lv_button_create(parent);
    pxa_lvgl_dialog_action_style(b,&p->config.style,PXA_LVGL_ACTION_SECONDARY);
    lv_obj_set_height(b,p->action_height);
    lv_obj_t *l=label(b,p->config.style.label_font);lv_obj_set_width(l,LV_PCT(100));
    lv_label_set_text(l,text);lv_obj_center(l);
    lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,p);lv_obj_add_event_cb(b,key,LV_EVENT_KEY,p);
    return b;
}
static void selected_text(pxa_lvgl_file_picker_t *p) {
    lv_obj_t *text=lv_obj_get_child(p->use,0);
    if(p->config.mode==PXA_FILE_PICKER_DIRECTORY) {
        lv_label_set_text(text,tr(p,"选择此文件夹","Select folder"));
        if(p->directory_ready)lv_obj_remove_state(p->use,LV_STATE_DISABLED);
        else lv_obj_add_state(p->use,LV_STATE_DISABLED);
        lv_label_set_text(p->status,p->purpose);return;
    }
    if(p->count) {
        lv_label_set_text_fmt(text,tr(p,"导入 (%u)","Import (%u)"),(unsigned)p->count);
        lv_obj_remove_state(p->use,LV_STATE_DISABLED);
    } else {
        lv_label_set_text(text,tr(p,"导入","Import"));lv_obj_add_state(p->use,LV_STATE_DISABLED);
    }
    lv_label_set_text(p->status,p->purpose);
}
static int full_path(pxa_lvgl_file_picker_t *p,const char *name,char *out,size_t capacity) {
    int n=snprintf(out,capacity,"%s%s%s",p->path,p->path[0]?"/":"",name);
    return n>=0 && (size_t)n<capacity && (!p->config.max_path_bytes || n<=p->config.max_path_bytes);
}
static size_t selection_index(pxa_lvgl_file_picker_t *p,const char *path) {
    size_t i;
    for(i=0;i<p->count;++i)if(p->choices[i].epoch==p->epoch && !strcmp(p->choices[i].path,path))break;
    return i;
}
static int remember(pxa_lvgl_file_picker_t *p,size_t row) {
    if(p->depth==p->history_capacity) {
        size_t capacity=p->history_capacity?p->history_capacity*2:8;
        if(capacity>SIZE_MAX/sizeof(*p->history))return 0;
        void *memory=realloc(p->history,capacity*sizeof(*p->history));
        if(!memory) {notice(p,tr(p,"内存不足","Not enough memory"));return 0;}
        p->history=memory;p->history_capacity=capacity;
    }
    p->history[p->depth++]=(picker_location_t){p->skip,row,lv_obj_get_scroll_y(p->list)};
    return 1;
}
static void mark_row(pxa_lvgl_file_picker_t *p,lv_obj_t *row,int checked) {
    const pxa_lvgl_dialog_style_t *s=&p->config.style;
    lv_obj_t *mark=lv_obj_get_child(row,2);
    if(checked)lv_obj_add_state(row,LV_STATE_CHECKED);else lv_obj_remove_state(row,LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(mark,checked?s->primary:s->surface,0);
    lv_obj_set_style_border_color(mark,checked?s->primary:s->outline,0);
    lv_obj_set_style_text_color(mark,s->on_primary,0);
    lv_label_set_text(lv_obj_get_child(mark,0),checked?LV_SYMBOL_OK:"");
}
static void open_entry(lv_event_t *e) {
    pxa_lvgl_file_picker_t *p=lv_event_get_user_data(e);
    lv_obj_t *row=lv_event_get_target_obj(e);size_t index=(size_t)lv_obj_get_index(row);
    char path[sizeof(p->path)];
    if(p->state || index>=p->entry_count)return;
    pxa_file_picker_entry_t *entry=&p->entries[index];
    if(!p->epoch) {
        char *name=malloc(strlen(entry->name)+1);if(!name)return;
        if(!remember(p,index)) {free(name);return;}
        strcpy(name,entry->name);free(p->volume_name);p->volume_name=name;
        p->epoch=entry->epoch;p->path[0]=0;p->skip=0;render(p,NULL);return;
    }
    if(!full_path(p,entry->name,path,sizeof(path))) {notice(p,tr(p,"路径过长","Path too long"));return;}
    if(entry->kind==PXA_FS_KIND_DIRECTORY) {
        if(!remember(p,index))return;
        strcpy(p->path,path);p->skip=0;render(p,NULL);return;
    }
    if(p->config.mode==PXA_FILE_PICKER_DIRECTORY)return;
    size_t i=selection_index(p,path);
    if(i<p->count) {free(p->choices[i].path);p->choices[i]=p->choices[--p->count];}
    else {
        if(p->count==p->capacity) {
            size_t capacity=p->capacity?p->capacity*2:8;
            if(capacity<p->capacity || capacity>UINT32_MAX || capacity>SIZE_MAX/sizeof(*p->choices))return;
            void *choices=realloc(p->choices,capacity*sizeof(*p->choices));
            if(!choices) {notice(p,tr(p,"内存不足","Not enough memory"));return;}
            p->choices=choices;p->capacity=capacity;
        }
        char *copy=malloc(strlen(path)+1);
        if(!copy) {notice(p,tr(p,"内存不足","Not enough memory"));return;}
        strcpy(copy,path);
        if(!p->config.multiple && p->count) {free(p->choices[0].path);p->count=0;}
        p->choices[p->count++]=(pxa_file_choice_t){p->epoch,copy};
    }
    for(i=0;i<p->entry_count;++i)if(p->entries[i].kind==PXA_FS_KIND_REGULAR &&
        full_path(p,p->entries[i].name,path,sizeof(path)))
        mark_row(p,lv_obj_get_child(p->list,(int32_t)i),selection_index(p,path)<p->count);
    selected_text(p);refresh_overlay(p);
}
static void next_page(lv_event_t *e) {
    pxa_lvgl_file_picker_t *p=lv_event_get_user_data(e);p->skip+=p->entry_count;render(p,NULL);
}
static void previous_page(lv_event_t *e) {
    pxa_lvgl_file_picker_t *p=lv_event_get_user_data(e);
    p->skip=p->skip>PXA_FILE_PICKER_PAGE?p->skip-PXA_FILE_PICKER_PAGE:0;render(p,NULL);
}
static void refresh_scroll(lv_event_t *e) {refresh_overlay(lv_event_get_user_data(e));}
static void confirm(lv_event_t *e) {
    pxa_lvgl_file_picker_t *p=lv_event_get_user_data(e);
    if(p->config.mode==PXA_FILE_PICKER_DIRECTORY) {
        if(p->state || !p->epoch || !p->directory_ready)return;
        if(p->config.check_directory && p->config.check_directory(p->config.context,p->epoch,p->path)!=PXA_STATUS_OK) {
            p->directory_ready=0;selected_text(p);
            notice(p,tr(p,"此文件夹已不可用，请返回重选","Folder unavailable. Go up and choose again."));return;
        }
        char *path=malloc(strlen(p->path)+1);
        pxa_file_choice_t *choice=malloc(sizeof(*choice));
        if(!path || !choice) {free(path);free(choice);notice(p,tr(p,"内存不足","Not enough memory"));return;}
        strcpy(path,p->path);*choice=(pxa_file_choice_t){p->epoch,path};
        p->choices=choice;p->capacity=p->count=1;p->state=1;
        lv_obj_add_state(p->up,LV_STATE_DISABLED);lv_obj_add_state(p->use,LV_STATE_DISABLED);
        changed(p);return;
    }
    if(p->state || !p->count)return;
    p->state=1;lv_obj_clean(p->list);
    p->current_name=label(p->list,p->config.body_font);lv_obj_set_width(p->current_name,LV_PCT(100));
    lv_label_set_long_mode(p->current_name,LV_LABEL_LONG_WRAP);lv_obj_set_height(p->current_name,LV_SIZE_CONTENT);
    p->progress=lv_bar_create(p->list);lv_obj_remove_style_all(p->progress);
    lv_obj_set_size(p->progress,LV_PCT(100),p->unit*2);lv_bar_set_range(p->progress,0,1000);
    lv_obj_set_style_radius(p->progress,p->config.style.radius,0);
    lv_obj_set_style_bg_color(p->progress,p->config.style.outline,0);lv_obj_set_style_bg_opa(p->progress,LV_OPA_COVER,0);
    lv_obj_set_style_radius(p->progress,p->config.style.radius,LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(p->progress,p->config.style.primary,LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(p->progress,LV_OPA_COVER,LV_PART_INDICATOR);
    lv_obj_add_state(p->use,LV_STATE_DISABLED);lv_obj_add_state(p->up,LV_STATE_DISABLED);
    if(p->group)lv_group_focus_obj(p->cancel);
    lv_label_set_text(p->folder_label,tr(p,"正在导入","Importing files"));
    lv_label_set_text(p->path_label,tr(p,"取消会保留已完成的文件","Cancel keeps completed files"));
    pxa_lvgl_file_picker_progress(p,0,p->count,0);refresh_overlay(p);changed(p);
}
static void row(pxa_lvgl_file_picker_t *p,size_t index) {
    const pxa_lvgl_dialog_style_t *s=&p->config.style;pxa_file_picker_entry_t *entry=&p->entries[index];
    int folder=entry->kind==PXA_FS_KIND_DIRECTORY;
    lv_obj_t *r=button(p,p->list,"",open_entry);lv_obj_clean(r);
    lv_obj_set_width(r,LV_PCT(100));lv_obj_set_height(r,p->row_height);
    lv_obj_set_flex_flow(r,LV_FLEX_FLOW_ROW);lv_obj_set_flex_align(r,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(r,p->unit*2,0);lv_obj_set_style_pad_column(r,p->unit*2,0);
    lv_obj_set_style_bg_color(r,s->surface,0);lv_obj_set_style_bg_opa(r,LV_OPA_COVER,0);
    lv_obj_set_style_border_opa(r,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_opa(r,LV_OPA_COVER,LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(r,s->primary_container,LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(r,LV_OPA_COVER,LV_STATE_CHECKED);
    lv_obj_set_style_text_color(r,s->on_primary_container,LV_STATE_CHECKED);
    lv_obj_set_style_text_align(r,LV_TEXT_ALIGN_LEFT,0);
    lv_obj_t *icon=label(r,LV_FONT_DEFAULT);lv_obj_set_width(icon,(LV_FONT_DEFAULT)->line_height);
    lv_label_set_text(icon,folder?LV_SYMBOL_DIRECTORY:LV_SYMBOL_FILE);
    lv_obj_t *name=label(r,p->config.body_font);lv_obj_set_width(name,0);lv_obj_set_flex_grow(name,1);
    lv_label_set_text(name,entry->name);
    if(folder) {
        lv_obj_t *chevron=label(r,LV_FONT_DEFAULT);lv_label_set_text(chevron,LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron,s->muted,0);
    } else {
        lv_obj_t *mark=lv_obj_create(r);lv_obj_remove_style_all(mark);lv_obj_set_scrollable(mark,false);
        lv_obj_set_clickable(mark,false);
        lv_obj_set_size(mark,(LV_FONT_DEFAULT)->line_height+2,(LV_FONT_DEFAULT)->line_height+2);
        lv_obj_set_style_radius(mark,p->config.multiple?s->radius:LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_border_width(mark,1,0);lv_obj_set_style_bg_opa(mark,LV_OPA_COVER,0);
        lv_obj_t *tick=label(mark,LV_FONT_DEFAULT);lv_label_set_text(tick,"");lv_obj_center(tick);
        char path[sizeof(p->path)];
        mark_row(p,r,full_path(p,entry->name,path,sizeof(path)) && selection_index(p,path)<p->count);
    }
    if(p->group)lv_group_add_obj(p->group,r);
}
static void empty(pxa_lvgl_file_picker_t *p,const char *message) {
    lv_obj_t *text=label(p->list,p->config.body_font);lv_obj_set_width(text,LV_PCT(100));
    lv_obj_set_height(text,LV_SIZE_CONTENT);lv_label_set_long_mode(text,LV_LABEL_LONG_WRAP);
    lv_label_set_text(text,message);lv_obj_set_style_text_color(text,p->config.style.muted,0);
}
static void render(pxa_lvgl_file_picker_t *p,const picker_location_t *restore) {
    uint8_t more=0;
    if(p->group)lv_group_remove_all_objs(p->group);
    lv_obj_clean(p->list);
    for(size_t i=0;i<p->entry_count;++i) {free(p->entries[i].name);p->entries[i].name=NULL;}
    p->entry_count=0;p->directory_ready=0;selected_text(p);
    const char *leaf=strrchr(p->path,'/');
    lv_label_set_text(p->folder_label,!p->epoch?tr(p,"选择存储设备","Choose storage"):
        p->path[0]?(leaf?leaf+1:p->path):p->volume_name);
    lv_label_set_text_fmt(p->path_label,"%s%s%s",p->epoch?p->volume_name:tr(p,"选择文件所在的位置","Choose where your files are"),
        p->epoch && p->path[0]?" / ":"",p->path);
    lv_label_set_text(lv_obj_get_child(p->up,0),p->epoch && !p->path[0]?tr(p,"存储设备","Storage"):tr(p,"上一级","Up"));
    if(p->epoch)lv_obj_remove_state(p->up,LV_STATE_DISABLED);else lv_obj_add_state(p->up,LV_STATE_DISABLED);
    if(p->group)lv_group_add_obj(p->group,p->up);
    pxa_status_t status=p->config.browse(p->config.context,p->epoch,p->path,p->extensions,p->skip,
        p->entries,PXA_FILE_PICKER_PAGE,&p->entry_count,&more);
    if(status!=PXA_STATUS_OK) {
        for(size_t i=0;i<p->entry_count;++i) {free(p->entries[i].name);p->entries[i].name=NULL;}
        p->entry_count=0;empty(p,tr(p,"目录不可用\n请返回上一级重新选择","Folder unavailable.\nGo up and choose again."));
    } else {
        if(p->config.mode==PXA_FILE_PICKER_DIRECTORY && p->epoch) {
            p->directory_ready=!p->config.check_directory ||
                p->config.check_directory(p->config.context,p->epoch,p->path)==PXA_STATUS_OK;
            selected_text(p);
            if(!p->directory_ready)lv_label_set_text(p->status,tr(p,"此位置不可授权，请选择其他文件夹","This location cannot be shared. Choose another folder."));
        }
        if(!p->entry_count) {
            const char *message=!p->epoch?tr(p,"没有可用的存储设备","No storage available."):
                p->config.mode!=PXA_FILE_PICKER_DIRECTORY?
                tr(p,"没有匹配的文件\n可以返回上一级继续查找","No matching files.\nGo up to another folder."):
                p->directory_ready?tr(p,"没有子文件夹\n可选择当前文件夹","No subfolders.\nYou can select this folder."):
                tr(p,"没有子文件夹\n请返回上一级重新选择","No subfolders.\nGo up and choose again.");
            empty(p,message);
        }
        for(size_t i=0;i<p->entry_count;++i)row(p,i);
        if(p->skip) {
            lv_obj_t *b=button(p,p->list,tr(p,"上一页","Previous page"),previous_page);
            lv_obj_set_width(b,LV_PCT(100));if(p->group)lv_group_add_obj(p->group,b);
        }
        if(more) {
            lv_obj_t *b=button(p,p->list,tr(p,"下一页","Next page"),next_page);
            lv_obj_set_width(b,LV_PCT(100));if(p->group)lv_group_add_obj(p->group,b);
        }
    }
    if(p->group) {lv_group_add_obj(p->group,p->cancel);lv_group_add_obj(p->group,p->use);}
    /* Also preselect the restored row for the native board's existing group.
     * It will recollect new rows without selecting or clicking a held key. */
    size_t index=restore && restore->row<p->entry_count?restore->row:0;
    lv_obj_t *focus=p->entry_count?lv_obj_get_child(p->list,(int32_t)index):p->epoch?p->up:p->cancel;
    lv_group_t *group=p->group?p->group:lv_obj_get_group(p->up);
    if(group) {
        if(lv_obj_get_group(focus)!=group)lv_group_add_obj(group,focus);
        lv_group_focus_obj(focus);
    }
    lv_obj_update_layout(p->panel);
    lv_obj_scroll_to_y(p->list,restore?restore->scroll:0,LV_ANIM_OFF);
    if(restore && p->entry_count)lv_obj_scroll_to_view(focus,LV_ANIM_OFF);
    refresh_overlay(p);
}
pxa_lvgl_file_picker_t *pxa_lvgl_file_picker_create(const pxa_lvgl_file_picker_config_t *c) {
    if(!c || !c->browse || !c->extensions || strlen(c->extensions)>PXA_FS_IMPORT_FILTER_MAX ||
        (c->purpose && strlen(c->purpose)>PXA_FS_IMPORT_PURPOSE_MAX) ||
        (c->mode!=PXA_FILE_PICKER_IMPORT && c->mode!=PXA_FILE_PICKER_DIRECTORY) ||
        (c->mode==PXA_FILE_PICKER_DIRECTORY && c->multiple) ||
        c->max_path_bytes>PXA_FS_SCOPED_MAX_PATH_BYTES)return NULL;
    pxa_lvgl_file_picker_t *p=calloc(1,sizeof(*p));if(!p)return NULL;
    p->config=*c;strcpy(p->extensions,c->extensions);strcpy(p->purpose,c->purpose?c->purpose:"");
    p->unit=c->style.spacing>0?c->style.spacing:4;
    if(!p->config.body_font)p->config.body_font=LV_FONT_DEFAULT;
    if(!p->config.title_font)p->config.title_font=p->config.body_font;
    if(!p->config.caption_font)p->config.caption_font=p->config.body_font;
    if(!p->config.style.label_font)p->config.style.label_font=p->config.body_font;
    p->config.style.spacing=p->unit;
    const int32_t w=lv_display_get_horizontal_resolution(NULL),h=lv_display_get_vertical_resolution(NULL);
    const int compact=h<80*p->unit;
    const int32_t margin=p->unit*(compact?2:3),padding=p->unit*(compact?2:3),gap=p->unit*(compact?1:2);
    p->action_height=p->config.style.label_font->line_height+p->unit*(compact?4:5);
    p->row_height=p->config.body_font->line_height+p->unit*(compact?3:4);
    if(!c->overlay)p->group=lv_group_create();
    p->mask=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(p->mask);
    lv_obj_set_size(p->mask,LV_PCT(100),LV_PCT(100));lv_obj_set_scrollable(p->mask,false);
    lv_obj_set_style_bg_color(p->mask,lv_color_black(),0);lv_obj_set_style_bg_opa(p->mask,LV_OPA_60,0);
    lv_obj_set_click_focusable(p->mask,false);lv_obj_set_clickable(p->mask,true);
    p->panel=box(p->mask,LV_FLEX_FLOW_COLUMN,gap);pxa_lvgl_dialog_panel_style(p->panel,&p->config.style);
    const int32_t width=w-2*margin,height=h-2*margin;
    lv_obj_set_size(p->panel,width>120*p->unit?120*p->unit:width,height>140*p->unit?140*p->unit:height);
    lv_obj_center(p->panel);lv_obj_set_style_pad_all(p->panel,padding,0);lv_obj_set_style_pad_row(p->panel,gap,0);
    lv_obj_set_style_text_font(p->panel,p->config.body_font,0);
    lv_obj_t *header=box(p->panel,LV_FLEX_FLOW_ROW,p->unit*2);
    lv_obj_set_flex_align(header,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title=label(header,p->config.title_font);lv_label_set_text(title,
        c->mode==PXA_FILE_PICKER_DIRECTORY?tr(p,"选择文件夹","Choose folder"):tr(p,"导入文件","Import files"));
    lv_obj_t *app=label(header,p->config.caption_font);lv_obj_set_width(app,0);lv_obj_set_flex_grow(app,1);
    lv_obj_set_style_text_align(app,LV_TEXT_ALIGN_RIGHT,0);lv_obj_set_style_text_color(app,c->style.muted,0);
    lv_label_set_text(app,c->app_name?c->app_name:"");
    p->status=label(p->panel,p->config.caption_font);lv_obj_set_width(p->status,LV_PCT(100));
    lv_obj_set_style_text_color(p->status,c->style.muted,0);
    lv_obj_t *navigation=box(p->panel,LV_FLEX_FLOW_ROW,p->unit*2);
    lv_obj_set_flex_align(navigation,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    p->up=button(p,navigation,tr(p,"上一级","Up"),up);
    lv_obj_set_width(p->up,p->config.style.label_font->line_height*4+p->unit*4);
    lv_obj_t *location=box(navigation,LV_FLEX_FLOW_COLUMN,0);lv_obj_set_width(location,0);lv_obj_set_flex_grow(location,1);
    p->folder_label=label(location,p->config.body_font);lv_obj_set_width(p->folder_label,LV_PCT(100));
    p->path_label=label(location,p->config.caption_font);lv_obj_set_width(p->path_label,LV_PCT(100));
    lv_obj_set_style_text_color(p->path_label,c->style.muted,0);
    p->list=box(p->panel,LV_FLEX_FLOW_COLUMN,p->unit);lv_obj_set_height(p->list,0);lv_obj_set_flex_grow(p->list,1);
    lv_obj_set_scrollable(p->list,true);lv_obj_set_scroll_dir(p->list,LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p->list,LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(p->list,p->unit,0);lv_obj_set_style_radius(p->list,c->style.radius*2,0);
    lv_obj_set_style_border_width(p->list,1,0);lv_obj_set_style_border_color(p->list,c->style.outline,0);
    lv_obj_set_style_bg_color(p->list,c->style.container,0);lv_obj_set_style_bg_opa(p->list,LV_OPA_COVER,0);
    lv_obj_set_style_width(p->list,p->unit>2?p->unit/2:1,LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(p->list,c->style.outline,LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(p->list,LV_OPA_60,LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(p->list,LV_RADIUS_CIRCLE,LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(p->list,refresh_scroll,LV_EVENT_SCROLL_END,p);
    lv_obj_t *footer=box(p->panel,LV_FLEX_FLOW_ROW,p->unit*2);
    p->cancel=button(p,footer,tr(p,"取消","Cancel"),cancel);
    p->use=button(p,footer,tr(p,"导入","Import"),confirm);
    pxa_lvgl_dialog_action_style(p->use,&p->config.style,PXA_LVGL_ACTION_PRIMARY);
    lv_obj_set_width(p->cancel,0);lv_obj_set_flex_grow(p->cancel,1);
    lv_obj_set_width(p->use,0);lv_obj_set_flex_grow(p->use,2);
    if(c->overlay)c->overlay(p->mask,1);
    render(p,NULL);return p;
}
void pxa_lvgl_file_picker_destroy(pxa_lvgl_file_picker_t *p) {
    if(!p)return;
    if(p->config.overlay)p->config.overlay(p->mask,-1);
    lv_obj_delete(p->mask);if(p->group)lv_group_delete(p->group);
    for(size_t i=0;i<p->entry_count;++i)free(p->entries[i].name);
    for(size_t i=0;i<p->count;++i)free(p->choices[i].path);
    free(p->history);free(p->volume_name);free(p->choices);free(p);
}
unsigned pxa_lvgl_file_picker_state(const pxa_lvgl_file_picker_t *p) {return p?p->state:2;}
const pxa_file_choice_t *pxa_lvgl_file_picker_selection(const pxa_lvgl_file_picker_t *p,size_t *count) {
    *count=p->count;return p->choices;
}
void pxa_lvgl_file_picker_progress(pxa_lvgl_file_picker_t *p,size_t done,size_t total,uint64_t bytes) {
    if(!p || p->state!=1 || p->config.mode!=PXA_FILE_PICKER_IMPORT)return;
    if(done<p->count) {const char *name=strrchr(p->choices[done].path,'/');
        lv_label_set_text(p->current_name,name?name+1:p->choices[done].path);}
    lv_label_set_text_fmt(p->status,tr(p,"已完成 %u/%u · %llu KiB","Copied %u/%u · %llu KiB"),
        (unsigned)done,(unsigned)total,(unsigned long long)(bytes/1024));
    lv_bar_set_value(p->progress,total?(int32_t)((uint64_t)done*1000/total):0,LV_ANIM_OFF);
    /* Native Surface compositing needs a fresh overlay, but not a full-panel
     * snapshot on every 16 KiB copy quantum. */
    uint32_t now=lv_tick_get();
    if(!p->progress_at || done!=p->progress_done || (uint32_t)(now-p->progress_at)>=100) {
        p->progress_at=now?now:1;p->progress_done=done;refresh_overlay(p);
    }
}
void pxa_lvgl_file_picker_back(pxa_lvgl_file_picker_t *p) {
    if(!p)return;
    if(p->state) {p->state=2;changed(p);}else ascend(p);
}
void pxa_lvgl_file_picker_controller(pxa_lvgl_file_picker_t *p,uint32_t buttons,uint64_t now) {
    if(!p || !p->group)return;
    uint32_t pressed=buttons&~p->previous;p->previous=buttons;
    const uint32_t dirs=PXA_UI_CONTROLLER_UP|PXA_UI_CONTROLLER_DOWN|PXA_UI_CONTROLLER_LEFT|PXA_UI_CONTROLLER_RIGHT;
    if(pressed&dirs)p->repeat_at=now+350000;
    else if((buttons&dirs) && now>=p->repeat_at) {pressed|=buttons&dirs;p->repeat_at=now+90000;}
    if(pressed&PXA_UI_CONTROLLER_B) {pxa_lvgl_file_picker_back(p);return;}
    if(pressed&(PXA_UI_CONTROLLER_UP|PXA_UI_CONTROLLER_LEFT))lv_group_focus_prev(p->group);
    else if(pressed&(PXA_UI_CONTROLLER_DOWN|PXA_UI_CONTROLLER_RIGHT))lv_group_focus_next(p->group);
    if(pressed&PXA_UI_CONTROLLER_A) {
        lv_obj_t *focused=lv_group_get_focused(p->group);
        if(focused && !lv_obj_has_state(focused,LV_STATE_DISABLED))lv_obj_send_event(focused,LV_EVENT_CLICKED,NULL);
    }
    if(pressed&PXA_UI_CONTROLLER_START)lv_obj_send_event(p->use,LV_EVENT_CLICKED,NULL);
}

#include "pxsys/reference_ime.h"
#include "pxa/lvgl/pxa_lvgl_ime.h"
#include <stdlib.h>

struct pxsys_reference_ime {
    pxa_lvgl_ime_t *keyboard;
    void (*closed)(void *);
    void *context;
    bool active;
};
static void focused(void *context,uint64_t generation,bool active,bool provider) {
    pxsys_reference_ime_t *ime=context;
    (void)generation; (void)provider;
    bool closed=ime->active && !active; ime->active=active;
    if (closed && ime->closed) ime->closed(ime->context);
}
static pxa_ui_environment_t environment(const pxsys_display_profile_t *display) {
    pxa_ui_environment_t result={0};
    if (!display) return result;
    result.width=display->width; result.height=display->height;
    result.density_q16=(uint32_t)((uint64_t)(display->density_dpi?display->density_dpi:160)*65536/160);
    result.display_shape=display->shape;
    result.corner_radii[0]=display->corner_radii.top_left;
    result.corner_radii[1]=display->corner_radii.top_right;
    result.corner_radii[2]=display->corner_radii.bottom_right;
    result.corner_radii[3]=display->corner_radii.bottom_left;
    pxsys_insets_t insets=display->safe_insets;
    (void)pxsys_display_effective_insets(display,&insets);
    result.safe_insets[0]=insets.top; result.safe_insets[1]=insets.right;
    result.safe_insets[2]=insets.bottom; result.safe_insets[3]=insets.left;
    return result;
}
static pxa_lvgl_ui_theme_t theme(const pxsys_reference_ime_config_t *config) {
    pxa_lvgl_ui_theme_t result={0};
    result.caption_font=config->caption_font; result.label_font=result.body_font=config->body_font;
    result.icon_font=config->icon_font;
    if (config->theme) {
        result.rgba[PXA_UI_THEME_TEXT]=(config->theme->colors[PXSYS_COLOR_ON_SURFACE]<<8)|255;
        result.rgba[PXA_UI_THEME_SURFACE_CONTAINER]=(config->theme->colors[PXSYS_COLOR_SURFACE_CONTAINER]<<8)|255;
        result.rgba[PXA_UI_THEME_SURFACE_CONTAINER_HIGHEST]=(config->theme->colors[PXSYS_COLOR_SURFACE_CONTAINER_HIGHEST]<<8)|255;
        result.rgba[PXA_UI_THEME_OUTLINE_VARIANT]=(config->theme->colors[PXSYS_COLOR_OUTLINE_VARIANT]<<8)|255;
        result.rgba[PXA_UI_THEME_PRIMARY_CONTAINER]=(config->theme->colors[PXSYS_COLOR_PRIMARY_CONTAINER]<<8)|255;
        result.rgba[PXA_UI_THEME_ON_PRIMARY_CONTAINER]=(config->theme->colors[PXSYS_COLOR_ON_PRIMARY_CONTAINER]<<8)|255;
    }
    return result;
}
pxsys_reference_ime_t *pxsys_reference_ime_create(lv_obj_t *parent,lv_obj_t *editor,
    const pxsys_reference_ime_config_t *config) {
    if (!parent || !editor || !config || !config->display) return NULL;
    pxsys_reference_ime_t *ime=calloc(1,sizeof(*ime));
    if (!ime) return NULL;
    ime->closed=config->closed; ime->context=config->context;
    pxa_lvgl_ime_config_t options={.struct_size=sizeof(options),.parent=parent,
        .environment=environment(config->display),.theme=theme(config),
        .preferences={config->layout,config->language},.focus=focused,.context=ime};
    ime->keyboard=pxa_lvgl_ime_create(&options);
    if (!ime->keyboard || pxa_lvgl_ime_focus(ime->keyboard,editor)!=PXA_STATUS_OK) {
        pxa_lvgl_ime_destroy(ime->keyboard); free(ime); return NULL;
    }
    return ime;
}
void pxsys_reference_ime_destroy(pxsys_reference_ime_t *ime) {
    if (!ime) return;
    ime->closed=NULL;
    pxa_lvgl_ime_destroy(ime->keyboard); free(ime);
}
void pxsys_reference_ime_update(pxsys_reference_ime_t *ime,const pxsys_reference_ime_config_t *config) {
    if (!ime || !config || !config->display) return;
    pxa_lvgl_ui_theme_t colors=theme(config);
    pxa_ui_environment_t display=environment(config->display);
    pxa_lvgl_ime_set_preferences(ime->keyboard,(pxa_ime_preferences_t){config->layout,config->language});
    pxa_lvgl_ime_set_theme(ime->keyboard,&colors);
    pxa_lvgl_ime_set_environment(ime->keyboard,&display);
}
bool pxsys_reference_ime_is_visible(const pxsys_reference_ime_t *ime) {
    return ime && pxa_lvgl_ime_active(ime->keyboard);
}
lv_obj_t *pxsys_reference_ime_get_root(const pxsys_reference_ime_t *ime) {
    return ime?pxa_lvgl_ime_root(ime->keyboard):NULL;
}
lv_coord_t pxsys_reference_ime_keyboard_top(const pxsys_reference_ime_t *ime) {
    lv_obj_t *root=pxsys_reference_ime_get_root(ime);
    if (!root) return -1;
    lv_obj_update_layout(root); lv_area_t area; lv_obj_get_coords(root,&area); return area.y1;
}

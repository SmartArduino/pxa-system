#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pxsys/reference_ime.h"
#include "pxsys/reference_layout.h"
#include "ime_alloc_trace.h"
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) { (void)a;(void)p;lv_display_flush_ready(d); }
static uint32_t fixed_tick(void) { return 0; }
int main(int argc,char **argv) {
 assert(argc==2); unsigned shape=(unsigned)atoi(argv[1]); unsigned w=shape?454:296,h=shape?454:240;
 lv_init();lv_tick_set_cb(fixed_tick); lv_display_t *d=lv_display_create(w,h); static uint8_t pixels[454*10*4];
 lv_display_set_buffers(d,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 pxsys_display_profile_t profile;pxsys_display_profile_init(&profile,w,h);profile.shape=shape?PXSYS_DISPLAY_SHAPE_CIRCLE:PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE;
 profile.corner_radii=(pxsys_corner_radii_t){40,40,40,40};profile.safe_insets=(pxsys_insets_t){4,4,4,4};
 pxsys_theme_snapshot_t theme={0};theme.colors[PXSYS_COLOR_ON_SURFACE]=0xffffff;theme.colors[PXSYS_COLOR_SURFACE_CONTAINER]=0x202830;
 theme.colors[PXSYS_COLOR_SURFACE_CONTAINER_HIGHEST]=0x344454;theme.colors[PXSYS_COLOR_PRIMARY_CONTAINER]=0x1565c0;theme.colors[PXSYS_COLOR_ON_PRIMARY_CONTAINER]=0xffffff;
 lv_obj_t *input=lv_textarea_create(lv_screen_active());lv_obj_set_size(input,w-24,36);lv_textarea_set_text(input,"");lv_textarea_set_one_line(input,true);
 size_t initial=0,stable=0,peak=0,end=0;
 for (unsigned pass=0;pass<9;++pass) {
  size_t begin=live_bytes;if(pass)peak_bytes=begin;
#ifdef IME_BEFORE
  pxsys_reference_ime_t *ime=pxsys_reference_ime_create(lv_screen_active(),input);assert(ime);
  pxsys_reference_ime_set_font(ime,LV_FONT_DEFAULT);pxsys_reference_ime_set_icon_font(ime,LV_FONT_DEFAULT);pxsys_reference_ime_set_theme(ime,&theme);
  pxsys_rect_t rect;assert(!pxsys_reference_input_method_rect(&profile,&rect));
  pxsys_reference_ime_set_layout(ime,rect.width,rect.height,LV_ALIGN_TOP_LEFT,rect.x,rect.y);
#else
  pxsys_reference_ime_config_t config={.display=&profile,.theme=&theme,.body_font=LV_FONT_DEFAULT,.caption_font=LV_FONT_DEFAULT,.icon_font=LV_FONT_DEFAULT,.layout=2};
  pxsys_reference_ime_t *ime=pxsys_reference_ime_create(lv_screen_active(),input,&config);assert(ime);
#endif
  lv_refr_now(d);size_t loaded=live_bytes;pxsys_reference_ime_destroy(ime);lv_refr_now(d);
  if(!pass)initial=peak_bytes;else {stable=loaded-begin;if(peak_bytes-begin>peak)peak=peak_bytes-begin;end=live_bytes-begin;}
 }
 printf("{\"shape\":%u,\"loaded_delta\":%zu,\"initialization_absolute_peak\":%zu,\"reopen_peak_delta\":%zu,\"closed_delta\":%zu}\n",shape,stable,initial,peak,end);
 lv_obj_delete(input);lv_display_delete(d);lv_deinit();
}

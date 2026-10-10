#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include PXA_PRODUCT_RUNNER
#include <assert.h>
typedef struct { product_host_t *host; const char *artifacts; unsigned stage,profile; uint64_t started; } safety_t;
static lv_obj_t *find_label(lv_obj_t *object,const char *text){
 if(lv_obj_check_type(object,&lv_label_class)&&!strcmp(lv_label_get_text(object),text))return object;
 for(unsigned i=0;i<lv_obj_get_child_count(object);++i){lv_obj_t *found=find_label(lv_obj_get_child(object,i),text);if(found)return found;}return NULL;
}
static bool has_error(lv_obj_t *object){
 if(lv_obj_check_type(object,&lv_label_class)&&!strncmp(lv_label_get_text(object),"Error ",6)){fprintf(stderr,"Guest %s\n",lv_label_get_text(object));return true;}
 for(unsigned i=0;i<lv_obj_get_child_count(object);++i)if(has_error(lv_obj_get_child(object,i)))return true;return false;
}
static void bind_probe(void *context,void (*focus)(void *,bool),void (*exit_app)(void *),void *runner){
 (void)focus;(void)exit_app;((safety_t*)context)->host=runner;
}
static void click(safety_t *p,const char *text){
 lv_obj_t *label=find_label(lv_screen_active(),text);assert(label);
 lv_obj_send_event(lv_obj_get_parent(label),LV_EVENT_CLICKED,NULL);dispatch_component_events(p->host);
}
static void pump_probe(void *context){
 safety_t *p=context;product_host_t *h=p->host;if(!h)return;
 assert(now_us(NULL)-p->started<20000000);lv_obj_t *root=lv_screen_active();assert(!has_error(root));
 if(p->stage==0&&find_label(root,"Core checks OK")){click(p,"Test audio");p->stage=1;}
 else if(p->stage==1&&find_label(root,"Audio checks OK")){
  product_focus_changed(h,false);dispatch_lifecycle(h);product_focus_changed(h,true);dispatch_lifecycle(h);
  click(p,"Repeat checks");p->stage=2;
 }else if(p->stage==2&&find_label(root,"Core checks OK")){
  click(p,"Test audio");p->stage=3;
 }else if(p->stage==3&&find_label(root,"Audio checks OK")){
  pxa_wamr_memory_snapshot_t m={0};assert(!pxa_wamr_engine_memory_snapshot(h->engine,&m));
  printf("SAFETY_MEMORY profile=%u wamr=%u peak=%u linear=%llu linear_peak=%llu\n",p->profile,m.current_bytes,m.peak_bytes,(unsigned long long)m.linear_current_bytes,(unsigned long long)m.linear_peak_bytes);
  lv_draw_buf_t *snapshot=lv_snapshot_take(h->content_parent,LV_COLOR_FORMAT_ARGB8888);assert(snapshot);
  char path[2048];snprintf(path,sizeof(path),"%s/profile-%u.bgra",p->artifacts,p->profile);
  FILE *f=fopen(path,"wb");assert(f);size_t size=(size_t)snapshot->header.stride*snapshot->header.h;
  assert(fwrite(snapshot->data,1,size,f)==size&&!fclose(f));
  snprintf(path,sizeof(path),"%s/profile-%u.json",p->artifacts,p->profile);f=fopen(path,"w");assert(f);
  fprintf(f,"{\"width\":%u,\"height\":%u,\"stride\":%u}\n",snapshot->header.w,snapshot->header.h,snapshot->header.stride);
  assert(!fclose(f));lv_draw_buf_destroy(snapshot);p->stage=4;h->exit_requested=1;
 }
}
int main(int argc,char **argv){
 assert(argc==5);setenv("SDL_VIDEODRIVER","dummy",1);setenv("SDL_AUDIODRIVER","dummy",1);
 for(unsigned i=0;i<3;++i){
  options_t o={0};o.package_path=argv[1];o.publisher_key=argv[2];o.state_root=argv[3];o.locale="en-US";
  o.width=i==1?480:i==2?454:240;o.height=i==1?640:i==2?454:320;o.density_dpi=i==0?160:305;
  if(i==2){o.display_shape=PXA_UI_DISPLAY_SHAPE_CIRCLE;for(unsigned j=0;j<4;++j)o.safe_insets[j]=67;}
  safety_t p={0};p.profile=i;p.artifacts=argv[4];p.started=now_us(NULL);
  assert(!run_product_simulator(&o,NULL,NULL,pump_probe,&p,NULL,&p,bind_probe,NULL,NULL,NULL));assert(p.stage==4&&!p.host);
 }
 puts("Service safety: real AOT, FS read/write/seek, moved Surface and Audio requests, draw duplicate, focus, restart, three DPI/shape profiles OK");
}

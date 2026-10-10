#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include PXA_PRODUCT_RUNNER
#include <assert.h>
typedef struct {product_host_t *host; unsigned count; bool recorded; uint64_t started;} probe_t;
static lv_obj_t *label(lv_obj_t *object,const char *text){
 if(lv_obj_check_type(object,&lv_label_class)&&!strcmp(lv_label_get_text(object),text))return object;
 for(unsigned i=0;i<lv_obj_get_child_count(object);++i){lv_obj_t *p=label(lv_obj_get_child(object,i),text);if(p)return p;}return NULL;
}
static void bind_probe(void *context,void (*focus)(void *,bool),void (*exit_app)(void *),void *runner){
 (void)focus;(void)exit_app;((probe_t*)context)->host=runner;
}
static void pump_probe(void *context){
 probe_t *p=context;product_host_t *h=p->host;if(!h)return;
 assert(now_us(NULL)-p->started<20000000);
 lv_obj_t *root=lv_screen_active(),*button_label=label(root,"Add one");if(!button_label)return;
 char value[16];snprintf(value,sizeof(value),"%u",p->count);assert(label(root,value));
 if(p->count<100){lv_obj_send_event(lv_obj_get_parent(button_label),LV_EVENT_CLICKED,NULL);dispatch_component_events(h);++p->count;return;}
 pxa_wamr_memory_snapshot_t m={0};assert(!pxa_wamr_engine_memory_snapshot(h->engine,&m));
 printf("COUNTER_MEMORY {\"updates\":%u,\"wamr_current_bytes\":%u,\"wamr_peak_bytes\":%u,\"linear_current_bytes\":%llu,\"linear_peak_bytes\":%llu,\"artifact_buffer_bytes\":%llu,\"event_buffer_bytes\":%llu}\n",p->count,m.current_bytes,m.peak_bytes,(unsigned long long)m.linear_current_bytes,(unsigned long long)m.linear_peak_bytes,(unsigned long long)m.artifact_buffer_bytes,(unsigned long long)m.event_buffer_bytes);
 p->recorded=true;h->exit_requested=1;
}
int main(int argc,char **argv){
 assert(argc==4);setenv("SDL_VIDEODRIVER","dummy",1);setenv("SDL_AUDIODRIVER","dummy",1);
 options_t o={0};o.package_path=argv[1];o.publisher_key=argv[2];o.state_root=argv[3];o.locale="en-US";o.width=240;o.height=320;o.density_dpi=160;
 probe_t p={0};p.started=now_us(NULL);
 assert(!run_product_simulator(&o,NULL,NULL,pump_probe,&p,NULL,&p,bind_probe,NULL,NULL,NULL));assert(p.recorded&&!p.host);
}

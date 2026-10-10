/* Run the independent C++ i18n sample as signed AOT, inspect actual LVGL text,
 * and deliver locale notifications through the same bridge as Settings. */
#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include <assert.h>

typedef struct {
    product_host_t *host;
    unsigned stage;
    uint64_t started;
} i18n_test_t;
static int has_label(lv_obj_t *object, const char *text) {
    if (lv_obj_check_type(object, &lv_label_class) && !strcmp(lv_label_get_text(object), text)) return 1;
    for (uint32_t i=0; i<lv_obj_get_child_count(object); ++i)
        if (has_label(lv_obj_get_child(object,i),text)) return 1;
    return 0;
}
static void bind_test(void *context, void (*focus)(void *, bool),
    void (*exit_app)(void *), void *runner) {
    (void)focus; (void)exit_app; ((i18n_test_t *)context)->host=runner;
}
static void change_locale(i18n_test_t *test, const char *tag) {
    pxsys_locale_snapshot_t locale={.struct_size=sizeof(locale)};
    assert(pxsys_locale_snapshot_init(&locale,pxsys_string_from_cstr(tag))==PXSYS_STATUS_OK);
    assert(pxsys_product_simulator_update_locale(test->host,&locale));
    memset(&locale,0,sizeof(locale)); // Queue owns its payload.
}
static void pump_test(void *context) {
    i18n_test_t *test=context; product_host_t *host=test->host;
    if (!host) return;
    assert(now_us(NULL)-test->started < UINT64_C(15000000));
    lv_obj_t *root=lv_screen_active();
    if (!test->stage) {
        if (!has_label(root,"Internationalization") || !has_label(root,"1 item")) return;
        ui_event(1,5,PXA_UI_EVENT_ACTION,0,NULL,0,host);
        test->stage=1;
    } else if (test->stage==1) {
        if (!has_label(root,"2 items")) return;
        change_locale(test,"zh-CN"); test->stage=2;
    } else if (test->stage==2) {
        if (!has_label(root,"国际化示例") || !has_label(root,"2 件物品")) return;
        change_locale(test,"zh-TW"); test->stage=3;
    } else if (test->stage==3) {
        if (!has_label(root,"國際化範例")) return;
        change_locale(test,"ru-RU"); test->stage=4;
    } else if (test->stage==4) {
        if (!has_label(root,"Локализация") || !has_label(root,"2 предмета")) return;
        pxsys_locale_snapshot_t invalid={.struct_size=sizeof(invalid)};
        assert(!pxsys_product_simulator_update_locale(host,&invalid));
        product_focus_changed(host,false); dispatch_lifecycle(host);
        product_focus_changed(host,true); dispatch_lifecycle(host);
        assert(has_label(root,"2 предмета"));
        test->stage=5; host->exit_requested=1;
    }
}
int main(int argc, char **argv) {
    assert(argc==3);
    setenv("SDL_VIDEODRIVER","dummy",1); setenv("SDL_AUDIODRIVER","dummy",1);
    for (unsigned repeat=0; repeat<3; ++repeat) {
        options_t options={0}; options.package_path=argv[1]; options.publisher_key=argv[2];
        options.width=240; options.height=320; options.locale="en-US";
        i18n_test_t test={0}; test.started=now_us(NULL);
        assert(!run_product_simulator(&options,NULL,NULL,pump_test,&test,NULL,&test,bind_test,NULL,NULL,NULL));
        assert(!test.host && test.stage==5);
    }
    puts("i18n AOT: rendered en/zh-CN/zh-TW/ru, plural click, locale ownership, focus and 3 restarts passed");
    return 0;
}

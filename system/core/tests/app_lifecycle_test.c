#include <assert.h>
#include <stdio.h>

#include "pxsys/app_lifecycle.h"

static void test_normal_lifecycle(void) {
    pxsys_app_lifecycle_t lifecycle;
    pxsys_app_lifecycle_init(&lifecycle);
    assert(lifecycle.state == PXSYS_APP_ALLOCATED);
    assert(pxsys_app_lifecycle_begin_create(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_create(&lifecycle, PXSYS_STATUS_OK) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_begin_start(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_start(&lifecycle, PXSYS_STATUS_OK) == PXSYS_STATUS_OK);
    assert(lifecycle.state == PXSYS_APP_BACKGROUND);
    assert(pxsys_app_lifecycle_foreground(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_background(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_request_stop(&lifecycle, PXSYS_STOP_NORMAL) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_begin_stop(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_stop(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_destroy(&lifecycle) == PXSYS_STATUS_OK);
    assert(lifecycle.state == PXSYS_APP_DESTROYED);
    assert(lifecycle.generation == 11);
}

static void test_failed_start_requires_stop_and_destroy(void) {
    pxsys_app_lifecycle_t lifecycle;
    pxsys_app_lifecycle_init(&lifecycle);
    assert(pxsys_app_lifecycle_begin_create(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_create(&lifecycle, PXSYS_STATUS_OK) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_begin_start(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_start(&lifecycle, PXSYS_STATUS_INTERNAL) == PXSYS_STATUS_OK);
    assert(lifecycle.state == PXSYS_APP_STOP_REQUESTED);
    assert(lifecycle.stop_reason == PXSYS_STOP_FAULT);
    assert(pxsys_app_lifecycle_begin_stop(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_stop(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_destroy(&lifecycle) == PXSYS_STATUS_OK);
}

static void test_failed_create_and_invalid_transitions(void) {
    pxsys_app_lifecycle_t lifecycle;
    pxsys_app_lifecycle_init(&lifecycle);
    assert(pxsys_app_lifecycle_begin_start(&lifecycle) == PXSYS_STATUS_BAD_STATE);
    assert(pxsys_app_lifecycle_finish_create(&lifecycle, (pxsys_status_t)100) ==
           PXSYS_STATUS_INVALID_ARGUMENT);
    assert(pxsys_app_lifecycle_begin_create(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_create(&lifecycle, PXSYS_STATUS_NO_MEMORY) ==
           PXSYS_STATUS_OK);
    assert(lifecycle.state == PXSYS_APP_DESTROY_PENDING);
    assert(pxsys_app_lifecycle_finish_destroy(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_foreground(&lifecycle) == PXSYS_STATUS_BAD_STATE);
}

static void test_unstarted_instance_skips_stop_callback(void) {
    pxsys_app_lifecycle_t lifecycle;
    pxsys_app_lifecycle_init(&lifecycle);
    assert(pxsys_app_lifecycle_begin_create(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_create(&lifecycle, PXSYS_STATUS_OK) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_request_stop(&lifecycle, PXSYS_STOP_SHUTDOWN) == PXSYS_STATUS_OK);
    assert(lifecycle.state == PXSYS_APP_DESTROY_PENDING);
    assert(lifecycle.stop_reason == PXSYS_STOP_SHUTDOWN);
}

static void test_pending_start_can_be_cancelled(void) {
    pxsys_app_lifecycle_t lifecycle;
    pxsys_app_lifecycle_init(&lifecycle);
    assert(pxsys_app_lifecycle_begin_create(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_create(&lifecycle, PXSYS_STATUS_OK) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_begin_start(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_request_stop(&lifecycle, PXSYS_STOP_SHUTDOWN) == PXSYS_STATUS_OK);
    assert(lifecycle.state == PXSYS_APP_STOP_REQUESTED);
    assert(pxsys_app_lifecycle_begin_stop(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_stop(&lifecycle) == PXSYS_STATUS_OK);
    assert(pxsys_app_lifecycle_finish_destroy(&lifecycle) == PXSYS_STATUS_OK);
}

int main(void) {
    test_normal_lifecycle();
    test_failed_start_requires_stop_and_destroy();
    test_failed_create_and_invalid_transitions();
    test_unstarted_instance_skips_stop_callback();
    test_pending_start_can_be_cancelled();
    puts("pxa_system_core app lifecycle tests passed");
    return 0;
}

#include <assert.h>
#include <stdlib.h>

#include "pxsys/window.h"

typedef struct {
    size_t calls;
    pxsys_window_snapshot_t last;
} observer_t;

static void* allocate(void* context, size_t size) {
    (void)context;
    return malloc(size);
}

static void release(void* context, void* memory) {
    (void)context;
    free(memory);
}

static void changed(void* context, const pxsys_window_snapshot_t* snapshot) {
    observer_t* observer = (observer_t*)context;
    observer->calls++;
    observer->last = *snapshot;
}

int main(void) {
    pxsys_window_service_config_t config;
    pxsys_window_snapshot_t initial;
    pxsys_window_snapshot_t next;
    pxsys_window_service_t* service = NULL;
    observer_t observer = {0};

    pxsys_window_service_config_init(&config);
    config.allocator.allocate = allocate;
    config.allocator.release = release;
    pxsys_window_snapshot_init(&initial);
    assert(pxsys_window_service_create(&config, &initial, &service) ==
           PXSYS_STATUS_OK);
    assert(pxsys_window_service_subscribe(service, &observer, changed) ==
           PXSYS_STATUS_OK);
    assert(observer.calls == 1 && observer.last.edge_to_edge);

    next = initial;
    next.status_bar_mode = PXSYS_WINDOW_BAR_TRANSIENT;
    next.navigation_bar_mode = PXSYS_WINDOW_BAR_TRANSIENT;
    assert(pxsys_window_service_update(service, &next) == PXSYS_STATUS_OK);
    assert(observer.calls == 2 && observer.last.generation == 2);
    assert(pxsys_window_service_update(service, &next) == PXSYS_STATUS_OK);
    assert(observer.calls == 2);
    assert(pxsys_window_service_unsubscribe(service, &observer, changed) ==
           PXSYS_STATUS_OK);
    assert(pxsys_window_service_destroy(service) == PXSYS_STATUS_OK);
    return 0;
}

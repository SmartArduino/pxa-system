#include <assert.h>
#include <string.h>

#include "pxsys/pxsys.h"

int main(void) {
    assert(strcmp(pxsys_status_name(PXSYS_STATUS_OK), "ok") == 0);
    assert(strcmp(pxsys_status_name(PXSYS_STATUS_PENDING), "pending") == 0);
    assert(strcmp(pxsys_status_name((pxsys_status_t)999), "unknown") == 0);
    pxsys_app_lifecycle_t lifecycle;
    pxsys_app_registry_config_t config;
    pxsys_app_lifecycle_init(&lifecycle);
    pxsys_app_registry_config_init(&config);
    return lifecycle.state == PXSYS_APP_ALLOCATED && config.struct_size == sizeof(config) ? 0 : 1;
}

#ifndef PXSYS_APP_LIFECYCLE_H
#define PXSYS_APP_LIFECYCLE_H

#include <stdint.h>

#include "pxsys/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t pxsys_app_state_t;
#define PXSYS_APP_ALLOCATED ((pxsys_app_state_t)0)
#define PXSYS_APP_CREATING ((pxsys_app_state_t)1)
#define PXSYS_APP_READY ((pxsys_app_state_t)2)
#define PXSYS_APP_STARTING ((pxsys_app_state_t)3)
#define PXSYS_APP_BACKGROUND ((pxsys_app_state_t)4)
#define PXSYS_APP_FOREGROUND ((pxsys_app_state_t)5)
#define PXSYS_APP_STOP_REQUESTED ((pxsys_app_state_t)6)
#define PXSYS_APP_STOPPING ((pxsys_app_state_t)7)
#define PXSYS_APP_DESTROY_PENDING ((pxsys_app_state_t)8)
#define PXSYS_APP_DESTROYED ((pxsys_app_state_t)9)

typedef uint8_t pxsys_stop_reason_t;
#define PXSYS_STOP_NORMAL ((pxsys_stop_reason_t)0)
#define PXSYS_STOP_REPLACED ((pxsys_stop_reason_t)1)
#define PXSYS_STOP_POLICY ((pxsys_stop_reason_t)2)
#define PXSYS_STOP_RESOURCE_PRESSURE ((pxsys_stop_reason_t)3)
#define PXSYS_STOP_PERMISSION_REVOKED ((pxsys_stop_reason_t)4)
#define PXSYS_STOP_FAULT ((pxsys_stop_reason_t)5)
#define PXSYS_STOP_SHUTDOWN ((pxsys_stop_reason_t)6)

typedef struct {
    pxsys_app_state_t state;
    pxsys_stop_reason_t stop_reason;
    uint64_t generation;
} pxsys_app_lifecycle_t;

void pxsys_app_lifecycle_init(pxsys_app_lifecycle_t* lifecycle);
pxsys_status_t pxsys_app_lifecycle_begin_create(pxsys_app_lifecycle_t* lifecycle);
pxsys_status_t pxsys_app_lifecycle_finish_create(pxsys_app_lifecycle_t* lifecycle,
                                                 pxsys_status_t result);
pxsys_status_t pxsys_app_lifecycle_begin_start(pxsys_app_lifecycle_t* lifecycle);
pxsys_status_t pxsys_app_lifecycle_finish_start(pxsys_app_lifecycle_t* lifecycle,
                                                pxsys_status_t result);
pxsys_status_t pxsys_app_lifecycle_foreground(pxsys_app_lifecycle_t* lifecycle);
pxsys_status_t pxsys_app_lifecycle_background(pxsys_app_lifecycle_t* lifecycle);
pxsys_status_t pxsys_app_lifecycle_request_stop(pxsys_app_lifecycle_t* lifecycle,
                                                pxsys_stop_reason_t reason);
pxsys_status_t pxsys_app_lifecycle_begin_stop(pxsys_app_lifecycle_t* lifecycle);
pxsys_status_t pxsys_app_lifecycle_finish_stop(pxsys_app_lifecycle_t* lifecycle);
pxsys_status_t pxsys_app_lifecycle_finish_destroy(pxsys_app_lifecycle_t* lifecycle);

#ifdef __cplusplus
}
#endif

#endif

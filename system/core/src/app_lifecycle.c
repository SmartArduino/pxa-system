#include "pxsys/app_lifecycle.h"

#include <stddef.h>

static pxsys_status_t transition(pxsys_app_lifecycle_t* lifecycle, pxsys_app_state_t expected,
                                 pxsys_app_state_t next) {
    if (lifecycle == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (lifecycle->state != expected)
        return PXSYS_STATUS_BAD_STATE;
    lifecycle->state = next;
    lifecycle->generation++;
    return PXSYS_STATUS_OK;
}

static int stop_reason_valid(pxsys_stop_reason_t reason) { return reason <= PXSYS_STOP_SHUTDOWN; }

void pxsys_app_lifecycle_init(pxsys_app_lifecycle_t* lifecycle) {
    if (lifecycle == NULL)
        return;
    lifecycle->state = PXSYS_APP_ALLOCATED;
    lifecycle->stop_reason = PXSYS_STOP_NORMAL;
    lifecycle->generation = 1;
}

pxsys_status_t pxsys_app_lifecycle_begin_create(pxsys_app_lifecycle_t* lifecycle) {
    return transition(lifecycle, PXSYS_APP_ALLOCATED, PXSYS_APP_CREATING);
}

pxsys_status_t pxsys_app_lifecycle_finish_create(pxsys_app_lifecycle_t* lifecycle,
                                                 pxsys_status_t result) {
    if (lifecycle == NULL || !pxsys_status_is_known(result)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (lifecycle->state != PXSYS_APP_CREATING)
        return PXSYS_STATUS_BAD_STATE;
    lifecycle->state = result == PXSYS_STATUS_OK ? PXSYS_APP_READY : PXSYS_APP_DESTROY_PENDING;
    if (result != PXSYS_STATUS_OK)
        lifecycle->stop_reason = PXSYS_STOP_FAULT;
    lifecycle->generation++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_app_lifecycle_begin_start(pxsys_app_lifecycle_t* lifecycle) {
    return transition(lifecycle, PXSYS_APP_READY, PXSYS_APP_STARTING);
}

pxsys_status_t pxsys_app_lifecycle_finish_start(pxsys_app_lifecycle_t* lifecycle,
                                                pxsys_status_t result) {
    if (lifecycle == NULL || !pxsys_status_is_known(result)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (lifecycle->state != PXSYS_APP_STARTING)
        return PXSYS_STATUS_BAD_STATE;
    lifecycle->state = result == PXSYS_STATUS_OK ? PXSYS_APP_BACKGROUND : PXSYS_APP_STOP_REQUESTED;
    if (result != PXSYS_STATUS_OK)
        lifecycle->stop_reason = PXSYS_STOP_FAULT;
    lifecycle->generation++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_app_lifecycle_foreground(pxsys_app_lifecycle_t* lifecycle) {
    if (lifecycle != NULL && lifecycle->state == PXSYS_APP_FOREGROUND) {
        return PXSYS_STATUS_OK;
    }
    return transition(lifecycle, PXSYS_APP_BACKGROUND, PXSYS_APP_FOREGROUND);
}

pxsys_status_t pxsys_app_lifecycle_background(pxsys_app_lifecycle_t* lifecycle) {
    if (lifecycle != NULL && lifecycle->state == PXSYS_APP_BACKGROUND) {
        return PXSYS_STATUS_OK;
    }
    return transition(lifecycle, PXSYS_APP_FOREGROUND, PXSYS_APP_BACKGROUND);
}

pxsys_status_t pxsys_app_lifecycle_request_stop(pxsys_app_lifecycle_t* lifecycle,
                                                pxsys_stop_reason_t reason) {
    if (lifecycle == NULL || !stop_reason_valid(reason)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (lifecycle->state == PXSYS_APP_STOP_REQUESTED) {
        return PXSYS_STATUS_OK;
    }
    if (lifecycle->state == PXSYS_APP_READY) {
        lifecycle->state = PXSYS_APP_DESTROY_PENDING;
    } else if (lifecycle->state == PXSYS_APP_STARTING || lifecycle->state == PXSYS_APP_BACKGROUND ||
               lifecycle->state == PXSYS_APP_FOREGROUND) {
        lifecycle->state = PXSYS_APP_STOP_REQUESTED;
    } else {
        return PXSYS_STATUS_BAD_STATE;
    }
    lifecycle->stop_reason = reason;
    lifecycle->generation++;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_app_lifecycle_begin_stop(pxsys_app_lifecycle_t* lifecycle) {
    return transition(lifecycle, PXSYS_APP_STOP_REQUESTED, PXSYS_APP_STOPPING);
}

pxsys_status_t pxsys_app_lifecycle_finish_stop(pxsys_app_lifecycle_t* lifecycle) {
    return transition(lifecycle, PXSYS_APP_STOPPING, PXSYS_APP_DESTROY_PENDING);
}

pxsys_status_t pxsys_app_lifecycle_finish_destroy(pxsys_app_lifecycle_t* lifecycle) {
    return transition(lifecycle, PXSYS_APP_DESTROY_PENDING, PXSYS_APP_DESTROYED);
}

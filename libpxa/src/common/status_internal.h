#ifndef PXA_STATUS_INTERNAL_H
#define PXA_STATUS_INTERNAL_H

#include "pxa/status.h"

static inline pxa_status_t pxa_status_normalize(pxa_status_t status) {
    return pxa_status_is_known(status) ? status : PXA_STATUS_INTERNAL;
}

#endif

#ifndef PXSYS_STATUS_H
#define PXSYS_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PXSYS_STATUS_OK = 0,
    PXSYS_STATUS_INVALID_ARGUMENT,
    PXSYS_STATUS_NOT_FOUND,
    PXSYS_STATUS_ALREADY_EXISTS,
    PXSYS_STATUS_RESOURCE_LIMIT,
    PXSYS_STATUS_NO_MEMORY,
    PXSYS_STATUS_UNSUPPORTED,
    PXSYS_STATUS_DENIED,
    PXSYS_STATUS_BUSY,
    PXSYS_STATUS_BAD_STATE,
    PXSYS_STATUS_CANCELLED,
    PXSYS_STATUS_TIMEOUT,
    PXSYS_STATUS_INTERNAL,
    PXSYS_STATUS_UNAVAILABLE,
    PXSYS_STATUS_CONFLICT,
    /* Operation was accepted and must be completed through its async API. */
    PXSYS_STATUS_PENDING,
} pxsys_status_t;

int pxsys_status_is_known(pxsys_status_t status);
const char* pxsys_status_name(pxsys_status_t status);

#ifdef __cplusplus
}
#endif

#endif

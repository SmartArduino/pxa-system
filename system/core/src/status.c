#include "pxsys/status.h"

int pxsys_status_is_known(pxsys_status_t status) {
    return status >= PXSYS_STATUS_OK && status <= PXSYS_STATUS_PENDING;
}

const char* pxsys_status_name(pxsys_status_t status) {
    static const char* const names[] = {
        "ok",        "invalid-argument", "not-found", "already-exists", "resource-limit",
        "no-memory", "unsupported",      "denied",    "busy",           "bad-state",
        "cancelled", "timeout",          "internal",  "unavailable",    "conflict",
        "pending",
    };
    return pxsys_status_is_known(status) ? names[(unsigned)status] : "unknown";
}

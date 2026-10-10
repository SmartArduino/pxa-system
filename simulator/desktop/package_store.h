#ifndef PXSYS_DESKTOP_PACKAGE_STORE_H
#define PXSYS_DESKTOP_PACKAGE_STORE_H
#include <stdbool.h>
#include <stdint.h>
#include "pxa/status.h"

/* Desktop-only management of committed user packages. The caller authenticates
 * the installed manifest and stops every live instance before a mutation. */
typedef enum {
    PXSYS_DESKTOP_PACKAGE_ENABLE,
    PXSYS_DESKTOP_PACKAGE_DISABLE,
    PXSYS_DESKTOP_PACKAGE_CLEAR_DATA,
    PXSYS_DESKTOP_PACKAGE_UNINSTALL,
} pxsys_desktop_package_action_t;
bool pxsys_desktop_package_installed(const char *packages, const char *app_id);
bool pxsys_desktop_package_enabled(const char *packages, const char *app_id);
bool pxsys_desktop_package_has_data(const char *state, const char *app_id,
                                   const uint8_t publisher[32]);
pxa_status_t pxsys_desktop_package_manage(const char *packages, const char *state,
    const char *app_id, const uint8_t publisher[32],
    pxsys_desktop_package_action_t action);
#endif

/* SPDX-License-Identifier: MIT */
#ifndef PXSYS_SIMULATOR_DEFAULTS_H
#define PXSYS_SIMULATOR_DEFAULTS_H
#include <string.h>
#ifndef PXSYS_SIMULATOR_NAVIGATION_BAR
#define PXSYS_SIMULATOR_NAVIGATION_BAR 0
#endif
/* 0: visible gesture handle, 1: gestures without a handle, 2: buttons. */
static inline unsigned pxsys_simulator_navigation_mode(const char *mode) {
    if (mode && strcmp(mode,"gesture-no-bar")==0) return 1;
    if (mode && strcmp(mode,"buttons")==0) return 2;
    if (mode && (strcmp(mode,"gesture")==0 || strcmp(mode,"gestures")==0)) return 0;
    return PXSYS_SIMULATOR_NAVIGATION_BAR ? 0u : 1u;
}
#endif

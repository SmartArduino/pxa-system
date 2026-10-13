#ifndef PXSYS_DESKTOP_RESOURCES_H
#define PXSYS_DESKTOP_RESOURCES_H

#include <stdlib.h>

/* The developer launcher supplies the relocatable installed font. Source
 * builds retain their configured fallback; no cache or allocation is needed. */
static inline const char *pxsys_desktop_font_path(void) {
    const char *path = getenv("PXA_SIMULATOR_FONT");
    if (path != NULL && path[0] != '\0') return path;
#ifdef PXSYS_DESKTOP_TEXT_FONT
    return PXSYS_DESKTOP_TEXT_FONT;
#else
    return "";
#endif
}

#endif

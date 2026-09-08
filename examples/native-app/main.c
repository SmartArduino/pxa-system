#include <stdio.h>

#include "pxsys/theme.h"

int main(void) {
    pxsys_theme_snapshot_t theme;
    pxsys_theme_snapshot_init(&theme, PXSYS_COLOR_SCHEME_DARK);
    printf("native PXA System example: dark background=0x%08x\n",
           (unsigned int)theme.colors[PXSYS_COLOR_BACKGROUND]);
    return theme.effective_scheme == PXSYS_COLOR_SCHEME_DARK ? 0 : 1;
}

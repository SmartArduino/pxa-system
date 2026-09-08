#include <assert.h>

#include "pxsys/reference_layout.h"

int main(void) {
    pxsys_display_profile_t display;
    pxsys_reference_layout_t layout;

    pxsys_display_profile_init(&display, 296, 240);
    display.shape = PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE;
    display.safe_insets = (pxsys_insets_t){8, 12, 8, 12};
    assert(pxsys_reference_layout_compute(&display, &layout) == PXSYS_STATUS_OK);
    assert(layout.size_class == PXSYS_UI_SIZE_COMPACT);
    assert(layout.safe_area.x == 12 && layout.safe_area.width == 272);
    assert(layout.status_bar.x == 0 && layout.status_bar.y == 0 &&
           layout.status_bar.width == display.width);
    assert(layout.navigation_bar.x == 0 &&
           layout.navigation_bar.y + (int32_t)layout.navigation_bar.height ==
               (int32_t)display.height);
    assert(layout.content.height > 0 && layout.grid_columns >= 2);

    pxsys_display_profile_init(&display, 454, 454);
    display.shape = PXSYS_DISPLAY_SHAPE_CIRCLE;
    assert(pxsys_reference_layout_compute(&display, &layout) == PXSYS_STATUS_OK);
    assert(layout.safe_area.width < display.width);
    assert(layout.grid_columns <= 2);

    pxsys_display_profile_init(&display, 1280, 720);
    assert(pxsys_reference_layout_compute(&display, &layout) == PXSYS_STATUS_OK);
    assert(layout.size_class == PXSYS_UI_SIZE_EXPANDED);
    assert(layout.grid_columns == 6);
    return 0;
}

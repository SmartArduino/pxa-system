#include <assert.h>

#include "pxsys/reference_layout.h"

int main(void) {
    pxsys_display_profile_t display;
    pxsys_reference_layout_t layout;
    pxsys_rect_t status_content;
    const unsigned sizes[]={128,240,296,480,640,800};
    for(unsigned i=0;i<6;++i) for(unsigned shape=0;shape<3;++shape) {
        pxsys_rect_t keyboard;
        pxsys_display_profile_init(&display,sizes[i],sizes[i]);
        display.shape=(pxsys_display_shape_t)shape;
        display.corner_radii=(pxsys_corner_radii_t){sizes[i]/4,sizes[i]/4,sizes[i]/4,sizes[i]/4};
        display.safe_insets=(pxsys_insets_t){8,8,8,8};
        display.density_dpi=320;
        assert(pxsys_reference_input_method_rect(&display,&keyboard)==PXSYS_STATUS_OK);
        assert(keyboard.width>0 && keyboard.height>0);
        for(unsigned y=0;y<keyboard.height;++y) for(unsigned x=0;x<keyboard.width;++x)
            assert(pxsys_display_contains_point(&display,keyboard.x+x,keyboard.y+y));
    }

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
    assert(layout.content.y == 32 && layout.content.height == 166);
    assert(layout.grid_columns >= 2);
    assert(pxsys_reference_layout_status_content_rect(
               &display, &layout, 16, &status_content) == PXSYS_STATUS_OK);
    assert(status_content.x == layout.safe_area.x &&
           status_content.width == layout.safe_area.width);

    display.corner_radii = (pxsys_corner_radii_t){100, 80, 0, 0};
    assert(pxsys_reference_layout_compute(&display, &layout) == PXSYS_STATUS_OK);
    assert(pxsys_reference_layout_status_content_rect(
               &display, &layout, 16, &status_content) == PXSYS_STATUS_OK);
    assert(layout.status_bar.x == 0 && layout.status_bar.width == display.width);
    assert(status_content.x > layout.safe_area.x &&
           status_content.x + (int32_t)status_content.width <
               layout.safe_area.x + (int32_t)layout.safe_area.width);
    for (int32_t row = status_content.y;
         row < status_content.y + (int32_t)status_content.height; ++row) {
        assert(pxsys_display_contains_point(&display, status_content.x, row));
        assert(pxsys_display_contains_point(
            &display, status_content.x + (int32_t)status_content.width - 1, row));
    }
    assert(pxsys_reference_layout_status_content_rect(
               &display, &layout, 0, &status_content) ==
           PXSYS_STATUS_INVALID_ARGUMENT);

    pxsys_display_profile_init(&display, 454, 454);
    display.shape = PXSYS_DISPLAY_SHAPE_CIRCLE;
    assert(pxsys_reference_layout_compute(&display, &layout) == PXSYS_STATUS_OK);
    assert(layout.safe_area.width < display.width);
    assert(layout.status_bar.width == display.width);
    assert(pxsys_reference_layout_status_content_rect(
               &display, &layout, 16, &status_content) == PXSYS_STATUS_OK);
    assert(status_content.x >= layout.safe_area.x &&
           status_content.x + (int32_t)status_content.width <=
               layout.safe_area.x + (int32_t)layout.safe_area.width);
    assert(layout.grid_columns <= 2);

    pxsys_display_profile_init(&display, 480, 480);
    display.density_dpi = 305;
    display.shape = PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE;
    display.corner_radii = (pxsys_corner_radii_t){58, 58, 58, 58};
    display.safe_insets = (pxsys_insets_t){12, 12, 12, 12};
    assert(pxsys_reference_layout_compute(&display, &layout) == PXSYS_STATUS_OK);
    assert(layout.safe_area.width == 456 && layout.safe_area.height == 456);
    assert(layout.status_bar.height == 48);
    assert(layout.navigation_bar.height == 69);
    assert(layout.content.width == 420 && layout.content.height == 345);
    assert(pxsys_reference_display_scale_px(&display, 50) == 75);
    {
        pxsys_theme_snapshot_t theme;
        pxsys_theme_snapshot_init(&theme, PXSYS_COLOR_SCHEME_DARK);
        pxsys_reference_theme_adapt_display(&display, &theme);
        assert(pxsys_theme_snapshot_validate(&theme) == PXSYS_STATUS_OK);
        assert(theme.typography_px[PXSYS_TYPOGRAPHY_BODY] == 24);
        assert(theme.typography_px[PXSYS_TYPOGRAPHY_CAPTION] == 18);
        assert(pxsys_reference_layout_status_content_rect(
                   &display, &layout, 22, &status_content) == PXSYS_STATUS_OK);
        for (int32_t row = status_content.y;
             row < status_content.y + (int32_t)status_content.height; ++row) {
            assert(pxsys_display_contains_point(&display, status_content.x, row));
            assert(pxsys_display_contains_point(&display,
                status_content.x + (int32_t)status_content.width - 1, row));
        }
    }
    display.density_dpi = 160;
    assert(pxsys_reference_display_scale_px(&display, 50) == 50);
    display.density_dpi = UINT16_MAX;
    assert(pxsys_reference_display_scale_px(&display, 50) == 75);
    assert(pxsys_reference_display_scale_px(&display, UINT16_MAX) == UINT16_MAX);

    pxsys_display_profile_init(&display, 1280, 720);
    assert(pxsys_reference_layout_compute(&display, &layout) == PXSYS_STATUS_OK);
    assert(layout.size_class == PXSYS_UI_SIZE_EXPANDED);
    assert(layout.grid_columns == 6);
    assert(pxsys_reference_layout_gesture_strip_height(
               PXSYS_UI_SIZE_COMPACT) == 20);
    assert(pxsys_reference_layout_gesture_strip_height(
               PXSYS_UI_SIZE_REGULAR) == 24);
    assert(pxsys_reference_layout_gesture_strip_height(
               PXSYS_UI_SIZE_EXPANDED) == 28);
    assert(pxsys_reference_layout_back_gesture_width() == 16);
    return 0;
}

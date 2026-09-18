#ifndef PXSYS_REFERENCE_LAYOUT_H
#define PXSYS_REFERENCE_LAYOUT_H

#include <stdint.h>

#include "pxsys/display.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PXSYS_UI_SIZE_COMPACT = 0,
    PXSYS_UI_SIZE_REGULAR,
    PXSYS_UI_SIZE_EXPANDED,
} pxsys_ui_size_class_t;

typedef struct {
    uint32_t struct_size;
    pxsys_ui_size_class_t size_class;
    pxsys_rect_t viewport;
    pxsys_rect_t safe_area;
    pxsys_rect_t status_bar;
    pxsys_rect_t content;
    pxsys_rect_t navigation_bar;
    uint16_t outer_padding;
    uint16_t item_gap;
    uint16_t tile_min_width;
    uint8_t grid_columns;
    uint8_t reserved[3];
} pxsys_reference_layout_t;

pxsys_status_t pxsys_reference_layout_compute(
    const pxsys_display_profile_t* display,
    pxsys_reference_layout_t* output);

/* System gesture strips reserved by gesture navigation: the bottom Home strip
 * and the left-edge Back strip. Products report them to applications through
 * the window snapshot's system bar insets. */
uint32_t pxsys_reference_layout_gesture_strip_height(
    pxsys_ui_size_class_t size_class);
uint32_t pxsys_reference_layout_back_gesture_width(void);

#ifdef __cplusplus
}
#endif

#endif

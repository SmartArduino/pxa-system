#ifndef PXSYS_REFERENCE_LAYOUT_H
#define PXSYS_REFERENCE_LAYOUT_H

#include <stdint.h>

#include "pxsys/display.h"
#include "pxsys/theme.h"

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

/* Reference UI readability scale: retain legacy sizing up to 200 DPI and
 * increase to at most 150% on small, dense panels. Apply to a fresh theme. */
uint16_t pxsys_reference_display_scale_px(
    const pxsys_display_profile_t* display, uint16_t pixels);
void pxsys_reference_theme_adapt_display(
    const pxsys_display_profile_t* display, pxsys_theme_snapshot_t* theme);
/* Keyboard rectangle inside effective insets and convex display boundaries. */
pxsys_status_t pxsys_reference_input_method_rect(
    const pxsys_display_profile_t* display, pxsys_rect_t* output);

pxsys_status_t pxsys_reference_layout_compute(
    const pxsys_display_profile_t* display,
    pxsys_reference_layout_t* output);

pxsys_status_t pxsys_reference_layout_status_content_rect(
    const pxsys_display_profile_t* display,
    const pxsys_reference_layout_t* layout, uint32_t content_height,
    pxsys_rect_t* output);

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

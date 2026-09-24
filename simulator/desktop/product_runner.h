#ifndef PXSYS_PRODUCT_RUNNER_H
#define PXSYS_PRODUCT_RUNNER_H

#include <stdint.h>
#include <stdbool.h>

#include "lvgl.h"
#include "pxa/window.h"
#include "pxsys/display.h"
#include "pxsys/theme.h"

typedef void (*pxsys_product_simulator_pump_fn)(void *context);
typedef void (*pxsys_product_simulator_window_fn)(
    void *context, const pxa_window_configuration_t *configuration);
typedef void (*pxsys_product_simulator_control_bind_fn)(
    void *context, void (*set_focus)(void *runner, bool focused),
    void (*request_exit)(void *runner), void *runner);

/* Runs a package against an already-initialized LVGL display. The call returns
 * when the package exits, leaving the caller's display and input devices live.
 * The Guest UI is parented to the supplied application surface. */
int pxsys_product_simulator_run_embedded(const char *package_path,
                                         const char *publisher_key,
                                         const char *state_root,
                                         const char *locale,
                                         uint32_t width, uint32_t height,
                                         lv_display_t *display, lv_obj_t *parent,
                                         pxsys_product_simulator_pump_fn pump,
                                         void *pump_context,
                                         pxsys_product_simulator_window_fn window_changed,
                                         void *window_context,
                                         pxsys_product_simulator_control_bind_fn control_bind,
                                         const pxsys_display_profile_t *host_display,
                                         const pxsys_insets_t *host_bar_insets,
                                         const pxsys_theme_snapshot_t *host_theme);

#endif

#ifndef PXSYS_PRODUCT_RUNNER_H
#define PXSYS_PRODUCT_RUNNER_H

#include <stdint.h>

#include "lvgl.h"

typedef void (*pxsys_product_simulator_pump_fn)(void *context);

/* Runs a package against an already-initialized LVGL display. The call returns
 * when the package exits, leaving the caller's display and input devices live. */
int pxsys_product_simulator_run_embedded(const char *package_path,
                                         const char *publisher_key,
                                         const char *state_root,
                                         const char *locale,
                                         uint32_t width, uint32_t height,
                                         lv_display_t *display,
                                         pxsys_product_simulator_pump_fn pump,
                                         void *pump_context);

#endif

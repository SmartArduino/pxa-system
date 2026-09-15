#ifndef PXSYS_PRODUCT_RUNNER_H
#define PXSYS_PRODUCT_RUNNER_H

/* Runs an installed package on the desktop simulator's existing LVGL display. */
int pxsys_product_simulator_run_embedded(const char *package_path,
                                         const char *publisher_key,
                                         const char *state_root);

#endif

/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PXSYS_DESKTOP_HOST_EXTENSIONS_H
#define PXSYS_DESKTOP_HOST_EXTENSIONS_H
#include <pxa/assets.h>
#include <pxa/permission.h>

/* Native product composition hook, not a Guest ABI. The Core remains
 * independent of workspace/vendor services; an optional CMake provider owns
 * their lifetime. */
typedef struct {
  pxa_runtime_t *runtime;
  pxa_permission_service_t *permissions;
  pxa_assets_service_t *assets;
  const pxa_memory_allocator_t *frame_allocator;
} pxsys_desktop_host_extensions_config_t;
#ifdef PXSYS_DESKTOP_HOST_EXTENSIONS
void *pxsys_desktop_host_extensions_create(
    const pxsys_desktop_host_extensions_config_t *);
void pxsys_desktop_host_extensions_foreground(void *, pxa_component_t);
void pxsys_desktop_host_extensions_poll(void *);
void pxsys_desktop_host_extensions_destroy(void *);
#else
static inline void *pxsys_desktop_host_extensions_create(
    const pxsys_desktop_host_extensions_config_t *c) {
  (void)c;
  return NULL;
}
static inline void pxsys_desktop_host_extensions_foreground(void *c,
                                                            pxa_component_t p) {
  (void)c;
  (void)p;
}
static inline void pxsys_desktop_host_extensions_poll(void *c) { (void)c; }
static inline void pxsys_desktop_host_extensions_destroy(void *c) { (void)c; }
#endif
#endif

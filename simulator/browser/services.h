/* SPDX-License-Identifier: MIT */
#ifndef PXA_BROWSER_SERVICES_H
#define PXA_BROWSER_SERVICES_H
#include "pxa/runtime.h"
#include "pxa/permission.h"
pxa_status_t browser_services_init(pxa_runtime_t *runtime);
pxa_status_t browser_audio_init(pxa_runtime_t *,pxa_permission_service_t *);
pxa_status_t browser_net_init(pxa_runtime_t *,pxa_permission_service_t *);
void browser_net_tick(void);
pxa_status_t browser_graphics_init(pxa_runtime_t *,uint16_t,uint16_t);
void browser_graphics_presented(void);
void browser_graphics_order(void);
#endif

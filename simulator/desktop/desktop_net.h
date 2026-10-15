#ifndef PXSYS_DESKTOP_NET_H
#define PXSYS_DESKTOP_NET_H

#include "pxa/net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pxsys_desktop_net_notify_fn)(void *context);

/* libcurl-backed asynchronous HTTP(S) backend for the PXA net service. Each
 * request runs on its own worker thread; notify is called once per completed
 * request so the host can wake its maintenance loop. */
int pxsys_desktop_net_backend(pxa_net_backend_t *output,
                              pxsys_desktop_net_notify_fn notify,
                              void *notify_context);

/* Cancel in-flight work and detach callbacks before destroying the Host. */
void pxsys_desktop_net_shutdown(pxa_net_backend_t *backend);

/* Owner-thread idle wait, interrupted by network progress. Reuses live
 * exchange conditions; allocates no synchronization objects. Returns zero
 * when no stream is active and the caller should use its normal idle wait. */
int pxsys_desktop_net_wait(pxa_net_backend_t *backend, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif

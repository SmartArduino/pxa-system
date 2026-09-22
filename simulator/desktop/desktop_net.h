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

#ifdef __cplusplus
}
#endif

#endif

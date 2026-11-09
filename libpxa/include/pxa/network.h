/* SPDX-License-Identifier: MIT */
#ifndef PXA_NETWORK_H
#define PXA_NETWORK_H
#include "pxa/lan.h"
typedef pxa_lan_backend_t pxa_network_backend_t;
typedef pxa_lan_request_t pxa_network_request_t;
#define PXA_NET_NETWORK_OPEN PXA_NET_LOCAL_OPEN
#define PXA_NET_NETWORK_CAPABILITIES PXA_NET_LOCAL_CAPABILITIES
#endif

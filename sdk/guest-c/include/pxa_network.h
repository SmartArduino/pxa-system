/* SPDX-License-Identifier: MIT */
#ifndef PXA_GUEST_NETWORK_H
#define PXA_GUEST_NETWORK_H
#include "pxa_lan.h"
#define PXA_NETWORK_SERVICE PXA_LOCAL_NETWORK_SERVICE
#define PXA_NETWORK_OPEN PXA_LOCAL_NETWORK_OPEN
#define PXA_NETWORK_CAPABILITIES PXA_LOCAL_NETWORK_CAPABILITIES
#define PXA_NETWORK_SCOPE "network"
#define PXA_NETWORK_FEATURE_CONNECT 16u
typedef pxa_local_endpoint_t pxa_network_endpoint_t;
typedef pxa_local_request_t pxa_network_request_t;
#define pxa_network_open pxa_local_open
#endif

#ifndef PXSYS_INTENT_WIRE_H
#define PXSYS_INTENT_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/intent.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_INTENT_WIRE_VERSION UINT16_C(1)

pxsys_status_t pxsys_intent_wire_size(const pxsys_intent_t* intent, size_t* size);
pxsys_status_t pxsys_intent_wire_encode(const pxsys_intent_t* intent, void* buffer, size_t capacity,
                                        size_t* written);
/* Decoded strings and bytes are views into buffer and remain valid only while it does. */
pxsys_status_t pxsys_intent_wire_decode(const void* buffer, size_t size, pxsys_intent_t* intent,
                                        pxsys_app_identity_t* target_storage);

#ifdef __cplusplus
}
#endif

#endif

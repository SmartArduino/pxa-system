#ifndef PXSYS_TOAST_H
#define PXSYS_TOAST_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/status.h"
#include "pxsys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_TOAST_INTERFACE_ID "system.ui.toast"
#define PXSYS_TOAST_OPERATION_POST UINT32_C(1)
#define PXSYS_TOAST_WIRE_HEADER_BYTES 8u

typedef enum {
    PXSYS_TOAST_DEFAULT = 0,
    PXSYS_TOAST_SUCCESS,
    PXSYS_TOAST_WARNING,
    PXSYS_TOAST_ERROR,
} pxsys_toast_tone_t;

typedef struct {
    uint32_t struct_size;
    uint64_t sequence;
    pxsys_string_t message;
    uint32_t duration_ms;
    pxsys_toast_tone_t tone;
} pxsys_toast_message_t;

typedef void (*pxsys_toast_posted_fn)(
    void* context, const pxsys_toast_message_t* toast);

typedef struct {
    uint32_t struct_size;
    size_t max_observers;
    size_t max_message_bytes;
    pxsys_allocator_t allocator;
} pxsys_toast_service_config_t;

typedef struct pxsys_toast_service pxsys_toast_service_t;

void pxsys_toast_message_init(pxsys_toast_message_t* toast,
                              pxsys_string_t message);
void pxsys_toast_service_config_init(pxsys_toast_service_config_t* config);
pxsys_status_t pxsys_toast_service_create(
    const pxsys_toast_service_config_t* config,
    pxsys_toast_service_t** output);
pxsys_status_t pxsys_toast_service_destroy(pxsys_toast_service_t* service);
pxsys_status_t pxsys_toast_service_post(
    pxsys_toast_service_t* service, const pxsys_toast_message_t* toast);
pxsys_status_t pxsys_toast_service_subscribe(
    pxsys_toast_service_t* service, void* context,
    pxsys_toast_posted_fn callback);
pxsys_status_t pxsys_toast_service_unsubscribe(
    pxsys_toast_service_t* service, void* context,
    pxsys_toast_posted_fn callback);

size_t pxsys_toast_wire_size(const pxsys_toast_message_t* toast);
pxsys_status_t pxsys_toast_wire_encode(const pxsys_toast_message_t* toast,
                                       uint8_t* output, size_t output_size,
                                       size_t* written);
pxsys_status_t pxsys_toast_wire_decode(pxsys_bytes_t payload,
                                       pxsys_toast_message_t* toast);

#ifdef __cplusplus
}
#endif

#endif

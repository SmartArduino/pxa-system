#ifndef PXSYS_LOCALE_H
#define PXSYS_LOCALE_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/status.h"
#include "pxsys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_LOCALE_TAG_MAX_BYTES 63u

typedef enum {
    PXSYS_TEXT_DIRECTION_LTR = 0,
    PXSYS_TEXT_DIRECTION_RTL,
} pxsys_text_direction_t;

typedef struct {
    uint32_t struct_size;
    uint64_t generation;
    pxsys_text_direction_t direction;
    uint16_t tag_size;
    char tag[PXSYS_LOCALE_TAG_MAX_BYTES + 1u];
} pxsys_locale_snapshot_t;

typedef void (*pxsys_locale_changed_fn)(
    void* context, const pxsys_locale_snapshot_t* locale);

typedef struct {
    uint32_t struct_size;
    size_t max_observers;
    pxsys_allocator_t allocator;
} pxsys_locale_service_config_t;

typedef struct pxsys_locale_service pxsys_locale_service_t;

pxsys_status_t pxsys_locale_snapshot_init(pxsys_locale_snapshot_t* snapshot,
                                          pxsys_string_t tag);
pxsys_status_t pxsys_locale_snapshot_validate(
    const pxsys_locale_snapshot_t* snapshot);
void pxsys_locale_service_config_init(pxsys_locale_service_config_t* config);
pxsys_status_t pxsys_locale_service_create(
    const pxsys_locale_service_config_t* config,
    const pxsys_locale_snapshot_t* initial, pxsys_locale_service_t** output);
pxsys_status_t pxsys_locale_service_destroy(pxsys_locale_service_t* service);
pxsys_status_t pxsys_locale_service_update(
    pxsys_locale_service_t* service, const pxsys_locale_snapshot_t* locale);
pxsys_status_t pxsys_locale_service_get(const pxsys_locale_service_t* service,
                                        pxsys_locale_snapshot_t* locale);
pxsys_status_t pxsys_locale_service_subscribe(pxsys_locale_service_t* service,
                                              void* context,
                                              pxsys_locale_changed_fn callback);
pxsys_status_t pxsys_locale_service_unsubscribe(
    pxsys_locale_service_t* service, void* context,
    pxsys_locale_changed_fn callback);

#ifdef __cplusplus
}
#endif

#endif

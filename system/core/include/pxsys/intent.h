#ifndef PXSYS_INTENT_H
#define PXSYS_INTENT_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/app_registry.h"
#include "pxsys/status.h"
#include "pxsys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_INTENT_FLAG_NEW_TASK UINT32_C(1)
#define PXSYS_INTENT_FLAG_SINGLE_TOP UINT32_C(2)
#define PXSYS_INTENT_FLAG_CLEAR_TOP UINT32_C(4)
#define PXSYS_INTENT_FLAG_EXPECT_RESULT UINT32_C(8)

typedef struct {
    uint32_t struct_size;
    /* NULL selects implicit resolution. */
    const pxsys_app_identity_t* target;
    pxsys_string_t action;
    pxsys_string_t uri;
    pxsys_string_t mime_type;
    uint32_t flags;
    uint64_t correlation_id;
    pxsys_bytes_t arguments;
} pxsys_intent_t;

typedef struct {
    uint32_t struct_size;
    pxsys_app_identity_t app;
    pxsys_string_t action;
    /* Empty scheme and MIME type are wildcards. */
    pxsys_string_t uri_scheme;
    pxsys_string_t mime_type;
    int32_t priority;
} pxsys_intent_filter_t;

typedef struct {
    uint32_t struct_size;
    size_t max_filters;
    size_t max_identifier_bytes;
    size_t max_mime_type_bytes;
    pxsys_app_registry_t* apps;
    pxsys_allocator_t allocator;
} pxsys_intent_resolver_config_t;

typedef struct pxsys_intent_resolver pxsys_intent_resolver_t;

void pxsys_intent_resolver_config_init(pxsys_intent_resolver_config_t* config);
pxsys_status_t pxsys_intent_resolver_create(const pxsys_intent_resolver_config_t* config,
                                            pxsys_intent_resolver_t** output);
pxsys_status_t pxsys_intent_resolver_destroy(pxsys_intent_resolver_t* resolver);
pxsys_status_t pxsys_intent_filter_register(pxsys_intent_resolver_t* resolver,
                                            const pxsys_intent_filter_t* filter);
pxsys_status_t pxsys_intent_filters_unregister(pxsys_intent_resolver_t* resolver,
                                               const pxsys_app_identity_t* app);
pxsys_status_t pxsys_intent_resolve(const pxsys_intent_resolver_t* resolver,
                                    const pxsys_intent_t* intent,
                                    const pxsys_app_descriptor_t** app);

#ifdef __cplusplus
}
#endif

#endif

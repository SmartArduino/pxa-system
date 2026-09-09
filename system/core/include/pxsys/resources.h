#ifndef PXSYS_RESOURCES_H
#define PXSYS_RESOURCES_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/status.h"
#include "pxsys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_RESOURCE_NAMESPACE_MAX_BYTES 95u
#define PXSYS_RESOURCE_LOCALE_MAX_BYTES 63u

typedef struct {
    pxsys_string_t key;
    pxsys_string_t value;
} pxsys_resource_entry_t;

/* Catalog strings and entries are borrowed and must outlive registration.
 * Higher priority catalogs override lower priority catalogs. */
typedef struct {
    uint32_t struct_size;
    pxsys_string_t resource_namespace;
    pxsys_string_t locale;
    const pxsys_resource_entry_t* entries;
    size_t entry_count;
    int32_t priority;
} pxsys_resource_catalog_t;

typedef struct {
    uint32_t struct_size;
    size_t max_catalogs;
    size_t max_key_bytes;
    size_t max_value_bytes;
    pxsys_allocator_t allocator;
} pxsys_resource_service_config_t;

typedef struct pxsys_resource_service pxsys_resource_service_t;

void pxsys_resource_service_config_init(
    pxsys_resource_service_config_t* config);
pxsys_status_t pxsys_resource_service_create(
    const pxsys_resource_service_config_t* config,
    pxsys_resource_service_t** output);
pxsys_status_t pxsys_resource_service_destroy(
    pxsys_resource_service_t* service);
pxsys_status_t pxsys_resource_catalog_register(
    pxsys_resource_service_t* service,
    const pxsys_resource_catalog_t* catalog);
pxsys_status_t pxsys_resource_catalog_unregister(
    pxsys_resource_service_t* service,
    const pxsys_resource_catalog_t* catalog);
/* The returned string is borrowed from the mounted catalog. Locale fallback
 * proceeds from an exact BCP 47 tag through parent tags, then the empty-tag
 * default catalog. */
pxsys_status_t pxsys_resource_resolve(
    const pxsys_resource_service_t* service, pxsys_string_t resource_namespace,
    pxsys_string_t key, pxsys_string_t locale, pxsys_string_t* value);

#ifdef __cplusplus
}
#endif

#endif

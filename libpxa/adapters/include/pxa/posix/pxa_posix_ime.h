#ifndef PXA_POSIX_IME_H
#define PXA_POSIX_IME_H
#include "pxa/ime.h"
#include "pxa/posix/pxa_posix_installer.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Optional installed-provider loader. Uses the application's existing runtime
 * and engine; a service never replaces or binds the foreground UI. Single
 * runtime owner; calls must be outside Guest imports and LVGL callbacks. */
typedef struct pxa_posix_ime_provider pxa_posix_ime_provider_t;
pxa_status_t pxa_posix_ime_provider_open(pxa_posix_installer_t *installer,
    const char *directory, pxa_runtime_t *runtime,
    const pxa_component_engine_t *engine,
    const pxa_package_activation_profile_t *services,
    const pxa_package_host_profile_t *host, pxa_posix_ime_provider_t **output);
/* Cooperative variant: read metadata now, authenticate and hash in steps.
 * engine is borrowed until verification completes; caller serializes access
 * to installer and keeps the installed source immutable until start. No
 * component exists until step returns OK and start is called. */
pxa_status_t pxa_posix_ime_provider_open_begin(pxa_posix_installer_t *,
    const char *directory, pxa_runtime_t *, const pxa_component_engine_t *,
    const pxa_package_activation_profile_t *, const pxa_package_host_profile_t *,
    pxa_posix_ime_provider_t **);
pxa_status_t pxa_posix_ime_provider_open_step(pxa_posix_ime_provider_t *, size_t byte_budget);
bool pxa_posix_ime_provider_loading(const pxa_posix_ime_provider_t *);
pxa_status_t pxa_posix_ime_provider_start(pxa_posix_ime_provider_t *provider,
    uint64_t instance, pxa_component_t *component);
/* Called by the engine's prepare_start hook. NOT_FOUND means another component.
 * Config contains a component-owned read-only dictionary capability. */
pxa_status_t pxa_posix_ime_provider_prepare(pxa_posix_ime_provider_t *provider,
    uint64_t instance, pxa_component_t component, uint8_t *config,
    size_t capacity, size_t *size);
void pxa_posix_ime_provider_close(pxa_posix_ime_provider_t *provider);
size_t pxa_posix_ime_provider_metadata_bytes(const pxa_posix_ime_provider_t *provider);
/* Installed registry discovery reads committed metadata without starting a
 * Guest. Only use a Host-managed installation root; execution authenticates
 * the selected package again. No resident manifest/catalog cache is kept. */
typedef void (*pxa_posix_ime_visit_fn)(void *context, const char *identity, pxa_bytes_t name);
pxa_status_t pxa_posix_ime_registry_visit(const char *packages,
    pxa_posix_ime_visit_fn visit, void *context);
pxa_status_t pxa_posix_ime_registry_resolve(const char *packages,
    const char *identity, char *directory, size_t capacity);
pxa_status_t pxa_posix_ime_selection_read(const char *file, char *identity, size_t capacity);
pxa_status_t pxa_posix_ime_preferences_read(const char *file,pxa_ime_preferences_t *output);
pxa_status_t pxa_posix_ime_preferences_write(const char *file,pxa_ime_preferences_t preferences);
pxa_status_t pxa_posix_ime_selection_write(const char *file, const char *identity);
#ifdef __cplusplus
}
#endif
#endif

#ifndef PXSYS_ROLE_REGISTRY_H
#define PXSYS_ROLE_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/app_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_ROLE_HOME "system.role.home"
#define PXSYS_ROLE_SETTINGS "system.role.settings"
#define PXSYS_ROLE_STATUS_BAR "system.role.status-bar"
#define PXSYS_ROLE_NAVIGATION_BAR "system.role.navigation-bar"
#define PXSYS_ROLE_LOCK_SCREEN "system.role.lock-screen"
#define PXSYS_ROLE_PERMISSION_PROMPT "system.role.permission-prompt"
#define PXSYS_ROLE_PACKAGE_INSTALLER "system.role.package-installer"
#define PXSYS_ROLE_APP_MANAGER "system.role.app-manager"
#define PXSYS_ROLE_FILE_PICKER "system.role.file-picker"
#define PXSYS_ROLE_THEME_PROVIDER "system.role.theme-provider"
#define PXSYS_ROLE_SOUND_SETTINGS "system.role.settings.sound"
#define PXSYS_ROLE_NETWORK_SETTINGS "system.role.settings.network"
#define PXSYS_ROLE_BLUETOOTH_SETTINGS "system.role.settings.bluetooth"
#define PXSYS_ROLE_ALARM_SETTINGS "system.role.settings.alarm"
#define PXSYS_ROLE_FILE_MANAGER "system.role.file-manager"
#define PXSYS_ROLE_DEVICE_INFO "system.role.device-info"

typedef struct {
    uint32_t struct_size;
    pxsys_string_t role_id;
    pxsys_app_identity_t app;
    int32_t priority;
    uint32_t flags;
} pxsys_role_candidate_t;

typedef pxsys_status_t (*pxsys_role_policy_fn)(void* context,
                                               const pxsys_role_candidate_t* candidate);

typedef struct {
    uint32_t struct_size;
    size_t max_candidates;
    size_t max_role_id_bytes;
    pxsys_app_registry_t* apps;
    void* policy_context;
    pxsys_role_policy_fn authorize;
    pxsys_allocator_t allocator;
} pxsys_role_registry_config_t;

typedef struct pxsys_role_registry pxsys_role_registry_t;

void pxsys_role_registry_config_init(pxsys_role_registry_config_t* config);
pxsys_status_t pxsys_role_registry_create(const pxsys_role_registry_config_t* config,
                                          pxsys_role_registry_t** output);
pxsys_status_t pxsys_role_registry_destroy(pxsys_role_registry_t* registry);
pxsys_status_t pxsys_role_candidate_register(pxsys_role_registry_t* registry,
                                             const pxsys_role_candidate_t* candidate);
pxsys_status_t pxsys_role_candidates_unregister(pxsys_role_registry_t* registry,
                                                const pxsys_app_identity_t* app);
pxsys_status_t pxsys_role_resolve(const pxsys_role_registry_t* registry, pxsys_string_t role_id,
                                  const pxsys_app_descriptor_t** app);

#ifdef __cplusplus
}
#endif

#endif

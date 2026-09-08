#ifndef PXSYS_RENDERER_H
#define PXSYS_RENDERER_H

#include <stdint.h>

#include "pxsys/status.h"
#include "pxsys/theme.h"
#include "pxsys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t slot;
    uint32_t generation;
} pxsys_surface_ref_t;

typedef enum {
    PXSYS_SURFACE_APPLICATION = 0,
    PXSYS_SURFACE_DIALOG,
    PXSYS_SURFACE_OVERLAY,
    PXSYS_SURFACE_SYSTEM_BAR,
    PXSYS_SURFACE_RAW_CONTENT,
} pxsys_surface_role_t;

typedef struct {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    pxsys_surface_role_t role;
    uint32_t flags;
} pxsys_surface_config_t;

typedef struct {
    uint32_t struct_size;
    uint64_t transaction_id;
    pxsys_bytes_t commands;
} pxsys_ui_transaction_t;

typedef struct {
    uint32_t struct_size;
    pxsys_string_t renderer_id;
    pxsys_version_t version;
    uint64_t features;
    void* context;
    pxsys_status_t (*surface_create)(void* context, const pxsys_surface_config_t* config,
                                     pxsys_surface_ref_t* surface);
    pxsys_status_t (*surface_destroy)(void* context, pxsys_surface_ref_t surface);
    pxsys_status_t (*surface_set_visible)(void* context, pxsys_surface_ref_t surface, int visible);
    pxsys_status_t (*apply)(void* context, pxsys_surface_ref_t surface,
                            const pxsys_ui_transaction_t* transaction);
    /* After a provider binds successfully, every valid snapshot must be accepted. */
    pxsys_status_t (*theme_changed)(void* context, const pxsys_theme_snapshot_t* theme);
} pxsys_renderer_provider_t;

/* This SPI deliberately exposes no LVGL, Qt, SDL or platform object. A backend-specific
 * native surface is a separate, explicitly negotiated extension interface. */

#ifdef __cplusplus
}
#endif

#endif

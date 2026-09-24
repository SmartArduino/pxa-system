#ifndef PXSYS_THEME_H
#define PXSYS_THEME_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/status.h"
#include "pxsys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PXSYS_THEME_MODE_SYSTEM = 0,
    PXSYS_THEME_MODE_LIGHT,
    PXSYS_THEME_MODE_DARK,
    PXSYS_THEME_MODE_CUSTOM,
} pxsys_theme_mode_t;

#define PXSYS_THEME_ID_MAX_BYTES 63u

typedef enum {
    PXSYS_COLOR_SCHEME_LIGHT = 0,
    PXSYS_COLOR_SCHEME_DARK,
} pxsys_color_scheme_t;

typedef enum {
    PXSYS_CONTRAST_NORMAL = 0,
    PXSYS_CONTRAST_HIGH,
} pxsys_contrast_t;

typedef enum {
    PXSYS_COLOR_BACKGROUND = 0,
    PXSYS_COLOR_SURFACE,
    PXSYS_COLOR_TEXT_PRIMARY,
    PXSYS_COLOR_TEXT_SECONDARY,
    PXSYS_COLOR_BORDER,
    PXSYS_COLOR_ACCENT,
    PXSYS_COLOR_ON_ACCENT,
    PXSYS_COLOR_ERROR,
    PXSYS_COLOR_WARNING,
    PXSYS_COLOR_SUCCESS,
    PXSYS_COLOR_SCRIM,
    PXSYS_COLOR_ON_BACKGROUND,
    PXSYS_COLOR_SURFACE_VARIANT,
    PXSYS_COLOR_ON_SURFACE_VARIANT,
    PXSYS_COLOR_SURFACE_CONTAINER_LOWEST,
    PXSYS_COLOR_SURFACE_CONTAINER_LOW,
    PXSYS_COLOR_SURFACE_CONTAINER,
    PXSYS_COLOR_SURFACE_CONTAINER_HIGH,
    PXSYS_COLOR_SURFACE_CONTAINER_HIGHEST,
    PXSYS_COLOR_OUTLINE_VARIANT,
    PXSYS_COLOR_PRIMARY_CONTAINER,
    PXSYS_COLOR_ON_PRIMARY_CONTAINER,
    PXSYS_COLOR_SECONDARY,
    PXSYS_COLOR_ON_SECONDARY,
    PXSYS_COLOR_SECONDARY_CONTAINER,
    PXSYS_COLOR_ON_SECONDARY_CONTAINER,
    PXSYS_COLOR_TERTIARY,
    PXSYS_COLOR_ON_TERTIARY,
    PXSYS_COLOR_TERTIARY_CONTAINER,
    PXSYS_COLOR_ON_TERTIARY_CONTAINER,
    PXSYS_COLOR_ON_ERROR,
    PXSYS_COLOR_ERROR_CONTAINER,
    PXSYS_COLOR_ON_ERROR_CONTAINER,
    PXSYS_COLOR_INVERSE_SURFACE,
    PXSYS_COLOR_INVERSE_ON_SURFACE,
    PXSYS_COLOR_INVERSE_PRIMARY,
    PXSYS_COLOR_SURFACE_TINT,
    PXSYS_COLOR_TOKEN_COUNT,
} pxsys_color_token_t;

#define PXSYS_COLOR_PRIMARY PXSYS_COLOR_ACCENT
#define PXSYS_COLOR_ON_PRIMARY PXSYS_COLOR_ON_ACCENT
#define PXSYS_COLOR_ON_SURFACE PXSYS_COLOR_TEXT_PRIMARY
#define PXSYS_COLOR_OUTLINE PXSYS_COLOR_BORDER

typedef enum {
    PXSYS_THEME_PALETTE_BLUE = 0,
    PXSYS_THEME_PALETTE_TEAL,
    PXSYS_THEME_PALETTE_VIOLET,
    PXSYS_THEME_PALETTE_AMBER,
    PXSYS_THEME_PALETTE_CORAL,
    PXSYS_THEME_PALETTE_SAGE,
    PXSYS_THEME_PALETTE_ROSE,
    PXSYS_THEME_PALETTE_GRAPHITE,
    PXSYS_THEME_PALETTE_COUNT,
} pxsys_theme_palette_t;

/* Semantic text roles are backend-neutral. A renderer or product font
 * provider maps these requested pixel sizes to concrete font faces. */
typedef enum {
    PXSYS_TYPOGRAPHY_DISPLAY = 0,
    PXSYS_TYPOGRAPHY_HEADLINE,
    PXSYS_TYPOGRAPHY_TITLE,
    PXSYS_TYPOGRAPHY_BODY,
    PXSYS_TYPOGRAPHY_LABEL,
    PXSYS_TYPOGRAPHY_CAPTION,
    PXSYS_TYPOGRAPHY_ROLE_COUNT,
} pxsys_typography_role_t;

typedef struct {
    uint32_t struct_size;
    pxsys_theme_mode_t configured_mode;
    pxsys_color_scheme_t effective_scheme;
    pxsys_contrast_t contrast;
    uint64_t generation;
    /* Colors use non-premultiplied 0xAARRGGBB. */
    uint32_t colors[PXSYS_COLOR_TOKEN_COUNT];
    uint16_t base_font_px;
    uint16_t base_spacing_px;
    uint16_t base_radius_px;
    uint16_t motion_scale_per_mille;
    /* Stable external theme identity. Empty for built-in themes. */
    uint16_t theme_id_size;
    char theme_id[PXSYS_THEME_ID_MAX_BYTES + 1u];
    /* Requested rendered size for each semantic typography role. */
    uint16_t typography_px[PXSYS_TYPOGRAPHY_ROLE_COUNT];
} pxsys_theme_snapshot_t;

typedef void (*pxsys_theme_changed_fn)(void* context, const pxsys_theme_snapshot_t* snapshot);

typedef struct {
    uint32_t struct_size;
    size_t max_observers;
    pxsys_allocator_t allocator;
} pxsys_theme_service_config_t;

typedef struct pxsys_theme_service pxsys_theme_service_t;

void pxsys_theme_snapshot_init(pxsys_theme_snapshot_t* snapshot, pxsys_color_scheme_t scheme);
const char* pxsys_theme_palette_name(pxsys_theme_palette_t palette);
pxsys_status_t pxsys_theme_snapshot_apply_palette(pxsys_theme_snapshot_t* snapshot,
                                                  pxsys_theme_palette_t palette);
pxsys_status_t pxsys_theme_snapshot_init_custom(pxsys_theme_snapshot_t* snapshot,
                                                pxsys_string_t theme_id,
                                                pxsys_color_scheme_t base_scheme);
pxsys_status_t pxsys_theme_snapshot_validate(
    const pxsys_theme_snapshot_t* snapshot);
uint16_t pxsys_theme_typography_px(const pxsys_theme_snapshot_t* snapshot,
                                   pxsys_typography_role_t role);
void pxsys_theme_service_config_init(pxsys_theme_service_config_t* config);
pxsys_status_t pxsys_theme_service_create(const pxsys_theme_service_config_t* config,
                                          const pxsys_theme_snapshot_t* initial,
                                          pxsys_theme_service_t** output);
pxsys_status_t pxsys_theme_service_destroy(pxsys_theme_service_t* service);
pxsys_status_t pxsys_theme_service_update(pxsys_theme_service_t* service,
                                          const pxsys_theme_snapshot_t* snapshot);
pxsys_status_t pxsys_theme_service_get(const pxsys_theme_service_t* service,
                                       pxsys_theme_snapshot_t* snapshot);
pxsys_status_t pxsys_theme_service_subscribe(pxsys_theme_service_t* service, void* context,
                                             pxsys_theme_changed_fn callback);
pxsys_status_t pxsys_theme_service_unsubscribe(pxsys_theme_service_t* service, void* context,
                                               pxsys_theme_changed_fn callback);

#ifdef __cplusplus
}
#endif

#endif

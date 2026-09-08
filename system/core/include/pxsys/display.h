#ifndef PXSYS_DISPLAY_H
#define PXSYS_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "pxsys/status.h"
#include "pxsys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_DISPLAY_MAX_CUTOUTS 8u

typedef enum {
    PXSYS_DISPLAY_SHAPE_RECTANGLE = 0,
    PXSYS_DISPLAY_SHAPE_ROUNDED_RECTANGLE,
    PXSYS_DISPLAY_SHAPE_CIRCLE,
    PXSYS_DISPLAY_SHAPE_CUSTOM,
} pxsys_display_shape_t;

typedef enum {
    PXSYS_CUTOUT_RECTANGLE = 0,
    PXSYS_CUTOUT_ROUNDED_RECTANGLE,
    PXSYS_CUTOUT_ELLIPSE,
} pxsys_cutout_shape_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} pxsys_rect_t;

typedef struct {
    uint16_t top;
    uint16_t right;
    uint16_t bottom;
    uint16_t left;
} pxsys_insets_t;

typedef struct {
    uint16_t top_left;
    uint16_t top_right;
    uint16_t bottom_right;
    uint16_t bottom_left;
} pxsys_corner_radii_t;

typedef struct {
    uint32_t struct_size;
    pxsys_cutout_shape_t shape;
    pxsys_rect_t bounds;
    uint16_t radius;
    uint16_t reserved;
} pxsys_display_cutout_t;

typedef struct {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint16_t density_dpi;
    uint16_t refresh_rate_hz;
    pxsys_display_shape_t shape;
    pxsys_corner_radii_t corner_radii;
    pxsys_insets_t safe_insets;
    uint8_t cutout_count;
    uint8_t reserved[7];
    pxsys_display_cutout_t cutouts[PXSYS_DISPLAY_MAX_CUTOUTS];
    uint64_t generation;
} pxsys_display_profile_t;

typedef void (*pxsys_display_changed_fn)(void* context,
                                         const pxsys_display_profile_t* profile);

typedef struct {
    uint32_t struct_size;
    size_t max_observers;
    pxsys_allocator_t allocator;
} pxsys_display_service_config_t;

typedef struct pxsys_display_service pxsys_display_service_t;

void pxsys_display_profile_init(pxsys_display_profile_t* profile,
                                uint32_t width, uint32_t height);
pxsys_status_t pxsys_display_profile_validate(const pxsys_display_profile_t* profile);
pxsys_status_t pxsys_display_safe_rect(const pxsys_display_profile_t* profile,
                                       pxsys_rect_t* output);
int pxsys_display_contains_point(const pxsys_display_profile_t* profile,
                                 int32_t x, int32_t y);

void pxsys_display_service_config_init(pxsys_display_service_config_t* config);
pxsys_status_t pxsys_display_service_create(
    const pxsys_display_service_config_t* config,
    const pxsys_display_profile_t* initial,
    pxsys_display_service_t** output);
pxsys_status_t pxsys_display_service_destroy(pxsys_display_service_t* service);
pxsys_status_t pxsys_display_service_update(pxsys_display_service_t* service,
                                            const pxsys_display_profile_t* profile);
pxsys_status_t pxsys_display_service_get(const pxsys_display_service_t* service,
                                         pxsys_display_profile_t* profile);
pxsys_status_t pxsys_display_service_subscribe(pxsys_display_service_t* service,
                                               void* context,
                                               pxsys_display_changed_fn callback);
pxsys_status_t pxsys_display_service_unsubscribe(pxsys_display_service_t* service,
                                                 void* context,
                                                 pxsys_display_changed_fn callback);

#ifdef __cplusplus
}
#endif

#endif

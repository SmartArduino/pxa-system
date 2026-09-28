#include "pxsys/theme.h"

#include <string.h>

#define PXSYS_THEME_MAGIC UINT32_C(0x50585448)

typedef struct {
    void* context;
    pxsys_theme_changed_fn callback;
} theme_observer_t;

struct pxsys_theme_service {
    uint32_t magic;
    size_t observer_capacity;
    size_t observer_count;
    uint8_t notifying;
    pxsys_allocator_t allocator;
    pxsys_theme_snapshot_t current;
    theme_observer_t* observers;
};

static int service_valid(const pxsys_theme_service_t* service) {
    return service != NULL && service->magic == PXSYS_THEME_MAGIC;
}

pxsys_status_t pxsys_theme_snapshot_validate(
    const pxsys_theme_snapshot_t* snapshot) {
    size_t role;
    if (snapshot == NULL || snapshot->struct_size < sizeof(*snapshot) ||
        snapshot->configured_mode > PXSYS_THEME_MODE_CUSTOM ||
        snapshot->effective_scheme > PXSYS_COLOR_SCHEME_DARK ||
        snapshot->contrast > PXSYS_CONTRAST_HIGH || snapshot->base_font_px == 0 ||
        snapshot->base_spacing_px == 0 || snapshot->motion_scale_per_mille > 1000 ||
        snapshot->theme_id_size > PXSYS_THEME_ID_MAX_BYTES ||
        snapshot->theme_id[snapshot->theme_id_size] != '\0' ||
        (snapshot->configured_mode == PXSYS_THEME_MODE_CUSTOM &&
         snapshot->theme_id_size == 0))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    for (role = 0; role < PXSYS_TYPOGRAPHY_ROLE_COUNT; ++role) {
        if (snapshot->typography_px[role] == 0)
            return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    return PXSYS_STATUS_OK;
}

static int snapshot_valid(const pxsys_theme_snapshot_t* snapshot) {
    return pxsys_theme_snapshot_validate(snapshot) == PXSYS_STATUS_OK;
}

typedef struct {
    uint32_t primary, on_primary, primary_container, on_primary_container;
    uint32_t secondary, on_secondary, secondary_container, on_secondary_container;
    uint32_t tertiary, on_tertiary, tertiary_container, on_tertiary_container;
    uint32_t inverse_primary;
} palette_colors_t;

/* Blue #1769e0, teal #007f75, violet #7145b4 and amber #985b00:
 * @material/material-color-utilities 0.4.0 SchemeTonalSpot, contrast 0.
 * Precomputed so theme changes require no HCT calculation on the device. */
static const palette_colors_t palettes[2][PXSYS_THEME_PALETTE_COUNT] = {
    {
        {0xff465d91, 0xffffffff, 0xffd9e2ff, 0xff2d4678,
         0xff575e71, 0xffffffff, 0xffdbe2f9, 0xff3f4759,
         0xff725573, 0xffffffff, 0xfffcd7fb, 0xff583e5a, 0xffafc6ff},
        {0xff006a62, 0xffffffff, 0xff9df2e6, 0xff005049,
         0xff4a635f, 0xffffffff, 0xffcce8e3, 0xff324b48,
         0xff46617a, 0xffffffff, 0xffcde5ff, 0xff2e4961, 0xff81d5ca},
        {0xff6a548d, 0xffffffff, 0xffecdcff, 0xff513c73,
         0xff645a70, 0xffffffff, 0xffeadef7, 0xff4c4357,
         0xff7f525c, 0xffffffff, 0xffffd9e0, 0xff643b44, 0xffd5bbfc},
        {0xff845316, 0xffffffff, 0xffffdcbc, 0xff683d00,
         0xff725a42, 0xffffffff, 0xfffeddbe, 0xff58432c,
         0xff57633b, 0xffffffff, 0xffdae9b6, 0xff3f4b25, 0xfffbba73},
        {0xffb62500, 0xffffffff, 0xffffdad2, 0xff8b1a00,
         0xff7f543b, 0xffffffff, 0xffffdbc9, 0xff643d25,
         0xff815521, 0xffffffff, 0xffffdcbc, 0xff653e0a, 0xffffb4a3},
        {0xff36693e, 0xffffffff, 0xffb7f1ba, 0xff1d5128,
         0xff516351, 0xffffffff, 0xffd4e8d1, 0xff3a4b3a,
         0xff39656c, 0xffffffff, 0xffbdeaf3, 0xff1f4d54, 0xff9cd49f},
        {0xffb5007b, 0xffffffff, 0xffffd8e7, 0xff8a005d,
         0xff7f525d, 0xffffffff, 0xffffd9e0, 0xff643b45,
         0xff5e5a7d, 0xffffffff, 0xffe4dfff, 0xff1a1836, 0xffffafd4},
        {0xff575f6b, 0xffffffff, 0xffdbe3f1, 0xff3f4753,
         0xff5a5f66, 0xffffffff, 0xffdfe2eb, 0xff42474e,
         0xff535f70, 0xffffffff, 0xffd6e3f7, 0xff3b4858, 0xffbfc7d5},
    },
    {
        {0xffafc6ff, 0xff132f60, 0xff2d4678, 0xffd9e2ff,
         0xffbfc6dc, 0xff293042, 0xff3f4759, 0xffdbe2f9,
         0xffdfbbde, 0xff402743, 0xff583e5a, 0xfffcd7fb, 0xff465d91},
        {0xff81d5ca, 0xff003732, 0xff005049, 0xff9df2e6,
         0xffb1ccc7, 0xff1c3531, 0xff324b48, 0xffcce8e3,
         0xffaec9e6, 0xff163349, 0xff2e4961, 0xffcde5ff, 0xff006a62},
        {0xffd5bbfc, 0xff3a255b, 0xff513c73, 0xffecdcff,
         0xffcec2db, 0xff352d40, 0xff4c4357, 0xffeadef7,
         0xfff1b7c3, 0xff4b252e, 0xff643b44, 0xffffd9e0, 0xff6a548d},
        {0xfffbba73, 0xff492900, 0xff683d00, 0xffffdcbc,
         0xffe0c1a3, 0xff402d18, 0xff58432c, 0xfffeddbe,
         0xffbecc9b, 0xff293411, 0xff3f4b25, 0xffdae9b6, 0xff845316},
        {0xffffb4a3, 0xff630f00, 0xff8b1a00, 0xffffdad2,
         0xfff3ba9b, 0xff4a2811, 0xff643d25, 0xffffdbc9,
         0xfff6bb7d, 0xff492900, 0xff653e0a, 0xffffdcbc, 0xffb62500},
        {0xff9cd49f, 0xff003914, 0xff1d5128, 0xffb7f1ba,
         0xffb8ccb6, 0xff243425, 0xff3a4b3a, 0xffd4e8d1,
         0xffa1ced7, 0xff00363d, 0xff1f4d54, 0xffbdeaf3, 0xff36693e},
        {0xffffafd4, 0xff620041, 0xff8a005d, 0xffffd8e7,
         0xfff1b7c4, 0xff4a252f, 0xff643b45, 0xffffd9e0,
         0xffc7c2ea, 0xff2f2d4c, 0xff464364, 0xffe4dfff, 0xffb5007b},
        {0xffbfc7d5, 0xff29313c, 0xff3f4753, 0xffdbe3f1,
         0xffc3c7cf, 0xff2c3137, 0xff42474e, 0xffdfe2eb,
         0xffbbc7db, 0xff253140, 0xff3b4858, 0xffd6e3f7, 0xff575f6b},
    },
};

typedef struct {
    uint32_t surface, on_surface, on_surface_variant, outline, surface_variant;
    uint32_t lowest, low, container, high, highest, outline_variant;
    uint32_t inverse_surface, inverse_on_surface, background, muted;
} palette_neutrals_t;

static const palette_neutrals_t base_neutrals[2][PXSYS_THEME_PALETTE_CORAL] = {
    {
        {0xfffaf9ff, 0xff1a1b20, 0xff44464f, 0xff757780, 0xffe1e2ec,
         0xffffffff, 0xfff3f3fa, 0xffeeedf4, 0xffe8e7ef, 0xffe2e2e9,
         0xffc5c6d0, 0xff2f3036, 0xfff1f0f7, 0xfffaf9ff, 0xff44464f},
        {0xfff4fbf8, 0xff161d1c, 0xff3f4947, 0xff6f7977, 0xffdae5e2,
         0xffffffff, 0xffeff5f3, 0xffe9efed, 0xffe3eae7, 0xffdde4e2,
         0xffbec9c6, 0xff2b3230, 0xffecf2f0, 0xfff4fbf8, 0xff3f4947},
        {0xfffef7ff, 0xff1d1a20, 0xff49454e, 0xff7b757f, 0xffe8e0eb,
         0xffffffff, 0xfff9f1f9, 0xfff3ecf4, 0xffede6ee, 0xffe7e0e8,
         0xffcbc4cf, 0xff322f35, 0xfff6eef7, 0xfffef7ff, 0xff49454e},
        {0xfffff8f4, 0xff211a14, 0xff50453a, 0xff837568, 0xfff1dfd0,
         0xffffffff, 0xfffff1e7, 0xfffaebe0, 0xfff4e6da, 0xffeee0d5,
         0xffd5c3b5, 0xff372f28, 0xfffdeee3, 0xfffff8f4, 0xff50453a},
    },
    {
        {0xff121318, 0xffe2e2e9, 0xffc5c6d0, 0xff8f9099, 0xff44464f,
         0xff0c0e13, 0xff1a1b20, 0xff1e1f25, 0xff282a2f, 0xff33353a,
         0xff44464f, 0xffe2e2e9, 0xff2f3036, 0xff121318, 0xffc5c6d0},
        {0xff0e1514, 0xffdde4e2, 0xffbec9c6, 0xff899390, 0xff3f4947,
         0xff090f0e, 0xff161d1c, 0xff1a2120, 0xff252b2a, 0xff303635,
         0xff3f4947, 0xffdde4e2, 0xff2b3230, 0xff0e1514, 0xffbec9c6},
        {0xff151218, 0xffe7e0e8, 0xffcbc4cf, 0xff958e99, 0xff49454e,
         0xff0f0d12, 0xff1d1a20, 0xff211e24, 0xff2c292f, 0xff37333a,
         0xff49454e, 0xffe7e0e8, 0xff322f35, 0xff151218, 0xffcbc4cf},
        {0xff19120c, 0xffeee0d5, 0xffd5c3b5, 0xff9d8e81, 0xff50453a,
         0xff130d07, 0xff211a14, 0xff251e17, 0xff302921, 0xff3b332c,
         0xff50453a, 0xffeee0d5, 0xff372f28, 0xff19120c, 0xffd5c3b5},
    },
};

static const palette_neutrals_t neutrals[2][PXSYS_THEME_PALETTE_COUNT - PXSYS_THEME_PALETTE_CORAL] = {
    {
        {0xfffff8f6, 0xff271814, 0xff58413c, 0xff8c716b, 0xfffddbd4,
         0xffffffff, 0xfffff0ed, 0xffffe9e4, 0xffffe2dc, 0xfff9dcd6,
         0xffe0bfb8, 0xff3d2c28, 0xffffede9, 0xfffff8f6, 0xff58413c},
        {0xfff7fbf2, 0xff181d18, 0xff424940, 0xff727970, 0xffdde5d9,
         0xffffffff, 0xfff1f5ec, 0xffebefe7, 0xffe5e9e1, 0xffe0e4db,
         0xffc1c9be, 0xff2d322c, 0xffeef2e9, 0xfff7fbf2, 0xff424940},
        {0xfffff8f8, 0xff25181e, 0xff544249, 0xff87717a, 0xfff7dbe5,
         0xffffffff, 0xfffff0f4, 0xffffe8f0, 0xfffae2ea, 0xfff4dde4,
         0xffdac0c9, 0xff3b2c32, 0xffffecf2, 0xfffff8f8, 0xff544249},
        {0xfffbf9fa, 0xff1b1b1d, 0xff474748, 0xff777778, 0xffe4e2e3,
         0xffffffff, 0xfff5f3f4, 0xfff0edee, 0xffeae7e9, 0xffe4e2e3,
         0xffc8c6c7, 0xff303031, 0xfff2f0f1, 0xfffbf9fa, 0xff474748},
    },
    {
        {0xff1e100d, 0xfff9dcd6, 0xffe0bfb8, 0xffa78a84, 0xff58413c,
         0xff180b08, 0xff271814, 0xff2b1c18, 0xff372622, 0xff42312d,
         0xff58413c, 0xfff9dcd6, 0xff3d2c28, 0xff1e100d, 0xffe0bfb8},
        {0xff101510, 0xffe0e4db, 0xffc1c9be, 0xff8b9389, 0xff424940,
         0xff0b0f0b, 0xff181d18, 0xff1c211c, 0xff272b26, 0xff313630,
         0xff424940, 0xffe0e4db, 0xff2d322c, 0xff101510, 0xffc1c9be},
        {0xff1c1015, 0xfff4dde4, 0xffdac0c9, 0xffa28a94, 0xff544249,
         0xff160b10, 0xff25181e, 0xff291c22, 0xff34262c, 0xff3f3137,
         0xff544249, 0xfff4dde4, 0xff3b2c32, 0xff1c1015, 0xffdac0c9},
        {0xff131314, 0xffe4e2e3, 0xffc8c6c7, 0xff919092, 0xff474748,
         0xff0e0e0f, 0xff1b1b1d, 0xff1f1f21, 0xff2a2a2b, 0xff353536,
         0xff474748, 0xffe4e2e3, 0xff303031, 0xff131314, 0xffc8c6c7},
    },
};

static void apply_neutrals(pxsys_theme_snapshot_t* snapshot,
                           const palette_neutrals_t* neutral) {
    snapshot->colors[PXSYS_COLOR_BACKGROUND] = neutral->background;
    snapshot->colors[PXSYS_COLOR_ON_BACKGROUND] = neutral->on_surface;
    snapshot->colors[PXSYS_COLOR_SURFACE] = neutral->surface;
    snapshot->colors[PXSYS_COLOR_TEXT_PRIMARY] = neutral->on_surface;
    snapshot->colors[PXSYS_COLOR_TEXT_SECONDARY] = neutral->muted;
    snapshot->colors[PXSYS_COLOR_BORDER] = neutral->outline;
    snapshot->colors[PXSYS_COLOR_SURFACE_VARIANT] = neutral->surface_variant;
    snapshot->colors[PXSYS_COLOR_ON_SURFACE_VARIANT] = neutral->on_surface_variant;
    snapshot->colors[PXSYS_COLOR_SURFACE_CONTAINER_LOWEST] = neutral->lowest;
    snapshot->colors[PXSYS_COLOR_SURFACE_CONTAINER_LOW] = neutral->low;
    snapshot->colors[PXSYS_COLOR_SURFACE_CONTAINER] = neutral->container;
    snapshot->colors[PXSYS_COLOR_SURFACE_CONTAINER_HIGH] = neutral->high;
    snapshot->colors[PXSYS_COLOR_SURFACE_CONTAINER_HIGHEST] = neutral->highest;
    snapshot->colors[PXSYS_COLOR_OUTLINE_VARIANT] = neutral->outline_variant;
    snapshot->colors[PXSYS_COLOR_INVERSE_SURFACE] = neutral->inverse_surface;
    snapshot->colors[PXSYS_COLOR_INVERSE_ON_SURFACE] = neutral->inverse_on_surface;
}

const char* pxsys_theme_palette_name(pxsys_theme_palette_t palette) {
    static const char* const names[PXSYS_THEME_PALETTE_COUNT] = {
        "blue", "teal", "violet", "amber", "coral", "sage", "rose", "graphite",
    };
    return (unsigned)palette < PXSYS_THEME_PALETTE_COUNT ? names[palette] : NULL;
}

pxsys_status_t pxsys_theme_snapshot_apply_palette(pxsys_theme_snapshot_t* snapshot,
                                                  pxsys_theme_palette_t palette) {
    const palette_colors_t* colors;
    const palette_neutrals_t* neutral;
    const char* name = pxsys_theme_palette_name(palette);
    size_t name_size;
    if (!snapshot_valid(snapshot) || name == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    colors = &palettes[snapshot->effective_scheme][palette];
    snapshot->colors[PXSYS_COLOR_ACCENT] = colors->primary;
    snapshot->colors[PXSYS_COLOR_ON_ACCENT] = colors->on_primary;
    snapshot->colors[PXSYS_COLOR_PRIMARY_CONTAINER] = colors->primary_container;
    snapshot->colors[PXSYS_COLOR_ON_PRIMARY_CONTAINER] = colors->on_primary_container;
    snapshot->colors[PXSYS_COLOR_SECONDARY] = colors->secondary;
    snapshot->colors[PXSYS_COLOR_ON_SECONDARY] = colors->on_secondary;
    snapshot->colors[PXSYS_COLOR_SECONDARY_CONTAINER] = colors->secondary_container;
    snapshot->colors[PXSYS_COLOR_ON_SECONDARY_CONTAINER] = colors->on_secondary_container;
    snapshot->colors[PXSYS_COLOR_TERTIARY] = colors->tertiary;
    snapshot->colors[PXSYS_COLOR_ON_TERTIARY] = colors->on_tertiary;
    snapshot->colors[PXSYS_COLOR_TERTIARY_CONTAINER] = colors->tertiary_container;
    snapshot->colors[PXSYS_COLOR_ON_TERTIARY_CONTAINER] = colors->on_tertiary_container;
    snapshot->colors[PXSYS_COLOR_INVERSE_PRIMARY] = colors->inverse_primary;
    snapshot->colors[PXSYS_COLOR_SURFACE_TINT] = colors->primary;
    neutral = palette >= PXSYS_THEME_PALETTE_CORAL
                  ? &neutrals[snapshot->effective_scheme][palette - PXSYS_THEME_PALETTE_CORAL]
                  : &base_neutrals[snapshot->effective_scheme][palette];
    apply_neutrals(snapshot, neutral);
    if (palette == PXSYS_THEME_PALETTE_BLUE) {
        snapshot->configured_mode = snapshot->effective_scheme == PXSYS_COLOR_SCHEME_DARK
                                        ? PXSYS_THEME_MODE_DARK : PXSYS_THEME_MODE_LIGHT;
        snapshot->theme_id_size = 0;
        snapshot->theme_id[0] = '\0';
    } else {
        name_size = strlen(name);
        snapshot->configured_mode = PXSYS_THEME_MODE_CUSTOM;
        snapshot->theme_id_size = (uint16_t)name_size;
        memcpy(snapshot->theme_id, name, name_size + 1u);
    }
    return PXSYS_STATUS_OK;
}

void pxsys_theme_snapshot_init(pxsys_theme_snapshot_t* snapshot, pxsys_color_scheme_t scheme) {
    static const uint32_t light[PXSYS_COLOR_TOKEN_COUNT] = {
        UINT32_C(0xfff7f7f8), UINT32_C(0xffffffff), UINT32_C(0xff171719), UINT32_C(0xff626269),
        UINT32_C(0xffd8d8dc), UINT32_C(0xff1769e0), UINT32_C(0xffffffff), UINT32_C(0xffb42318),
        UINT32_C(0xffa15c00), UINT32_C(0xff18794e), UINT32_C(0x66000000),
    };
    static const uint32_t dark[PXSYS_COLOR_TOKEN_COUNT] = {
        UINT32_C(0xff111214), UINT32_C(0xff1c1d20), UINT32_C(0xfff1f1f2), UINT32_C(0xffaaaab0),
        UINT32_C(0xff3a3b40), UINT32_C(0xff69a7ff), UINT32_C(0xff0d203d), UINT32_C(0xffff7b72),
        UINT32_C(0xffffb454), UINT32_C(0xff5bd69a), UINT32_C(0x99000000),
    };
    if (snapshot == NULL)
        return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->struct_size = sizeof(*snapshot);
    snapshot->configured_mode = PXSYS_THEME_MODE_SYSTEM;
    snapshot->effective_scheme =
        scheme <= PXSYS_COLOR_SCHEME_DARK ? scheme : PXSYS_COLOR_SCHEME_LIGHT;
    snapshot->contrast = PXSYS_CONTRAST_NORMAL;
    memcpy(snapshot->colors, snapshot->effective_scheme == PXSYS_COLOR_SCHEME_DARK ? dark : light,
           sizeof(light));
    if (snapshot->effective_scheme == PXSYS_COLOR_SCHEME_DARK) {
        snapshot->colors[PXSYS_COLOR_ON_ERROR] = UINT32_C(0xff690005);
        snapshot->colors[PXSYS_COLOR_ERROR_CONTAINER] = UINT32_C(0xff93000a);
        snapshot->colors[PXSYS_COLOR_ON_ERROR_CONTAINER] = UINT32_C(0xffffdad6);
    } else {
        snapshot->colors[PXSYS_COLOR_ON_ERROR] = UINT32_C(0xffffffff);
        snapshot->colors[PXSYS_COLOR_ERROR_CONTAINER] = UINT32_C(0xffffdad6);
        snapshot->colors[PXSYS_COLOR_ON_ERROR_CONTAINER] = UINT32_C(0xff410002);
    }
    snapshot->base_font_px = 16;
    snapshot->base_spacing_px = 4;
    snapshot->base_radius_px = 4;
    snapshot->motion_scale_per_mille = 1000;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_DISPLAY] = 28;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_HEADLINE] = 24;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_TITLE] = 20;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_BODY] = 16;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_LABEL] = 14;
    snapshot->typography_px[PXSYS_TYPOGRAPHY_CAPTION] = 12;
    (void)pxsys_theme_snapshot_apply_palette(snapshot, PXSYS_THEME_PALETTE_BLUE);
    snapshot->configured_mode = PXSYS_THEME_MODE_SYSTEM;
}

uint16_t pxsys_theme_typography_px(const pxsys_theme_snapshot_t* snapshot,
                                   pxsys_typography_role_t role) {
    if (pxsys_theme_snapshot_validate(snapshot) != PXSYS_STATUS_OK ||
        role >= PXSYS_TYPOGRAPHY_ROLE_COUNT)
        return 0;
    return snapshot->typography_px[role];
}

pxsys_status_t pxsys_theme_snapshot_init_custom(pxsys_theme_snapshot_t* snapshot,
                                                pxsys_string_t theme_id,
                                                pxsys_color_scheme_t base_scheme) {
    if (snapshot == NULL || theme_id.data == NULL || theme_id.size == 0 ||
        theme_id.size > PXSYS_THEME_ID_MAX_BYTES ||
        base_scheme > PXSYS_COLOR_SCHEME_DARK) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    pxsys_theme_snapshot_init(snapshot, base_scheme);
    snapshot->configured_mode = PXSYS_THEME_MODE_CUSTOM;
    snapshot->theme_id_size = (uint16_t)theme_id.size;
    memcpy(snapshot->theme_id, theme_id.data, theme_id.size);
    snapshot->theme_id[theme_id.size] = '\0';
    return PXSYS_STATUS_OK;
}

void pxsys_theme_service_config_init(pxsys_theme_service_config_t* config) {
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_observers = 16;
    config->allocator.struct_size = sizeof(config->allocator);
}

pxsys_status_t pxsys_theme_service_create(const pxsys_theme_service_config_t* config,
                                          const pxsys_theme_snapshot_t* initial,
                                          pxsys_theme_service_t** output) {
    pxsys_theme_service_t* service;
    if (output == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (config == NULL || config->struct_size < sizeof(*config) || config->max_observers == 0 ||
        config->max_observers > SIZE_MAX / sizeof(theme_observer_t) ||
        config->allocator.struct_size < sizeof(config->allocator) ||
        config->allocator.allocate == NULL || config->allocator.release == NULL ||
        !snapshot_valid(initial)) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    service = (pxsys_theme_service_t*)config->allocator.allocate(config->allocator.context,
                                                                 sizeof(*service));
    if (service == NULL)
        return PXSYS_STATUS_NO_MEMORY;
    memset(service, 0, sizeof(*service));
    service->observers = (theme_observer_t*)config->allocator.allocate(
        config->allocator.context, config->max_observers * sizeof(*service->observers));
    if (service->observers == NULL) {
        config->allocator.release(config->allocator.context, service);
        return PXSYS_STATUS_NO_MEMORY;
    }
    memset(service->observers, 0, config->max_observers * sizeof(*service->observers));
    service->observer_capacity = config->max_observers;
    service->allocator = config->allocator;
    service->current = *initial;
    service->current.struct_size = sizeof(service->current);
    service->current.generation = 1;
    service->magic = PXSYS_THEME_MAGIC;
    *output = service;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_theme_service_destroy(pxsys_theme_service_t* service) {
    pxsys_allocator_t allocator;
    if (!service_valid(service))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying)
        return PXSYS_STATUS_BUSY;
    allocator = service->allocator;
    service->magic = 0;
    allocator.release(allocator.context, service->observers);
    allocator.release(allocator.context, service);
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_theme_service_update(pxsys_theme_service_t* service,
                                          const pxsys_theme_snapshot_t* snapshot) {
    size_t index;
    uint64_t generation;
    if (!service_valid(service) || !snapshot_valid(snapshot))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying)
        return PXSYS_STATUS_BUSY;
    generation = service->current.generation == UINT64_MAX ? 1 : service->current.generation + 1u;
    service->current = *snapshot;
    service->current.struct_size = sizeof(service->current);
    service->current.generation = generation;
    service->notifying = 1;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback != NULL) {
            service->observers[index].callback(service->observers[index].context,
                                               &service->current);
        }
    }
    service->notifying = 0;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_theme_service_get(const pxsys_theme_service_t* service,
                                       pxsys_theme_snapshot_t* snapshot) {
    if (!service_valid(service) || snapshot == NULL || snapshot->struct_size < sizeof(*snapshot))
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *snapshot = service->current;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_theme_service_subscribe(pxsys_theme_service_t* service, void* context,
                                             pxsys_theme_changed_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying)
        return PXSYS_STATUS_BUSY;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == callback &&
            service->observers[index].context == context) {
            return PXSYS_STATUS_ALREADY_EXISTS;
        }
    }
    if (service->observer_count == service->observer_capacity)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == NULL)
            break;
    }
    service->observers[index].context = context;
    service->observers[index].callback = callback;
    service->observer_count++;
    service->notifying = 1;
    callback(context, &service->current);
    service->notifying = 0;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_theme_service_unsubscribe(pxsys_theme_service_t* service, void* context,
                                               pxsys_theme_changed_fn callback) {
    size_t index;
    if (!service_valid(service) || callback == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    if (service->notifying)
        return PXSYS_STATUS_BUSY;
    for (index = 0; index < service->observer_capacity; ++index) {
        if (service->observers[index].callback == callback &&
            service->observers[index].context == context) {
            memset(&service->observers[index], 0, sizeof(service->observers[index]));
            service->observer_count--;
            return PXSYS_STATUS_OK;
        }
    }
    return PXSYS_STATUS_NOT_FOUND;
}

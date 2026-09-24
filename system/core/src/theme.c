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

static const palette_colors_t palettes[2][PXSYS_THEME_PALETTE_COUNT] = {
    {
        {0xff1769e0, 0xffffffff, 0xffd9e3ff, 0xff001b3f,
         0xff505f7a, 0xffffffff, 0xffd8e3ff, 0xff0a1c35,
         0xff725572, 0xffffffff, 0xfffbd7f9, 0xff2b122d, 0xffa9c7ff},
        {0xff007f75, 0xffffffff, 0xffa2f2e4, 0xff002b26,
         0xff47655f, 0xffffffff, 0xffcae9e0, 0xff03201b,
         0xff75567a, 0xffffffff, 0xfffdd6ff, 0xff2c1033, 0xff7ddbcc},
        {0xff7145b4, 0xffffffff, 0xffecdcff, 0xff280057,
         0xff645a70, 0xffffffff, 0xffeadcf7, 0xff21182a,
         0xff805156, 0xffffffff, 0xffffd9dc, 0xff321014, 0xffd4baff},
        {0xff985b00, 0xffffffff, 0xffffddb0, 0xff311b00,
         0xff735b40, 0xffffffff, 0xffffdcc0, 0xff2b1705,
         0xff665e23, 0xffffffff, 0xffece6a1, 0xff201c00, 0xffffc16b},
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
        {0xff69a7ff, 0xff0d203d, 0xff17427d, 0xffd9e3ff,
         0xffb9c7e4, 0xff22314a, 0xff394961, 0xffd8e3ff,
         0xffdfbade, 0xff402742, 0xff593e59, 0xfffbd7f9, 0xff1769e0},
        {0xff69d8c4, 0xff07372f, 0xff005047, 0xffa2f2e4,
         0xffb0ccc3, 0xff1b3630, 0xff324c46, 0xffcae9e0,
         0xffe4b8e7, 0xff432646, 0xff5b3d5f, 0xfffdd6ff, 0xff007f75},
        {0xffc6a7ff, 0xff30164e, 0xff543587, 0xffecdcff,
         0xffcebfdb, 0xff352e40, 0xff4c4558, 0xffeadcf7,
         0xfff4b7bc, 0xff4b2529, 0xff653b40, 0xffffd9dc, 0xff7145b4},
        {0xffffc16b, 0xff493000, 0xff724400, 0xffffddb0,
         0xffe1c2a3, 0xff412e1b, 0xff5b4430, 0xffffdcc0,
         0xffd0cb86, 0xff35310a, 0xff4d4820, 0xffece6a1, 0xff985b00},
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

static const palette_neutrals_t base_neutrals[2] = {
    {0xffffffff, 0xff171719, 0xff44464c, 0xffd8d8dc, 0xffe1e2eb,
     0xffffffff, 0xfff4f4f7, 0xffeeeef2, 0xffe8e8ec, 0xffe2e2e7,
     0xffc4c6d0, 0xff303034, 0xfff3f0f4, 0xfff7f7f8, 0xff626269},
    {0xff1c1d20, 0xfff1f1f2, 0xffc5c6ce, 0xff3a3b40, 0xff44464c,
     0xff0c0d10, 0xff191a1d, 0xff202125, 0xff2b2c30, 0xff36373c,
     0xff44464c, 0xffe2e2e6, 0xff2f3033, 0xff111214, 0xffaaaab0},
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
                  : &base_neutrals[snapshot->effective_scheme];
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

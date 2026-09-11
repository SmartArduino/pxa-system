#include "render.h"

#include <stddef.h>

#include "rc_math.h"

/* Large enough for a native (1X) render of the product's 296x240 view and of
 * a 320x240 view. Larger views fall back to 2X or coarser. */
#define SCENE_MAX_W RENDER_SCENE_MAX_W
#define SCENE_MAX_H RENDER_SCENE_MAX_H
#define TAN_HALF 0.70F
#define FOG_START 14.0F

#define RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r)&0xF8u) << 8) | (((uint16_t)(g)&0xFCu) << 3) | \
                ((uint16_t)(b) >> 3)))

#define COL_WHITE RGB565(245, 245, 245)
#define COL_BLACK RGB565(10, 10, 14)
#define COL_SHADOW RGB565(12, 16, 24)
#define COL_PANEL RGB565(30, 36, 48)
#define COL_PANEL_LIGHT RGB565(52, 62, 80)
#define COL_SELECT RGB565(250, 214, 70)
#define COL_TEXT RGB565(232, 240, 248)
#define COL_MUTED RGB565(160, 176, 192)
#define COL_GREEN RGB565(96, 200, 110)
#define COL_DANGER RGB565(226, 86, 86)

#define FOG_R 168
#define FOG_G 205
#define FOG_B 244

static uint16_t *g_scene;
static uint16_t g_depth[SCENE_MAX_W * SCENE_MAX_H];
static float g_ndc_x[SCENE_MAX_W];
static float g_ndc_y[SCENE_MAX_H];
static uint16_t g_ray_len_q12[SCENE_MAX_W * SCENE_MAX_H];
static uint16_t *g_row_ptr[SCREEN_H_MAX];
static int g_scene_w = 148;
static int g_scene_h = 120;
static int g_scale = 2;
static int g_max_steps = 80;
static float g_fog_end = 46.0F;
static float g_fog_inv = 1.0F / 32.0F;
/* Keep the direct Surface within the Guest's fixed RGB565 frame buffer. */
static int g_view_pixel_budget = 115000;
static render_perf_stats_t g_perf_stats;

render_layout_t g_layout = {
    SCREEN_W_DEFAULT, SCREEN_H_DEFAULT, 0, 0, SCREEN_W_DEFAULT,
    SCREEN_H_DEFAULT, 20, 208, 262, 216, 16, 262, 178, 16,
    262, 140, 16, 262, 104, 12, 276, 14, 11, 2, 2, 150, 18,
    248, 14, 11, 220, 14, 11,
};

menu_button_t g_menu_buttons[MENU_BUTTON_MAX];
int g_menu_button_count;

inventory_layout_t g_inv_layout;
static int g_inv_mode = 2;

int render_min_quality(void) {
    int scale = QUALITY_MIN;
    while (scale < QUALITY_MAX) {
        const int width = (g_layout.view_w + scale - 1) / scale;
        const int height = (g_layout.view_h + scale - 1) / scale;
        if (width <= SCENE_MAX_W && height <= SCENE_MAX_H) {
            break;
        }
        ++scale;
    }
    return scale;
}

static void render_update_scene(void) {
    const int view_w = g_layout.view_w;
    const int view_h = g_layout.view_h;
    int width = (view_w + g_scale - 1) / g_scale;
    int height = (view_h + g_scale - 1) / g_scale;
    /* Clamp the scene proportionally so a very wide or tall view keeps its
     * aspect instead of stretching. */
    if (width > SCENE_MAX_W) {
        height = height * SCENE_MAX_W / width;
        width = SCENE_MAX_W;
    }
    if (height > SCENE_MAX_H) {
        width = width * SCENE_MAX_H / height;
        height = SCENE_MAX_H;
    }
    if (width < 32) {
        width = 32;
    }
    if (height < 24) {
        height = 24;
    }
    g_scene_w = width;
    g_scene_h = height;
    /* Lower quality also shortens the view distance, which cuts the number of
     * DDA steps per ray on top of the lower ray count. */
    if (g_scale == QUALITY_PERFORMANCE) {
        g_max_steps = 48;
        g_fog_end = 30.0F;
    } else if (g_scale == QUALITY_BALANCED) {
        g_max_steps = 64;
        g_fog_end = 38.0F;
    } else {
        g_max_steps = 80;
        g_fog_end = 46.0F;
    }
    g_fog_inv = 1.0F / (g_fog_end - FOG_START);
    {
        const float inv_w = 1.0F / (float)g_scene_w;
        const float inv_h = 1.0F / (float)g_scene_h;
        const float tan_x = TAN_HALF * ((float)g_scene_w / (float)g_scene_h);
        int y;
        for (int x = 0; x < g_scene_w; ++x) {
            g_ndc_x[x] =
                (2.0F * ((float)x + 0.5F) * inv_w - 1.0F) * tan_x;
        }
        for (y = 0; y < g_scene_h; ++y) {
            const float ndc_y =
                (1.0F - 2.0F * ((float)y + 0.5F) * inv_h) * TAN_HALF;
            uint16_t *lengths = &g_ray_len_q12[y * g_scene_w];
            g_ndc_y[y] = ndc_y;
            for (int x = 0; x < g_scene_w; ++x) {
                const float length = rc_sqrt(
                    1.0F + g_ndc_x[x] * g_ndc_x[x] + ndc_y * ndc_y);
                lengths[x] = (uint16_t)(length * 4096.0F + 0.5F);
            }
        }
    }
}

void render_configure(int width, int height) {
    if (width < 120) {
        width = 120;
    }
    if (height < 90) {
        height = 90;
    }
    g_layout.screen_w = width;
    g_layout.screen_h = height;
    g_layout.view_w = width < SCREEN_W_MAX ? width : SCREEN_W_MAX;
    g_layout.view_h = height < SCREEN_H_MAX ? height : SCREEN_H_MAX;
    if (g_layout.view_w * g_layout.view_h > g_view_pixel_budget) {
        /* Scale both axes by the same factor to keep the aspect ratio. */
        int scaled_w = g_layout.view_w;
        int scaled_h = g_layout.view_h;
        int guard = 0;
        while (scaled_w * scaled_h > g_view_pixel_budget && guard < 64) {
            scaled_w = scaled_w * 63 / 64;
            scaled_h = scaled_h * 63 / 64;
            if (scaled_w < 160) {
                scaled_w = 160;
            }
            if (scaled_h < 120) {
                scaled_h = 120;
            }
            ++guard;
        }
        g_layout.view_w = scaled_w;
        g_layout.view_h = scaled_h;
    }
    g_layout.view_x = (width - g_layout.view_w) / 2;
    g_layout.view_y = (height - g_layout.view_h) / 2;
    /* The Host reserves system gesture strips at the top, bottom and left
     * edges (status bar pull-down, home gesture, back gesture). Interactive
     * controls stay clear of them so touches always reach the app. */
    g_layout.hotbar_x =
        g_layout.view_x + (g_layout.view_w - HOTBAR_SLOTS * HOTBAR_SLOT) / 2;
    g_layout.hotbar_y = g_layout.view_y + g_layout.view_h - 48;
    g_layout.jump_x = g_layout.view_x + g_layout.view_w - 34;
    g_layout.jump_y = g_layout.view_y + g_layout.view_h - 42;
    g_layout.jump_r = 16;
    g_layout.action_x = g_layout.jump_x;
    g_layout.action_y = g_layout.jump_y - 38;
    g_layout.action_r = 16;
    g_layout.place_x = g_layout.jump_x;
    g_layout.place_y = g_layout.jump_y - 76;
    g_layout.place_r = 16;
    g_layout.down_x = g_layout.jump_x;
    g_layout.down_y = g_layout.jump_y - 112;
    g_layout.down_r = 12;
    g_layout.fly_x = g_layout.view_x + g_layout.view_w - 20;
    g_layout.fly_y = g_layout.view_y + 28;
    g_layout.fly_r = 11;
    g_layout.quality_x = g_layout.view_x + 24;
    g_layout.quality_y = g_layout.view_y + 24;
    g_layout.quality_w = 150;
    g_layout.quality_h = 18;
    g_layout.bag_x = g_layout.view_x + g_layout.view_w - 48;
    g_layout.bag_y = g_layout.view_y + 28;
    g_layout.bag_r = 11;
    g_layout.menu_x = g_layout.view_x + g_layout.view_w - 76;
    g_layout.menu_y = g_layout.view_y + 28;
    g_layout.menu_r = 11;
    g_inv_layout.panel_x = g_layout.view_x + 6;
    g_inv_layout.panel_y = g_layout.view_y + 6;
    g_inv_layout.panel_w = g_layout.view_w - 12;
    g_inv_layout.panel_h = g_layout.view_h - 12;
    g_inv_layout.main_x =
        g_inv_layout.panel_x + (g_inv_layout.panel_w - 9 * HOTBAR_SLOT) / 2;
    g_inv_layout.main_y = g_inv_layout.panel_y + 96;
    g_inv_layout.inv_hotbar_x = g_inv_layout.main_x;
    g_inv_layout.inv_hotbar_y = g_inv_layout.panel_y + 176;
    g_inv_layout.craft_x = g_inv_layout.main_x + 30;
    g_inv_layout.craft_y = g_inv_layout.panel_y + 34;
    g_inv_layout.result_x =
        g_inv_layout.craft_x + g_inv_mode * HOTBAR_SLOT + 16;
    g_inv_layout.result_y = g_inv_layout.craft_y +
                            (g_inv_mode * HOTBAR_SLOT) / 2 -
                            HOTBAR_SLOT / 2;
    g_inv_layout.close_x = g_inv_layout.panel_x + g_inv_layout.panel_w - 18;
    g_inv_layout.close_y = g_inv_layout.panel_y + 32;
    g_inv_layout.close_r = 10;
    if (g_scale < render_min_quality()) {
        g_scale = render_min_quality();
    }
    render_update_scene();
}

void render_set_quality(int scale) {
    const int minimum = render_min_quality();
    if (scale < minimum) {
        scale = minimum;
    } else if (scale > QUALITY_BALANCED && scale < QUALITY_PERFORMANCE) {
        scale = QUALITY_PERFORMANCE;
    } else if (scale > QUALITY_MAX) {
        scale = QUALITY_MAX;
    }
    g_scale = scale;
    render_update_scene();
}

void render_shrink_view(void) {
    if (g_view_pixel_budget > 40000) {
        g_view_pixel_budget = g_view_pixel_budget * 3 / 4;
    }
    render_configure(g_layout.screen_w, g_layout.screen_h);
}

int render_quality(void) { return g_scale; }
int render_scene_width(void) { return g_scene_w; }
int render_scene_height(void) { return g_scene_h; }
void render_get_perf_stats(render_perf_stats_t *stats) {
    if (stats != NULL) *stats = g_perf_stats;
}

static const uint8_t kBaseR[BLOCK_TYPE_COUNT] = {
    0,   95,  134, 126, 218, 102, 58,  48,  157, 150,
    170, 105, 140, 236, 128, 58,  44,  70,  232, 58};
static const uint8_t kBaseG[BLOCK_TYPE_COUNT] = {
    0,   159, 96,  126, 207, 81,  143, 96,  128, 70,
    210, 105, 110, 240, 120, 140, 110, 150, 230, 58};
static const uint8_t kBaseB[BLOCK_TYPE_COUNT] = {
    0,   53,  67,  126, 160, 50,  70,  200, 79,  55,
    225, 105, 70,  246, 112, 62,  50,  60,  220, 58};

/* Mob body colours by kind: slime, sheep, pig, cow. */
static const uint8_t kMobR[MOB_KIND_COUNT] = {104, 232, 232, 110};
static const uint8_t kMobG[MOB_KIND_COUNT] = {188, 232, 150, 80};
static const uint8_t kMobB[MOB_KIND_COUNT] = {104, 225, 160, 60};

/* Face order is face * 2 + (dir positive ? 0 : 1):
 * x+, x-, y-(bottom), y+(top), z+, z-. */
static const uint8_t kFaceShade[6] = {200, 175, 115, 255, 165, 145};

typedef struct {
    float fx;
    float fy;
    float fz;
    float rx;
    float ry;
    float rz;
    float ux;
    float uy;
    float uz;
    float cam_x;
    float cam_y;
    float cam_z;
    float tan_x;
    float tan_y;
} camera_t;

static void camera_build(camera_t *cam, const player_t *player) {
    const float cy = rc_cos(player->yaw);
    const float sy = rc_sin(player->yaw);
    const float cp = rc_cos(player->pitch);
    const float sp = rc_sin(player->pitch);
    cam->fx = sy * cp;
    cam->fy = sp;
    cam->fz = cy * cp;
    cam->rx = cy;
    cam->ry = 0.0F;
    cam->rz = -sy;
    cam->ux = -sy * sp;
    cam->uy = cp;
    cam->uz = -cy * sp;
    cam->cam_x = player->x;
    cam->cam_y = player->y + EYE_HEIGHT;
    cam->cam_z = player->z;
    cam->tan_y = TAN_HALF;
    cam->tan_x = TAN_HALF * ((float)g_scene_w / (float)g_scene_h);
}

static int camera_project(const camera_t *cam, float x, float y, float z,
                          float *out_x, float *out_y, float *out_depth) {
    const float dx = x - cam->cam_x;
    const float dy = y - cam->cam_y;
    const float dz = z - cam->cam_z;
    const float depth = dx * cam->fx + dy * cam->fy + dz * cam->fz;
    const float right = dx * cam->rx + dy * cam->ry + dz * cam->rz;
    const float up = dx * cam->ux + dy * cam->uy + dz * cam->uz;
    if (depth < 0.06F) {
        return 0;
    }
    *out_x = (float)g_scene_w * 0.5F +
             (right / depth) * ((float)g_scene_w * 0.5F) / cam->tan_x;
    *out_y = (float)g_scene_h * 0.5F -
             (up / depth) * ((float)g_scene_h * 0.5F) / cam->tan_y;
    *out_depth = depth;
    return 1;
}

static inline uint32_t hash3(int x, int y, int z) {
    uint32_t value = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u +
                     (uint32_t)z * 2246822519u;
    value = (value ^ (value >> 13)) * 1274126177u;
    return value ^ (value >> 16);
}

uint16_t render_block_color(int block) {
    if ((unsigned)block >= (unsigned)BLOCK_TYPE_COUNT) {
        block = BLOCK_DIRT;
    }
    return RGB565(kBaseR[block], kBaseG[block], kBaseB[block]);
}

static inline uint16_t *frame_row(int y) { return g_row_ptr[y]; }

static uint16_t sky_pixel(float ndx, float ndy, float ndz) {
    int r;
    int g;
    int b;
    float sun;
    if (ndy >= 0.0F) {
        r = 150 - (int)(ndy * 95.0F);
        g = 200 - (int)(ndy * 75.0F);
        b = 252 - (int)(ndy * 30.0F);
    } else {
        const float below = -ndy;
        r = 150 + (int)(below * 30.0F);
        g = 200 - (int)(below * 25.0F);
        b = 240 - (int)(below * 30.0F);
    }
    sun = ndx * 0.35F + ndy * 0.55F + ndz * (-0.75F);
    if (sun > 0.988F) {
        r = 255;
        g = 252;
        b = 235;
    } else if (sun > 0.962F) {
        const int glow = (int)((sun - 0.962F) * 1538.0F);
        r += glow;
        g += glow;
        b += glow >> 1;
    }
    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }
    return RGB565(r, g, b);
}

/* --- block textures ------------------------------------------------------
 * A 16 x 16 RGB565 texture per block and face kind (top, side, bottom) is
 * generated once. The ray caster samples it per hit pixel, so the world uses
 * real tile textures instead of per-pixel colour noise. */

#define TEX_KIND_TOP 0
#define TEX_KIND_SIDE 1
#define TEX_KIND_BOTTOM 2

static uint16_t g_tex[BLOCK_TYPE_COUNT][3][256];
static uint8_t g_textures_ready;

static inline uint32_t tex_hash(int x, int y, int seed) {
    uint32_t value = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^
                     (uint32_t)seed * 83492791u;
    value = (value ^ (value >> 13)) * 1274126177u;
    return value ^ (value >> 16);
}

static void tex_pixel(int block, int kind, int x, int y, int r, int g, int b) {
    g_tex[block][kind][(y << 4) | x] = RGB565(r, g, b);
}

static void tex_fill(int block, int kind, int r, int g, int b, int amount,
                     int seed) {
    int x;
    int y;
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const int n =
                (int)(tex_hash(x, y, seed) % (uint32_t)(2 * amount + 1)) -
                amount;
            tex_pixel(block, kind, x, y, r + n, g + n, b + n);
        }
    }
}

static void build_textures(void) {
    int x;
    int y;
    /* Grass: green top, dirt bottom, dirt side with a jagged green fringe. */
    tex_fill(BLOCK_GRASS, TEX_KIND_TOP, 104, 168, 62, 16, 11);
    tex_fill(BLOCK_GRASS, TEX_KIND_BOTTOM, 134, 96, 67, 12, 12);
    tex_fill(BLOCK_GRASS, TEX_KIND_SIDE, 134, 96, 67, 12, 13);
    for (x = 0; x < 16; ++x) {
        const int fringe = 2 + (int)(tex_hash(x, 0, 14) % 3u);
        for (y = 0; y < fringe; ++y) {
            const int n = (int)(tex_hash(x, y, 15) % 25u) - 12;
            tex_pixel(BLOCK_GRASS, TEX_KIND_SIDE, x, y, 96 + n, 158 + n,
                      56 + n);
        }
    }
    /* Dirt: brown noise with darker specks. */
    tex_fill(BLOCK_DIRT, TEX_KIND_TOP, 134, 96, 67, 13, 21);
    tex_fill(BLOCK_DIRT, TEX_KIND_SIDE, 134, 96, 67, 13, 21);
    tex_fill(BLOCK_DIRT, TEX_KIND_BOTTOM, 134, 96, 67, 13, 21);
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            if ((tex_hash(x, y, 22) & 31u) == 0u) {
                tex_pixel(BLOCK_DIRT, TEX_KIND_SIDE, x, y, 106, 72, 48);
            }
        }
    }
    /* Stone: grey noise with a few dark cracks. */
    tex_fill(BLOCK_STONE, TEX_KIND_TOP, 128, 128, 128, 14, 31);
    tex_fill(BLOCK_STONE, TEX_KIND_SIDE, 128, 128, 128, 14, 31);
    tex_fill(BLOCK_STONE, TEX_KIND_BOTTOM, 128, 128, 128, 14, 31);
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const uint32_t h = tex_hash(x, y, 32);
            if ((h & 63u) == 0u) {
                tex_pixel(BLOCK_STONE, TEX_KIND_SIDE, x, y, 96, 96, 96);
            } else if ((h & 127u) == 1u) {
                tex_pixel(BLOCK_STONE, TEX_KIND_SIDE, x, y, 150, 150, 150);
            }
        }
    }
    /* Sand: pale noise with faint darker grains. */
    tex_fill(BLOCK_SAND, TEX_KIND_TOP, 218, 207, 160, 10, 41);
    tex_fill(BLOCK_SAND, TEX_KIND_SIDE, 214, 202, 154, 10, 42);
    tex_fill(BLOCK_SAND, TEX_KIND_BOTTOM, 210, 198, 150, 10, 43);
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            if ((tex_hash(x, y, 44) & 15u) == 0u) {
                tex_pixel(BLOCK_SAND, TEX_KIND_SIDE, x, y, 198, 184, 138);
            }
        }
    }
    /* Wood: bark stripes on the side, growth rings on the cut faces. */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const int stripe = ((x >> 1) & 1) != 0 ? -14 : 8;
            const int n = (int)(tex_hash(x, y, 51) % 17u) - 8;
            const int dx = x - 8;
            const int dy = y - 8;
            const int dist = (dx * dx + dy * dy) >> 2;
            const int ring = ((dist & 7) < 3) ? -16 : 8;
            const int m = (int)(tex_hash(x, y, 52) % 13u) - 6;
            tex_pixel(BLOCK_WOOD, TEX_KIND_SIDE, x, y, 108 + stripe + n,
                      86 + stripe + n, 56 + stripe + n);
            tex_pixel(BLOCK_WOOD, TEX_KIND_TOP, x, y, 164 + ring + m,
                      132 + ring + m, 80 + ring + m);
            tex_pixel(BLOCK_WOOD, TEX_KIND_BOTTOM, x, y, 164 + ring + m,
                      132 + ring + m, 80 + ring + m);
        }
    }
    /* Leaves: strong green noise with dark holes. */
    tex_fill(BLOCK_LEAVES, TEX_KIND_TOP, 58, 143, 70, 30, 61);
    tex_fill(BLOCK_LEAVES, TEX_KIND_SIDE, 54, 136, 66, 30, 62);
    tex_fill(BLOCK_LEAVES, TEX_KIND_BOTTOM, 50, 128, 62, 30, 63);
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            if ((tex_hash(x, y, 64) & 7u) == 0u) {
                tex_pixel(BLOCK_LEAVES, TEX_KIND_SIDE, x, y, 34, 92, 44);
            }
        }
    }
    /* Water: two overlapping ripple lattices with a few sparkles. The ray
     * caster blends two scrolled samples of this tile, so the surface
     * shimmers without an obvious sliding band. */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            int wa = (x + (y >> 1)) & 7;
            int wb = (y - (x >> 1)) & 7;
            const int n = (int)(tex_hash(x, y, 71) % 9u) - 4;
            int bright;
            wa = wa < 4 ? wa : 7 - wa;
            wb = wb < 4 ? wb : 7 - wb;
            bright = 4 + (wa + wb) * 5 + n;
            if ((tex_hash(x, y, 72) & 63u) == 0u) {
                bright += 24;
            }
            tex_pixel(BLOCK_WATER, TEX_KIND_TOP, x, y, 40 + bright / 2,
                      96 + bright, 200 + bright / 2);
            tex_pixel(BLOCK_WATER, TEX_KIND_SIDE, x, y, 30 + bright / 3,
                      80 + bright * 3 / 4, 174 + bright / 2);
            tex_pixel(BLOCK_WATER, TEX_KIND_BOTTOM, x, y, 22 + n, 60 + n,
                      148 + n);
        }
    }
    /* Planks: horizontal boards with seams and grain. */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const int seam = (y % 5) == 0 || (x % 8) == 0 ? -26 : 0;
            const int n = (int)(tex_hash(x, y, 81) % 19u) - 9;
            tex_pixel(BLOCK_PLANK, TEX_KIND_TOP, x, y, 158 + seam + n,
                      128 + seam + n, 80 + seam + n);
            tex_pixel(BLOCK_PLANK, TEX_KIND_SIDE, x, y, 158 + seam + n,
                      128 + seam + n, 80 + seam + n);
            tex_pixel(BLOCK_PLANK, TEX_KIND_BOTTOM, x, y, 158 + seam + n,
                      128 + seam + n, 80 + seam + n);
        }
    }
    /* Brick: offset courses with light mortar. */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const int course = y >> 2;
            const int shifted = (x + ((course & 1) != 0 ? 4 : 0)) & 15;
            const int mortar = (y & 3) == 0 || (shifted & 7) == 0;
            const int n = (int)(tex_hash(x, y, 91) % 17u) - 8;
            if (mortar) {
                tex_pixel(BLOCK_BRICK, TEX_KIND_SIDE, x, y, 172 + n,
                          168 + n, 160 + n);
            } else {
                tex_pixel(BLOCK_BRICK, TEX_KIND_SIDE, x, y, 152 + n, 72 + n,
                          56 + n);
            }
        }
    }
    tex_fill(BLOCK_BRICK, TEX_KIND_TOP, 152, 72, 56, 14, 92);
    tex_fill(BLOCK_BRICK, TEX_KIND_BOTTOM, 152, 72, 56, 14, 92);
    /* Glass: pale pane with a bright frame and a diagonal highlight. */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const int frame = x == 0 || y == 0 || x == 15 || y == 15;
            const int shine = (x + y) == 6 || (x + y) == 7;
            if (frame) {
                tex_pixel(BLOCK_GLASS, TEX_KIND_SIDE, x, y, 236, 248, 255);
            } else if (shine) {
                tex_pixel(BLOCK_GLASS, TEX_KIND_SIDE, x, y, 226, 244, 252);
            } else {
                tex_pixel(BLOCK_GLASS, TEX_KIND_SIDE, x, y, 176, 214, 230);
            }
            tex_pixel(BLOCK_GLASS, TEX_KIND_TOP, x, y, 190, 226, 238);
            tex_pixel(BLOCK_GLASS, TEX_KIND_BOTTOM, x, y, 170, 206, 222);
        }
    }
    /* Cobblestone: rounded stones over a dark base. */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const int cell = (int)(tex_hash(x >> 2, y >> 2, 101) & 3u);
            const int base = 96 + cell * 14;
            const int n = (int)(tex_hash(x, y, 102) % 21u) - 10;
            const int edge = (x & 3) == 0 || (y & 3) == 0 ? -22 : 0;
            const int v = base + n + edge;
            tex_pixel(BLOCK_COBBLE, TEX_KIND_SIDE, x, y, v, v, v);
            tex_pixel(BLOCK_COBBLE, TEX_KIND_TOP, x, y, base + n, base + n,
                      base + n);
            tex_pixel(BLOCK_COBBLE, TEX_KIND_BOTTOM, x, y, base + n,
                      base + n, base + n);
        }
    }
    /* Crafting table: planks with a 2x2 grid on top and a worn side. */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const int grain = (int)(tex_hash(x, y, 121) % 17u) - 8;
            const int grid = (x & 7) == 0 || (y & 7) == 0;
            const int base = grid ? 120 : 168;
            tex_pixel(BLOCK_TABLE, TEX_KIND_TOP, x, y, base + grain,
                      (base * 78 / 100) + grain, (base * 48 / 100) + grain);
            {
                const int band = y < 3 ? -28 : (y == 10 ? -34 : 0);
                const int b = 142 + band + grain;
                tex_pixel(BLOCK_TABLE, TEX_KIND_SIDE, x, y, b,
                          (b * 78 / 100), (b * 50 / 100));
            }
            tex_pixel(BLOCK_TABLE, TEX_KIND_BOTTOM, x, y, 140 + grain,
                      108 + grain, 68 + grain);
        }
    }
    /* Snow: bright with a faint blue cast. */
    tex_fill(BLOCK_SNOW, TEX_KIND_TOP, 236, 240, 246, 6, 131);
    tex_fill(BLOCK_SNOW, TEX_KIND_SIDE, 232, 238, 245, 6, 132);
    tex_fill(BLOCK_SNOW, TEX_KIND_BOTTOM, 224, 230, 238, 6, 133);
    /* Gravel: mixed grey pebbles with dark pits. */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const int n = (int)(tex_hash(x, y, 141) % 41u) - 20;
            const int base = 126 + n;
            if ((tex_hash(x, y, 142) & 31u) == 0u) {
                tex_pixel(BLOCK_GRAVEL, TEX_KIND_TOP, x, y, 88, 84, 80);
            } else {
                tex_pixel(BLOCK_GRAVEL, TEX_KIND_TOP, x, y, base,
                          base - 6, base - 14);
            }
            tex_pixel(BLOCK_GRAVEL, TEX_KIND_SIDE, x, y, base - 6,
                      base - 12, base - 20);
            tex_pixel(BLOCK_GRAVEL, TEX_KIND_BOTTOM, x, y, base - 12,
                      base - 18, base - 26);
        }
    }
    /* Cactus: green columns with ridges and pale spines. */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const int ridge = (x % 5) == 0 || (x % 5) == 3 ? -18 : 6;
            const int n = (int)(tex_hash(x, y, 151) % 13u) - 6;
            int r = 52 + ridge + n;
            int g = 132 + ridge + n;
            int b = 56 + ridge + n;
            if ((tex_hash(x, y, 152) & 63u) == 0u) {
                r = 214;
                g = 226;
                b = 176;
            }
            tex_pixel(BLOCK_CACTUS, TEX_KIND_SIDE, x, y, r, g, b);
            tex_pixel(BLOCK_CACTUS, TEX_KIND_TOP, x, y, 62 + n, 146 + n,
                      66 + n);
            tex_pixel(BLOCK_CACTUS, TEX_KIND_BOTTOM, x, y, 46 + n,
                      116 + n, 50 + n);
        }
    }
    /* Bush: dense dark foliage. */
    tex_fill(BLOCK_BUSH, TEX_KIND_TOP, 44, 110, 50, 26, 161);
    tex_fill(BLOCK_BUSH, TEX_KIND_SIDE, 38, 100, 44, 26, 162);
    tex_fill(BLOCK_BUSH, TEX_KIND_BOTTOM, 32, 90, 38, 26, 163);
    /* Flower: grass with red and yellow blossoms. */
    tex_fill(BLOCK_FLOWER, TEX_KIND_TOP, 70, 150, 60, 16, 171);
    tex_fill(BLOCK_FLOWER, TEX_KIND_SIDE, 66, 142, 58, 16, 172);
    tex_fill(BLOCK_FLOWER, TEX_KIND_BOTTOM, 60, 130, 54, 16, 173);
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const uint32_t h = tex_hash(x, y, 174);
            if ((h & 15u) == 0u) {
                const int red = (h & 16u) != 0u;
                tex_pixel(BLOCK_FLOWER, TEX_KIND_TOP, x, y,
                          red ? 226 : 240, red ? 66 : 214,
                          red ? 70 : 70);
                tex_pixel(BLOCK_FLOWER, TEX_KIND_SIDE, x, y,
                          red ? 226 : 240, red ? 66 : 214,
                          red ? 70 : 70);
            }
        }
    }
    /* Wool: soft off-white weave. */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const int weave = ((x + y) & 3) == 0 ? -10 : 4;
            const int n = (int)(tex_hash(x, y, 181) % 11u) - 5;
            tex_pixel(BLOCK_WOOL, TEX_KIND_TOP, x, y, 232 + weave + n,
                      230 + weave + n, 220 + weave + n);
            tex_pixel(BLOCK_WOOL, TEX_KIND_SIDE, x, y, 226 + weave + n,
                      224 + weave + n, 214 + weave + n);
            tex_pixel(BLOCK_WOOL, TEX_KIND_BOTTOM, x, y, 218 + weave + n,
                      216 + weave + n, 206 + weave + n);
        }
    }
    /* Bedrock: large dark blotches. */
    for (y = 0; y < 16; ++y) {
        for (x = 0; x < 16; ++x) {
            const int blotch =
                (int)(tex_hash(x >> 2, y >> 2, 111) % 41u) - 20;
            const int n = (int)(tex_hash(x, y, 112) % 17u) - 8;
            const int v = 58 + blotch + n;
            tex_pixel(BLOCK_BEDROCK, TEX_KIND_TOP, x, y, v, v, v);
            tex_pixel(BLOCK_BEDROCK, TEX_KIND_SIDE, x, y, v, v, v);
            tex_pixel(BLOCK_BEDROCK, TEX_KIND_BOTTOM, x, y, v, v, v);
        }
    }
    g_textures_ready = 1;
}

static void render_ensure_textures(void) {
    if (!g_textures_ready) {
        build_textures();
    }
}

static uint16_t shade_block(uint8_t block, int face, int sign, float uu,
                            float vv, float distance, uint32_t now_ms) {
    const int kind = face == 1 ? (sign < 0 ? TEX_KIND_TOP : TEX_KIND_BOTTOM)
                               : TEX_KIND_SIDE;
    const float frac_u = uu - rc_floor(uu);
    const float frac_v = vv - rc_floor(vv);
    int tx = (int)(frac_u * 16.0F) & 15;
    int ty = 15 - ((int)(frac_v * 16.0F) & 15);
    int r;
    int g;
    int b;
    int shade;
    if (block == BLOCK_WATER) {
        /* Blend two differently scrolled samples of the ripple tile. */
        const uint16_t *tile = g_tex[BLOCK_WATER][kind];
        const int shift_a = (int)(now_ms >> 7);
        const int shift_b = (int)(now_ms >> 8);
        const int ax = (tx + shift_a) & 15;
        const int by = (ty + shift_b) & 15;
        const uint16_t ca = tile[(ty << 4) | ax];
        const uint16_t cb = tile[(by << 4) | tx];
        r = (((ca >> 11) & 31) + ((cb >> 11) & 31)) >> 1;
        g = (((ca >> 5) & 63) + ((cb >> 5) & 63)) >> 1;
        b = ((ca & 31) + (cb & 31)) >> 1;
    } else {
        const uint16_t color = g_tex[block][kind][(ty << 4) | tx];
        r = (color >> 11) & 31;
        g = (color >> 5) & 63;
        b = color & 31;
    }
    shade = kFaceShade[face * 2 + (sign > 0 ? 0 : 1)];
    r = (r * shade) >> 8;
    g = (g * shade) >> 8;
    b = (b * shade) >> 8;
    {
        int fog = (int)((distance - FOG_START) * (256.0F * g_fog_inv));
        if (fog < 0) {
            fog = 0;
        } else if (fog > 256) {
            fog = 256;
        }
        if (fog != 0) {
            r = (r * (256 - fog) + (FOG_R >> 3) * fog) >> 8;
            g = (g * (256 - fog) + (FOG_G >> 2) * fog) >> 8;
            b = (b * (256 - fog) + (FOG_B >> 3) * fog) >> 8;
        }
    }
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void render_scene(const camera_t *cam, uint32_t now_ms) {
    const float fx = cam->fx;
    const float fy = cam->fy;
    const float fz = cam->fz;
    const float rx = cam->rx;
    const float rz = cam->rz;
    const float ux = cam->ux;
    const float uy = cam->uy;
    const float uz = cam->uz;
    const float cam_x = cam->cam_x;
    const float cam_y = cam->cam_y;
    const float cam_z = cam->cam_z;
    int py;
    g_perf_stats = (render_perf_stats_t){0};
    for (py = 0; py < g_scene_h; ++py) {
        const float ndc_y = g_ndc_y[py];
        const float dir_y = fy + uy * ndc_y;
        const uint16_t *ray_lengths = &g_ray_len_q12[py * g_scene_w];
        uint16_t *out = &g_scene[py * g_scene_w];
        uint16_t *depth_out = &g_depth[py * g_scene_w];
        int px;
        for (px = 0; px < g_scene_w; ++px) {
            const float ndc_x = g_ndc_x[px];
            const float dir_x = fx + rx * ndc_x + ux * ndc_y;
            const float dir_z = fz + rz * ndc_x + uz * ndc_y;
            const float ray_len = (float)ray_lengths[px] * (1.0F / 4096.0F);
            int map_x = rc_floor_int(cam_x);
            int map_y = rc_floor_int(cam_y);
            int map_z = rc_floor_int(cam_z);
            const int step_x = dir_x > 0.0F ? 1 : -1;
            const int step_y = dir_y > 0.0F ? 1 : -1;
            const int step_z = dir_z > 0.0F ? 1 : -1;
            const float delta_x =
                dir_x != 0.0F ? rc_fabs(1.0F / dir_x) : 1.0e30F;
            const float delta_y =
                dir_y != 0.0F ? rc_fabs(1.0F / dir_y) : 1.0e30F;
            const float delta_z =
                dir_z != 0.0F ? rc_fabs(1.0F / dir_z) : 1.0e30F;
            float side_x = dir_x > 0.0F
                               ? ((float)map_x + 1.0F - cam_x) * delta_x
                               : (cam_x - (float)map_x) * delta_x;
            float side_y = dir_y > 0.0F
                               ? ((float)map_y + 1.0F - cam_y) * delta_y
                               : (cam_y - (float)map_y) * delta_y;
            float side_z = dir_z > 0.0F
                               ? ((float)map_z + 1.0F - cam_z) * delta_z
                               : (cam_z - (float)map_z) * delta_z;
            uint8_t block = BLOCK_AIR;
            int face = 0;
            int sign = 1;
            float travel = 0.0F;
            int step;
            /* Rays stay inside one chunk for many steps; cache the chunk
             * pointer so the hot loop only pays one data load per step. */
            int cache_i = 0x7FFFFFFF;
            int cache_j = 0;
            chunk_t *chunk = g_chunk_grid[0][0];
            for (step = 0; step < g_max_steps; ++step) {
                if (side_x <= side_y && side_x <= side_z) {
                    map_x += step_x;
                    travel = side_x;
                    side_x += delta_x;
                    face = 0;
                    sign = step_x;
                } else if (side_y <= side_z) {
                    map_y += step_y;
                    travel = side_y;
                    side_y += delta_y;
                    face = 1;
                    sign = step_y;
                } else {
                    map_z += step_z;
                    travel = side_z;
                    side_z += delta_z;
                    face = 2;
                    sign = step_z;
                }
                if (travel > g_fog_end) {
                    block = BLOCK_AIR;
                    break;
                }
                {
                    const int ci = (map_x >> CHUNK_BITS) - g_chunk_origin_cx;
                    const int cj = (map_z >> CHUNK_BITS) - g_chunk_origin_cz;
                    if ((unsigned)map_y >= (unsigned)CHUNK_HEIGHT ||
                        (unsigned)ci >= (unsigned)GRID_W ||
                        (unsigned)cj >= (unsigned)GRID_W) {
                        break;
                    }
                    if (ci != cache_i || cj != cache_j) {
                        cache_i = ci;
                        cache_j = cj;
                        chunk = g_chunk_grid[cj][ci];
                    }
                    block = chunk->blocks[((map_y << 8) |
                                           ((map_z & CHUNK_MASK)
                                            << CHUNK_BITS) |
                                           (map_x & CHUNK_MASK))];
                }
                if (block != BLOCK_AIR) {
                    break;
                }
            }
            {
                const uint16_t steps = (uint16_t)(
                    step < g_max_steps ? step + 1 : g_max_steps);
                ++g_perf_stats.rays;
                g_perf_stats.total_steps += steps;
                if (steps > g_perf_stats.max_steps)
                    g_perf_stats.max_steps = steps;
            }
            if (block == BLOCK_AIR) {
                const float inv_len = 1.0F / ray_len;
                out[px] = sky_pixel(dir_x * inv_len, dir_y * inv_len,
                                    dir_z * inv_len);
                depth_out[px] = 0xFFFFu;
            } else {
                const float hit_x = cam_x + dir_x * travel;
                const float hit_y = cam_y + dir_y * travel;
                const float hit_z = cam_z + dir_z * travel;
                const float distance = travel * ray_len;
                float uu;
                float vv;
                int fixed;
                if (face == 0) {
                    uu = hit_z;
                    vv = hit_y;
                } else if (face == 1) {
                    uu = hit_x;
                    vv = hit_z;
                } else {
                    uu = hit_x;
                    vv = hit_y;
                }
                out[px] = shade_block(block, face, sign, uu, vv, distance,
                                      now_ms);
                fixed = (int)(distance * 16.0F);
                depth_out[px] =
                    fixed > 0xFFFE ? 0xFFFE : (uint16_t)fixed;
            }
        }
    }
}

static void render_entities(const camera_t *cam) {
    int index;
    for (index = 0; index < MAX_MOBS; ++index) {
        const mob_t *mob = &g_mobs[index];
        float min_x;
        float min_y;
        float min_z;
        float max_x;
        float max_y;
        float max_z;
        float corners[8][3];
        float bbox[4] = {1.0e9F, 1.0e9F, -1.0e9F, -1.0e9F};
        int visible = 0;
        int corner;
        int px0;
        int px1;
        int py0;
        int py1;
        int px;
        int py;
        float mob_near = 0.0F;
        if (!mob->alive) {
            continue;
        }
        min_x = mob->x - MOB_HALF;
        min_y = mob->y;
        min_z = mob->z - MOB_HALF;
        max_x = mob->x + MOB_HALF;
        max_y = mob->y + MOB_HEIGHT;
        max_z = mob->z + MOB_HALF;
        corners[0][0] = min_x;
        corners[0][1] = min_y;
        corners[0][2] = min_z;
        corners[1][0] = max_x;
        corners[1][1] = min_y;
        corners[1][2] = min_z;
        corners[2][0] = min_x;
        corners[2][1] = max_y;
        corners[2][2] = min_z;
        corners[3][0] = max_x;
        corners[3][1] = max_y;
        corners[3][2] = min_z;
        corners[4][0] = min_x;
        corners[4][1] = min_y;
        corners[4][2] = max_z;
        corners[5][0] = max_x;
        corners[5][1] = min_y;
        corners[5][2] = max_z;
        corners[6][0] = min_x;
        corners[6][1] = max_y;
        corners[6][2] = max_z;
        corners[7][0] = max_x;
        corners[7][1] = max_y;
        corners[7][2] = max_z;
        for (corner = 0; corner < 8; ++corner) {
            float sx;
            float sy;
            float depth;
            if (!camera_project(cam, corners[corner][0], corners[corner][1],
                                corners[corner][2], &sx, &sy, &depth)) {
                continue;
            }
            visible = 1;
            if (sx < bbox[0]) {
                bbox[0] = sx;
            }
            if (sy < bbox[1]) {
                bbox[1] = sy;
            }
            if (sx > bbox[2]) {
                bbox[2] = sx;
            }
            if (sy > bbox[3]) {
                bbox[3] = sy;
            }
        }
        if (!visible) {
            continue;
        }
        {
            const float dx = mob->x - cam->cam_x;
            const float dy = mob->y + MOB_HEIGHT * 0.5F - cam->cam_y;
            const float dz = mob->z - cam->cam_z;
            mob_near = rc_sqrt(dx * dx + dy * dy + dz * dz) - 0.9F;
            if (mob_near < 0.0F) {
                mob_near = 0.0F;
            }
        }
        px0 = (int)bbox[0];
        py0 = (int)bbox[1];
        px1 = (int)bbox[2] + 1;
        py1 = (int)bbox[3] + 1;
        if (px0 < 0) {
            px0 = 0;
        }
        if (py0 < 0) {
            py0 = 0;
        }
        if (px1 > g_scene_w - 1) {
            px1 = g_scene_w - 1;
        }
        if (py1 > g_scene_h - 1) {
            py1 = g_scene_h - 1;
        }
        for (py = py0; py <= py1; ++py) {
            for (px = px0; px <= px1; ++px) {
                if ((int)(mob_near * 16.0F) >=
                    (int)g_depth[py * g_scene_w + px]) {
                    continue;
                }
                const float ndc_x = g_ndc_x[px];
                const float ndc_y = g_ndc_y[py];
                const float dir_x = cam->fx + cam->rx * ndc_x +
                                    cam->ux * ndc_y;
                const float dir_y = cam->fy + cam->uy * ndc_y;
                const float dir_z = cam->fz + cam->rz * ndc_x +
                                    cam->uz * ndc_y;
                const float ray_len =
                    (float)g_ray_len_q12[py * g_scene_w + px] *
                    (1.0F / 4096.0F);
                float t_min = -1.0e30F;
                float t_max = 1.0e30F;
                int face = 0;
                int sign = 1;
                float distance;
                int ok = 1;
                if (dir_x != 0.0F) {
                    const float inv = 1.0F / dir_x;
                    float t1 = (min_x - cam->cam_x) * inv;
                    float t2 = (max_x - cam->cam_x) * inv;
                    if (t1 > t2) {
                        const float swap = t1;
                        t1 = t2;
                        t2 = swap;
                    }
                    if (t1 > t_min) {
                        t_min = t1;
                        face = 0;
                        sign = dir_x > 0.0F ? -1 : 1;
                    }
                    if (t2 < t_max) {
                        t_max = t2;
                    }
                } else if (cam->cam_x < min_x || cam->cam_x > max_x) {
                    ok = 0;
                }
                if (ok && dir_y != 0.0F) {
                    const float inv = 1.0F / dir_y;
                    float t1 = (min_y - cam->cam_y) * inv;
                    float t2 = (max_y - cam->cam_y) * inv;
                    if (t1 > t2) {
                        const float swap = t1;
                        t1 = t2;
                        t2 = swap;
                    }
                    if (t1 > t_min) {
                        t_min = t1;
                        face = 1;
                        sign = dir_y > 0.0F ? -1 : 1;
                    }
                    if (t2 < t_max) {
                        t_max = t2;
                    }
                } else if (ok &&
                           (cam->cam_y < min_y || cam->cam_y > max_y)) {
                    ok = 0;
                }
                if (ok && dir_z != 0.0F) {
                    const float inv = 1.0F / dir_z;
                    float t1 = (min_z - cam->cam_z) * inv;
                    float t2 = (max_z - cam->cam_z) * inv;
                    if (t1 > t2) {
                        const float swap = t1;
                        t1 = t2;
                        t2 = swap;
                    }
                    if (t1 > t_min) {
                        t_min = t1;
                        face = 2;
                        sign = dir_z > 0.0F ? -1 : 1;
                    }
                    if (t2 < t_max) {
                        t_max = t2;
                    }
                } else if (ok &&
                           (cam->cam_z < min_z || cam->cam_z > max_z)) {
                    ok = 0;
                }
                if (!ok || t_max < t_min || t_max < 0.0F) {
                    continue;
                }
                if (t_min < 0.0F) {
                    t_min = 0.0F;
                }
                distance = t_min * ray_len;
                if ((int)(distance * 16.0F) >=
                    (int)g_depth[py * g_scene_w + px]) {
                    continue;
                }
                {
                    const int shade =
                        kFaceShade[face * 2 + (sign > 0 ? 0 : 1)];
                    const int kind = mob->kind < MOB_KIND_COUNT
                                         ? mob->kind
                                         : MOB_SLIME;
                    int r = (kMobR[kind] * shade) >> 8;
                    int g = (kMobG[kind] * shade) >> 8;
                    int b = (kMobB[kind] * shade) >> 8;
                    if (mob->hurt > 0.0F) {
                        r = 255;
                        g = (g * 2) / 5;
                        b = (b * 2) / 5;
                    }
                    if (face != 1) {
                        const float hx = cam->cam_x + dir_x * t_min;
                        const float hy = cam->cam_y + dir_y * t_min;
                        const float hz = cam->cam_z + dir_z * t_min;
                        const float hu = face == 0 ? hz : hx;
                        const float hv = hy;
                        const float fu = hu - rc_floor(hu);
                        const float fv = hv - rc_floor(hv);
                        if (fv > 0.5F && fv < 0.85F &&
                            ((fu > 0.18F && fu < 0.38F) ||
                             (fu > 0.62F && fu < 0.82F))) {
                            r = 28;
                            g = 40;
                            b = 32;
                        }
                    }
                    g_scene[py * g_scene_w + px] = RGB565(r, g, b);
                }
            }
        }
    }

    for (index = 0; index < MAX_PARTICLES; ++index) {
        const particle_t *particle = &g_particles[index];
        float sx;
        float sy;
        float depth;
        int px;
        int py;
        int size;
        uint16_t color;
        if (particle->life <= 0.0F) {
            continue;
        }
        if (!camera_project(cam, particle->x, particle->y, particle->z, &sx,
                            &sy, &depth)) {
            continue;
        }
        px = (int)sx;
        py = (int)sy;
        if ((unsigned)px >= (unsigned)g_scene_w ||
            (unsigned)py >= (unsigned)g_scene_h) {
            continue;
        }
        if ((int)(depth * 16.0F) >= (int)g_depth[py * g_scene_w + px]) {
            continue;
        }
        {
            const int block = particle->block;
            const int bright =
                particle->life > 0.6F ? 255 : (particle->life > 0.3F ? 215 : 170);
            color = RGB565((kBaseR[block] * bright) >> 8,
                           (kBaseG[block] * bright) >> 8,
                           (kBaseB[block] * bright) >> 8);
        }
        size = particle->life > 0.3F ? 1 : 0;
        g_scene[py * g_scene_w + px] = color;
        if (size == 1) {
            if (px + 1 < g_scene_w) {
                g_scene[py * g_scene_w + px + 1] = color;
            }
            if (py + 1 < g_scene_h) {
                g_scene[(py + 1) * g_scene_w + px] = color;
                if (px + 1 < g_scene_w) {
                    g_scene[(py + 1) * g_scene_w + px + 1] = color;
                }
            }
        }
    }
}

static void darken_pixel(int x, int y) {
    if ((unsigned)x >= (unsigned)g_scene_w ||
        (unsigned)y >= (unsigned)g_scene_h) {
        return;
    }
    g_scene[y * g_scene_w + x] =
        (uint16_t)((g_scene[y * g_scene_w + x] >> 1) & 0x7BEFu);
}

static void darken_line(int x0, int y0, int x1, int y1) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps;
    int index;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    steps = dx > dy ? dx : dy;
    if (steps == 0) {
        darken_pixel(x0, y0);
        return;
    }
    for (index = 0; index <= steps; ++index) {
        darken_pixel(x0 + (x1 - x0) * index / steps,
                     y0 + (y1 - y0) * index / steps);
    }
}

static void render_highlight(const camera_t *cam, const ray_hit_t *target,
                             float progress) {
    float corners[8][3];
    float screen[8][2];
    int visible[8];
    int index;
    static const uint8_t edges[12][2] = {
        {0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
        {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    if (target == NULL || !target->hit) {
        return;
    }
    corners[0][0] = (float)target->x;
    corners[0][1] = (float)target->y;
    corners[0][2] = (float)target->z;
    corners[1][0] = (float)target->x + 1.0F;
    corners[1][1] = (float)target->y;
    corners[1][2] = (float)target->z;
    corners[2][0] = (float)target->x;
    corners[2][1] = (float)target->y + 1.0F;
    corners[2][2] = (float)target->z;
    corners[3][0] = (float)target->x + 1.0F;
    corners[3][1] = (float)target->y + 1.0F;
    corners[3][2] = (float)target->z;
    corners[4][0] = (float)target->x;
    corners[4][1] = (float)target->y;
    corners[4][2] = (float)target->z + 1.0F;
    corners[5][0] = (float)target->x + 1.0F;
    corners[5][1] = (float)target->y;
    corners[5][2] = (float)target->z + 1.0F;
    corners[6][0] = (float)target->x;
    corners[6][1] = (float)target->y + 1.0F;
    corners[6][2] = (float)target->z + 1.0F;
    corners[7][0] = (float)target->x + 1.0F;
    corners[7][1] = (float)target->y + 1.0F;
    corners[7][2] = (float)target->z + 1.0F;
    for (index = 0; index < 8; ++index) {
        float sx;
        float sy;
        float depth;
        visible[index] = camera_project(cam, corners[index][0],
                                        corners[index][1],
                                        corners[index][2], &sx, &sy, &depth);
        screen[index][0] = sx;
        screen[index][1] = sy;
    }
    for (index = 0; index < 12; ++index) {
        const int a = edges[index][0];
        const int b = edges[index][1];
        if (visible[a] && visible[b]) {
            darken_line((int)screen[a][0], (int)screen[a][1],
                        (int)screen[b][0], (int)screen[b][1]);
        }
    }
    if (progress > 0.01F) {
        const int stage = 1 + (int)(progress * 9.0F);
        int crack;
        for (crack = 0; crack < stage; ++crack) {
            const uint32_t seed =
                hash3(target->x, target->y, target->z + crack);
            const float f = (float)((seed >> 4) & 15u) / 16.0F;
            const float g = (float)((seed >> 12) & 15u) / 16.0F;
            float a[3];
            float b[3];
            float sa[2];
            float sb[2];
            float depth;
            int i;
            for (i = 0; i < 3; ++i) {
                a[i] = (float)target->x;
                b[i] = (float)target->x + 1.0F;
            }
            if (target->face == 0) {
                const float plane = (target->x - target->place_x) > 0
                                        ? (float)target->x
                                        : (float)target->x + 1.0F;
                a[0] = plane;
                b[0] = plane;
                a[1] = (float)target->y + f;
                b[1] = (float)target->y + g;
                a[2] = (float)target->z + g;
                b[2] = (float)target->z + f;
            } else if (target->face == 1) {
                const float plane = (target->y - target->place_y) > 0
                                        ? (float)target->y
                                        : (float)target->y + 1.0F;
                a[1] = plane;
                b[1] = plane;
                a[0] = (float)target->x + f;
                b[0] = (float)target->x + g;
                a[2] = (float)target->z + g;
                b[2] = (float)target->z + f;
            } else {
                const float plane = (target->z - target->place_z) > 0
                                        ? (float)target->z
                                        : (float)target->z + 1.0F;
                a[2] = plane;
                b[2] = plane;
                a[0] = (float)target->x + f;
                b[0] = (float)target->x + g;
                a[1] = (float)target->y + g;
                b[1] = (float)target->y + f;
            }
            if (camera_project(cam, a[0], a[1], a[2], &sa[0], &sa[1],
                               &depth) &&
                camera_project(cam, b[0], b[1], b[2], &sb[0], &sb[1],
                               &depth)) {
                darken_line((int)sa[0], (int)sa[1], (int)sb[0], (int)sb[1]);
            }
        }
    }
}

void render_target(uint16_t *pixels, uint32_t stride_pixels) {
    int row;
    g_scene = pixels;
    for (row = 0; row < g_scene_h; ++row) {
        g_row_ptr[row] = pixels + (size_t)row * stride_pixels;
    }
}

int render_3d(uint16_t *pixels, uint32_t stride_pixels,
              const player_t *player, uint32_t now_ms,
              const ray_hit_t *target, float mine_progress) {
    camera_t cam;
    if (pixels == NULL || stride_pixels < (uint32_t)g_scene_w) {
        return 0;
    }
    render_ensure_textures();
    render_target(pixels, stride_pixels);
    camera_build(&cam, player);
    render_scene(&cam, now_ms);
    render_entities(&cam);
    render_highlight(&cam, target, mine_progress);
    return 1;
}

/* --- HUD primitives ----------------------------------------------------- */

static void hud_pixel(int x, int y, uint16_t color) {
    int target_x;
    int target_y;
    if (x < g_layout.view_x || x >= g_layout.view_x + g_layout.view_w ||
        y < g_layout.view_y || y >= g_layout.view_y + g_layout.view_h) {
        return;
    }
    target_x = (x - g_layout.view_x) * g_scene_w / g_layout.view_w;
    target_y = (y - g_layout.view_y) * g_scene_h / g_layout.view_h;
    frame_row(target_y)[target_x] = color;
}

static void hud_rect(int x, int y, int width, int height, uint16_t color) {
    int row;
    for (row = y; row < y + height; ++row) {
        int column;
        if (row < g_layout.view_y ||
            row >= g_layout.view_y + g_layout.view_h) {
            continue;
        }
        for (column = x; column < x + width; ++column) {
            hud_pixel(column, row, color);
        }
    }
}

static void hud_rect_outline(int x, int y, int width, int height, int thickness,
                             uint16_t color) {
    hud_rect(x, y, width, thickness, color);
    hud_rect(x, y + height - thickness, width, thickness, color);
    hud_rect(x, y, thickness, height, color);
    hud_rect(x + width - thickness, y, thickness, height, color);
}

static void hud_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps;
    int index;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    steps = dx > dy ? dx : dy;
    if (steps == 0) {
        hud_pixel(x0, y0, color);
        return;
    }
    for (index = 0; index <= steps; ++index) {
        const int x = x0 + (x1 - x0) * index / steps;
        const int y = y0 + (y1 - y0) * index / steps;
        hud_pixel(x, y, color);
    }
}

static void hud_circle(int cx, int cy, int radius, uint16_t color) {
    int dy;
    for (dy = -radius; dy <= radius; ++dy) {
        int dx;
        for (dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) {
                hud_pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

static void hud_circle_outline(int cx, int cy, int radius, uint16_t color) {
    int dy;
    for (dy = -radius; dy <= radius; ++dy) {
        int dx;
        for (dx = -radius; dx <= radius; ++dx) {
            const int d = dx * dx + dy * dy;
            if (d <= radius * radius && d >= (radius - 2) * (radius - 2)) {
                hud_pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

typedef struct {
    char ch;
    uint8_t rows[5];
} glyph_t;

static const glyph_t kGlyphs[] = {
    {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}}, {'2', {7, 1, 7, 4, 7}},
    {'3', {7, 1, 7, 1, 7}}, {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 7, 1, 7}},
    {'6', {7, 4, 7, 5, 7}}, {'7', {7, 1, 1, 1, 1}}, {'8', {7, 5, 7, 5, 7}},
    {'9', {7, 5, 7, 1, 7}}, {'.', {0, 0, 0, 0, 2}}, {':', {0, 2, 0, 2, 0}},
    {'-', {0, 0, 7, 0, 0}}, {'/', {1, 1, 2, 4, 4}}, {'+', {0, 2, 7, 2, 0}},
    {'A', {7, 5, 7, 5, 5}}, {'B', {6, 5, 6, 5, 6}}, {'C', {7, 4, 4, 4, 7}},
    {'D', {6, 5, 5, 5, 6}}, {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}},
    {'G', {7, 4, 5, 5, 7}}, {'H', {5, 5, 7, 5, 5}}, {'I', {7, 2, 2, 2, 7}},
    {'J', {1, 1, 1, 5, 7}}, {'K', {5, 5, 6, 5, 5}}, {'L', {4, 4, 4, 4, 7}},
    {'M', {5, 7, 7, 5, 5}}, {'N', {5, 7, 7, 7, 5}}, {'O', {7, 5, 5, 5, 7}},
    {'P', {7, 5, 7, 4, 4}}, {'Q', {7, 5, 5, 7, 1}}, {'R', {6, 5, 6, 5, 5}},
    {'S', {7, 4, 7, 1, 7}}, {'T', {7, 2, 2, 2, 2}}, {'U', {5, 5, 5, 5, 7}},
    {'V', {5, 5, 5, 5, 2}}, {'W', {5, 5, 7, 7, 5}}, {'X', {5, 5, 2, 5, 5}},
    {'Y', {5, 5, 2, 2, 2}}, {'Z', {7, 1, 2, 4, 7}}, {' ', {0, 0, 0, 0, 0}},
};

static const uint8_t *glyph_for(char ch) {
    unsigned index;
    for (index = 0; index < sizeof(kGlyphs) / sizeof(kGlyphs[0]); ++index) {
        if (kGlyphs[index].ch == ch) {
            return kGlyphs[index].rows;
        }
    }
    return kGlyphs[sizeof(kGlyphs) / sizeof(kGlyphs[0]) - 1].rows;
}

static int text_width(const char *text, int scale) {
    int length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length * 4 * scale;
}

static void hud_text(int x, int y, const char *text, uint16_t color,
                     int scale) {
    int cursor = x;
    while (*text != '\0') {
        const uint8_t *rows = glyph_for(*text++);
        int row;
        for (row = 0; row < 5; ++row) {
            int column;
            for (column = 0; column < 3; ++column) {
                if ((rows[row] & (4u >> column)) == 0u) {
                    continue;
                }
                if (scale == 1) {
                    hud_pixel(cursor + column, y + row, color);
                } else {
                    hud_rect(cursor + column * scale, y + row * scale, scale,
                             scale, color);
                }
            }
        }
        cursor += 4 * scale;
    }
}

static void hud_text_centered(int y, const char *text, uint16_t color,
                              int scale) {
    hud_text(g_layout.view_x +
                 (g_layout.view_w - text_width(text, scale)) / 2,
             y, text, color, scale);
}

static void draw_crosshair(float swing) {
    const int cx = g_layout.view_x + g_layout.view_w / 2;
    const int cy = g_layout.view_y + g_layout.view_h / 2;
    hud_line(cx - 8, cy, cx - 2, cy, COL_SHADOW);
    hud_line(cx + 2, cy, cx + 8, cy, COL_SHADOW);
    hud_line(cx, cy - 8, cx, cy - 2, COL_SHADOW);
    hud_line(cx, cy + 2, cx, cy + 8, COL_SHADOW);
    hud_line(cx - 7, cy, cx - 3, cy, COL_WHITE);
    hud_line(cx + 3, cy, cx + 7, cy, COL_WHITE);
    hud_line(cx, cy - 7, cx, cy - 3, COL_WHITE);
    hud_line(cx, cy + 3, cx, cy + 7, COL_WHITE);
    hud_pixel(cx, cy, COL_WHITE);
    if (swing > 0.0F) {
        hud_line(cx - 10, cy - 10, cx - 5, cy - 5, COL_DANGER);
        hud_line(cx + 5, cy - 5, cx + 10, cy - 10, COL_DANGER);
        hud_line(cx - 10, cy + 10, cx - 5, cy + 5, COL_DANGER);
        hud_line(cx + 5, cy + 5, cx + 10, cy + 10, COL_DANGER);
    }
}

static void draw_item_icon(int x, int y, int size, uint8_t item);
static void draw_count(int x, int y, uint8_t count);

static void draw_hotbar(const hud_state_t *hud) {
    const int icon = HOTBAR_SLOT - 4;
    int slot;
    for (slot = 0; slot < HOTBAR_SLOTS; ++slot) {
        const int x = g_layout.hotbar_x + slot * HOTBAR_SLOT;
        const int y = g_layout.hotbar_y;
        const item_stack_t *value = &g_inventory[slot];
        hud_rect(x, y, HOTBAR_SLOT, HOTBAR_SLOT, COL_PANEL);
        if (value->item != BLOCK_AIR) {
            draw_item_icon(x + 2, y + 2, icon, value->item);
            hud_rect(x + 2, y + 7, icon, 1, COL_SHADOW);
            draw_count(x + HOTBAR_SLOT - 2, y + HOTBAR_SLOT - 1,
                       value->count);
        }
        if (slot == hud->hotbar_selected) {
            hud_rect_outline(x, y - 2, HOTBAR_SLOT, HOTBAR_SLOT + 2, 2,
                             COL_SELECT);
        } else {
            hud_rect_outline(x, y, HOTBAR_SLOT, HOTBAR_SLOT, 1, COL_SHADOW);
        }
    }
}

static void draw_button(int cx, int cy, int radius, uint16_t fill,
                        uint8_t active) {
    hud_circle(cx, cy, radius, COL_SHADOW);
    hud_circle(cx, cy, radius - 1, active ? COL_GREEN : fill);
}

static void draw_arrow(int cx, int cy, int direction) {
    int index;
    for (index = 0; index < 6; ++index) {
        const int half = direction > 0 ? index : 5 - index;
        hud_line(cx - half, cy - 2 + index, cx + half, cy - 2 + index,
                 COL_WHITE);
    }
}

static void draw_pickaxe(int cx, int cy) {
    hud_line(cx - 5, cy + 6, cx + 2, cy - 2, COL_WHITE);
    hud_line(cx - 8, cy - 3, cx + 6, cy - 3, COL_WHITE);
    hud_line(cx - 8, cy - 3, cx - 6, cy - 6, COL_WHITE);
    hud_line(cx + 6, cy - 3, cx + 4, cy - 6, COL_WHITE);
}

static void draw_sword(int cx, int cy) {
    hud_line(cx - 4, cy + 6, cx + 4, cy - 4, COL_WHITE);
    hud_line(cx - 6, cy + 2, cx - 1, cy + 7, COL_WHITE);
    hud_pixel(cx + 5, cy - 5, COL_WHITE);
    hud_pixel(cx + 5, cy - 6, COL_WHITE);
}

/* --- inventory screen --------------------------------------------------- */

void render_inventory_set_mode(int table) {
    g_inv_mode = table ? 3 : 2;
    g_inv_layout.result_x =
        g_inv_layout.craft_x + g_inv_mode * HOTBAR_SLOT + 16;
    g_inv_layout.result_y = g_inv_layout.craft_y +
                            (g_inv_mode * HOTBAR_SLOT) / 2 -
                            HOTBAR_SLOT / 2;
}

int render_inventory_slot(int hit) {
    if (hit >= INV_HIT_CRAFT && hit < INV_HIT_RESULT) {
        return hit - INV_HIT_CRAFT;
    }
    if (hit >= INV_HIT_MAIN && hit < INV_HIT_MAIN + INV_MAIN_SLOTS) {
        return HOTBAR_SLOTS + (hit - INV_HIT_MAIN);
    }
    if (hit >= INV_HIT_HOTBAR && hit < INV_HIT_HOTBAR + HOTBAR_SLOTS) {
        return hit - INV_HIT_HOTBAR;
    }
    return -1;
}

int render_inventory_hit(int x, int y) {
    int column;
    int row;
    if (x >= g_inv_layout.close_x - g_inv_layout.close_r &&
        x < g_inv_layout.close_x + g_inv_layout.close_r &&
        y >= g_inv_layout.close_y - g_inv_layout.close_r &&
        y < g_inv_layout.close_y + g_inv_layout.close_r) {
        return INV_HIT_CLOSE;
    }
    for (row = 0; row < g_inv_mode; ++row) {
        for (column = 0; column < g_inv_mode; ++column) {
            const int sx = g_inv_layout.craft_x + column * HOTBAR_SLOT;
            const int sy = g_inv_layout.craft_y + row * HOTBAR_SLOT;
            if (x >= sx && x < sx + HOTBAR_SLOT && y >= sy &&
                y < sy + HOTBAR_SLOT) {
                return INV_HIT_CRAFT + row * g_inv_mode + column;
            }
        }
    }
    if (x >= g_inv_layout.result_x &&
        x < g_inv_layout.result_x + HOTBAR_SLOT &&
        y >= g_inv_layout.result_y &&
        y < g_inv_layout.result_y + HOTBAR_SLOT) {
        return INV_HIT_RESULT;
    }
    for (row = 0; row < 3; ++row) {
        for (column = 0; column < 9; ++column) {
            const int sx = g_inv_layout.main_x + column * HOTBAR_SLOT;
            const int sy = g_inv_layout.main_y + row * HOTBAR_SLOT;
            if (x >= sx && x < sx + HOTBAR_SLOT && y >= sy &&
                y < sy + HOTBAR_SLOT) {
                return INV_HIT_MAIN + row * 9 + column;
            }
        }
    }
    for (column = 0; column < 9; ++column) {
        const int sx = g_inv_layout.inv_hotbar_x + column * HOTBAR_SLOT;
        const int sy = g_inv_layout.inv_hotbar_y;
        if (x >= sx && x < sx + HOTBAR_SLOT && y >= sy &&
            y < sy + HOTBAR_SLOT) {
            return INV_HIT_HOTBAR + column;
        }
    }
    return INV_HIT_NONE;
}

static void dim_region(int x, int y, int width, int height) {
    int row;
    for (row = y; row < y + height; ++row) {
        int column;
        for (column = x; column < x + width; ++column) {
            if (x < 0 || row < g_layout.view_y ||
                row >= g_layout.view_y + g_layout.view_h ||
                column < g_layout.view_x ||
                column >= g_layout.view_x + g_layout.view_w) {
                continue;
            }
            if (((column ^ row) & 1) == 0 &&
                ((column - g_layout.view_x) * g_scene_w) %
                        g_layout.view_w ==
                    0 &&
                ((row - g_layout.view_y) * g_scene_h) % g_layout.view_h ==
                    0) {
                const int target_x =
                    (column - g_layout.view_x) * g_scene_w / g_layout.view_w;
                const int target_y =
                    (row - g_layout.view_y) * g_scene_h / g_layout.view_h;
                uint16_t *pixel = &frame_row(target_y)[target_x];
                *pixel = (uint16_t)((*pixel >> 1) & 0x7BEFu);
            }
        }
    }
}

static void draw_tool_icon(int x, int y, int size, uint8_t item) {
    const int wooden = item <= ITEM_WOOD_SWORD;
    const int kind = (int)(item - ITEM_FIRST_TOOL) % 4;
    const uint16_t handle = RGB565(118, 84, 48);
    const uint16_t handle_light = RGB565(146, 106, 62);
    const uint16_t head =
        wooden ? RGB565(174, 134, 84) : RGB565(158, 158, 158);
    const uint16_t head_light =
        wooden ? RGB565(198, 158, 104) : RGB565(188, 188, 188);
    const int u = size;
#define TOOL_X(a) (x + (u * (a)) / 20)
#define TOOL_Y(a) (y + (u * (a)) / 20)
    /* Handle: lower-left to the middle. */
    hud_line(TOOL_X(6), TOOL_Y(17), TOOL_X(12), TOOL_Y(8), handle);
    hud_line(TOOL_X(7), TOOL_Y(17), TOOL_X(13), TOOL_Y(8), handle_light);
    if (kind == 0) {
        /* Pickaxe: a wide head with two down-turned tips. */
        hud_line(TOOL_X(3), TOOL_Y(6), TOOL_X(16), TOOL_Y(6), head);
        hud_line(TOOL_X(3), TOOL_Y(7), TOOL_X(16), TOOL_Y(7), head_light);
        hud_line(TOOL_X(3), TOOL_Y(6), TOOL_X(2), TOOL_Y(10), head);
        hud_line(TOOL_X(16), TOOL_Y(6), TOOL_X(17), TOOL_Y(10), head);
    } else if (kind == 1) {
        /* Axe: a blade on the upper right. */
        hud_rect(TOOL_X(10), TOOL_Y(3), (u * 7) / 20, (u * 7) / 20, head);
        hud_rect(TOOL_X(10), TOOL_Y(3), (u * 7) / 20, (u * 2) / 20,
                 head_light);
    } else if (kind == 2) {
        /* Shovel: a spade at the top. */
        hud_rect(TOOL_X(9), TOOL_Y(3), (u * 6) / 20, (u * 6) / 20, head);
        hud_rect(TOOL_X(9), TOOL_Y(3), (u * 6) / 20, (u * 2) / 20,
                 head_light);
    } else {
        /* Sword: a long blade, guard and pommel. */
        hud_line(TOOL_X(6), TOOL_Y(15), TOOL_X(16), TOOL_Y(3), head);
        hud_line(TOOL_X(7), TOOL_Y(15), TOOL_X(17), TOOL_Y(3), head_light);
        hud_line(TOOL_X(4), TOOL_Y(11), TOOL_X(10), TOOL_Y(16), handle);
        hud_pixel(TOOL_X(4), TOOL_Y(17), handle_light);
    }
#undef TOOL_X
#undef TOOL_Y
}

static void draw_item_icon(int x, int y, int size, uint8_t item) {
    int ix;
    int iy;
    if (game_item_is_tool(item)) {
        draw_tool_icon(x, y, size, item);
        return;
    }
    if (item == BLOCK_AIR || item >= BLOCK_TYPE_COUNT) {
        return;
    }
    for (iy = 0; iy < size; ++iy) {
        const int kind = iy < size / 3 ? TEX_KIND_TOP : TEX_KIND_SIDE;
        const int sy = iy < size / 3
                           ? iy * 16 / (size / 3)
                           : (iy - size / 3) * 16 / (size - size / 3);
        const int row = sy << 4;
        for (ix = 0; ix < size; ++ix) {
            const int sx = ix * 16 / size;
            hud_pixel(x + ix, y + iy, g_tex[item][kind][row | sx]);
        }
    }
}

static void draw_count(int x, int y, uint8_t count) {
    char text[4];
    if (count <= 1) {
        return;
    }
    text[0] = (char)('0' + (count / 10) % 10);
    text[1] = (char)('0' + count % 10);
    text[2] = '\0';
    if (count >= 10) {
        hud_text(x - 13, y - 6, text, COL_SHADOW, 1);
        hud_text(x - 14, y - 7, text, COL_WHITE, 1);
    } else {
        hud_text(x - 9, y - 6, text + 1, COL_SHADOW, 1);
        hud_text(x - 10, y - 7, text + 1, COL_WHITE, 1);
    }
}

static void draw_slot(int x, int y, uint8_t item, uint8_t count,
                      uint8_t selected) {
    hud_rect(x, y, HOTBAR_SLOT, HOTBAR_SLOT, COL_PANEL);
    if (item != BLOCK_AIR) {
        draw_item_icon(x + 2, y + 2, HOTBAR_SLOT - 4, item);
        draw_count(x + HOTBAR_SLOT - 2, y + HOTBAR_SLOT - 1, count);
    }
    hud_rect_outline(x, y, HOTBAR_SLOT, HOTBAR_SLOT, selected ? 2 : 1,
                     selected ? COL_SELECT : COL_SHADOW);
}

static void draw_inventory(const hud_state_t *hud) {
    int row;
    int column;
    char *out;
    char title[24];
    dim_region(g_layout.view_x, g_layout.view_y, g_layout.view_w,
               g_layout.view_h);
    hud_rect(g_inv_layout.panel_x, g_inv_layout.panel_y,
             g_inv_layout.panel_w, g_inv_layout.panel_h, COL_PANEL);
    hud_rect_outline(g_inv_layout.panel_x, g_inv_layout.panel_y,
                     g_inv_layout.panel_w, g_inv_layout.panel_h, 2,
                     COL_PANEL_LIGHT);
    {
        const char *label = hud->craft_table ? "CRAFTING" : "INVENTORY";
        out = title;
        while (*label != '\0' && out < title + sizeof(title) - 1) {
            *out++ = *label++;
        }
        *out = '\0';
    }
    hud_text_centered(g_inv_layout.panel_y + 8, title, COL_TEXT, 2);
    hud_text_centered(g_inv_layout.panel_y + 22, "TAP MOVE  HOLD SPLIT",
                      COL_MUTED, 1);

    /* Crafting grid, arrow and result. */
    for (row = 0; row < g_inv_mode; ++row) {
        for (column = 0; column < g_inv_mode; ++column) {
            const int x = g_inv_layout.craft_x + column * HOTBAR_SLOT;
            const int y = g_inv_layout.craft_y + row * HOTBAR_SLOT;
            const int index = row * g_inv_mode + column;
            const item_stack_t *slot =
                hud->craft_table ? &g_table_craft[index] : &g_craft[index];
            draw_slot(x, y, slot->item, slot->count, 0);
        }
    }
    {
        const int arrow_x = g_inv_layout.craft_x +
                            g_inv_mode * HOTBAR_SLOT + 4;
        const int arrow_y = g_inv_layout.craft_y +
                            (g_inv_mode * HOTBAR_SLOT) / 2;
        const item_stack_t *result =
            hud->craft_table ? &g_table_result : &g_craft_result;
        hud_line(arrow_x, arrow_y, arrow_x + 10, arrow_y, COL_MUTED);
        hud_line(arrow_x + 6, arrow_y - 4, arrow_x + 10, arrow_y,
                 COL_MUTED);
        hud_line(arrow_x + 6, arrow_y + 4, arrow_x + 10, arrow_y,
                 COL_MUTED);
        draw_slot(g_inv_layout.result_x, g_inv_layout.result_y,
                  result->item, result->count, result->item != BLOCK_AIR);
    }

    for (row = 0; row < 3; ++row) {
        for (column = 0; column < 9; ++column) {
            const int x = g_inv_layout.main_x + column * HOTBAR_SLOT;
            const int y = g_inv_layout.main_y + row * HOTBAR_SLOT;
            const item_stack_t *slot =
                &g_inventory[HOTBAR_SLOTS + row * 9 + column];
            draw_slot(x, y, slot->item, slot->count, 0);
        }
    }
    for (column = 0; column < 9; ++column) {
        const int x = g_inv_layout.inv_hotbar_x + column * HOTBAR_SLOT;
        const item_stack_t *slot = &g_inventory[column];
        draw_slot(x, g_inv_layout.inv_hotbar_y, slot->item, slot->count,
                  (uint8_t)(column == hud->hotbar_selected));
    }
    hud_line(g_inv_layout.close_x - 7, g_inv_layout.close_y - 7,
             g_inv_layout.close_x + 7, g_inv_layout.close_y + 7, COL_SHADOW);
    hud_line(g_inv_layout.close_x - 7, g_inv_layout.close_y + 7,
             g_inv_layout.close_x + 7, g_inv_layout.close_y - 7, COL_SHADOW);
    hud_line(g_inv_layout.close_x - 6, g_inv_layout.close_y - 6,
             g_inv_layout.close_x + 6, g_inv_layout.close_y + 6, COL_TEXT);
    hud_line(g_inv_layout.close_x - 6, g_inv_layout.close_y + 6,
             g_inv_layout.close_x + 6, g_inv_layout.close_y - 6, COL_TEXT);

    if (hud->cursor_item != BLOCK_AIR && hud->cursor_count != 0) {
        const int x = hud->pointer_x - HOTBAR_SLOT / 2;
        const int y = hud->pointer_y - HOTBAR_SLOT - 6;
        hud_rect(x, y, HOTBAR_SLOT, HOTBAR_SLOT, COL_PANEL);
        draw_item_icon(x + 2, y + 2, HOTBAR_SLOT - 4, hud->cursor_item);
        draw_count(x + HOTBAR_SLOT - 2, y + HOTBAR_SLOT - 1,
                   hud->cursor_count);
    }
}

static void draw_use_icon(int cx, int cy) {
    /* A small crafting table: top board and two legs. */
    hud_rect(cx - 7, cy - 5, 15, 3, COL_WHITE);
    hud_rect(cx - 6, cy - 2, 3, 7, COL_WHITE);
    hud_rect(cx + 4, cy - 2, 3, 7, COL_WHITE);
}

static void draw_controls(const hud_state_t *hud) {
    draw_button(g_layout.jump_x, g_layout.jump_y, g_layout.jump_r,
                COL_PANEL_LIGHT, hud->jump_held);
    draw_arrow(g_layout.jump_x, g_layout.jump_y - 3, 1);

    /* Context action: mine, attack or use. */
    draw_button(g_layout.action_x, g_layout.action_y, g_layout.action_r,
                COL_PANEL_LIGHT,
                (uint8_t)(hud->action_held ||
                          hud->action_mode != 0));
    if (hud->action_mode == 1) {
        draw_sword(g_layout.action_x, g_layout.action_y);
    } else if (hud->action_mode == 2) {
        draw_use_icon(g_layout.action_x, g_layout.action_y);
    } else {
        draw_pickaxe(g_layout.action_x, g_layout.action_y);
    }

    draw_button(g_layout.place_x, g_layout.place_y, g_layout.place_r,
                COL_PANEL_LIGHT, 0);
    hud_rect(g_layout.place_x - 7, g_layout.place_y - 1, 15, 3, COL_WHITE);
    hud_rect(g_layout.place_x - 1, g_layout.place_y - 7, 3, 15, COL_WHITE);

    if (hud->flying) {
        draw_button(g_layout.down_x, g_layout.down_y, g_layout.down_r,
                    COL_PANEL_LIGHT, hud->down_held);
        draw_arrow(g_layout.down_x, g_layout.down_y - 3, -1);
    }

    draw_button(g_layout.fly_x, g_layout.fly_y, g_layout.fly_r,
                COL_PANEL_LIGHT, hud->flying);
    hud_text(g_layout.fly_x - 5, g_layout.fly_y - 2, "F", COL_WHITE, 1);

    /* Menu: three bars. */
    draw_button(g_layout.menu_x, g_layout.menu_y, g_layout.menu_r,
                COL_PANEL_LIGHT, 0);
    hud_rect(g_layout.menu_x - 6, g_layout.menu_y - 5, 13, 2, COL_TEXT);
    hud_rect(g_layout.menu_x - 6, g_layout.menu_y - 1, 13, 2, COL_TEXT);
    hud_rect(g_layout.menu_x - 6, g_layout.menu_y + 3, 13, 2, COL_TEXT);

    /* Backpack: a small bag outline. */
    draw_button(g_layout.bag_x, g_layout.bag_y, g_layout.bag_r,
                COL_PANEL_LIGHT, hud->inventory_open);
    hud_rect(g_layout.bag_x - 6, g_layout.bag_y - 4, 13, 9, COL_TEXT);
    hud_rect(g_layout.bag_x - 4, g_layout.bag_y - 2, 9, 5, COL_PANEL);
    hud_rect(g_layout.bag_x - 3, g_layout.bag_y - 6, 7, 3, COL_TEXT);
}

static void draw_joystick(const hud_state_t *hud) {
    if (!hud->move_active) {
        return;
    }
    hud_circle_outline(hud->move_origin_x, hud->move_origin_y, 36, COL_SHADOW);
    hud_circle_outline(hud->move_origin_x, hud->move_origin_y, 34, COL_MUTED);
    hud_circle((int)(hud->move_origin_x + hud->move_dx),
               (int)(hud->move_origin_y + hud->move_dy), 11, COL_SHADOW);
    hud_circle((int)(hud->move_origin_x + hud->move_dx),
               (int)(hud->move_origin_y + hud->move_dy), 9, COL_PANEL_LIGHT);
}

static void draw_use_hint(const hud_state_t *hud) {
    if (!hud->target_table || hud->inventory_open) {
        return;
    }
    hud_text_centered(g_layout.view_y + g_layout.view_h / 2 + 20,
                      "ACTION: USE", COL_SHADOW, 1);
    hud_text_centered(g_layout.view_y + g_layout.view_h / 2 + 19,
                      "ACTION: USE", COL_SELECT, 1);
}

static void draw_mine_progress(float progress) {
    const int x = g_layout.view_x + g_layout.view_w / 2 - 22;
    const int y = g_layout.view_y + g_layout.view_h / 2 + 14;
    if (progress <= 0.01F) {
        return;
    }
    if (progress > 1.0F) {
        progress = 1.0F;
    }
    hud_rect(x - 1, y - 1, 46, 7, COL_SHADOW);
    hud_rect(x, y, 44, 5, COL_PANEL);
    hud_rect(x, y, (int)(44.0F * progress), 5, COL_SELECT);
}

static char *put_i32(char *out, int32_t value) {
    char reversed[12];
    int digits = 0;
    uint32_t magnitude;
    if (value < 0) {
        *out++ = '-';
        magnitude = (uint32_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint32_t)value;
    }
    do {
        reversed[digits++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude != 0);
    while (digits != 0) {
        *out++ = reversed[--digits];
    }
    return out;
}

static void draw_status(const hud_state_t *hud) {
    char line[32];
    char *out = line;
    const int fps = (int)(hud->fps_x10 / 10u);
    const char *selected =
        g_inventory[hud->hotbar_selected].item == BLOCK_AIR
            ? "EMPTY"
            : game_item_name(g_inventory[hud->hotbar_selected].item);
    if (hud->flying) {
        *out++ = 'F';
        *out++ = 'L';
        *out++ = 'Y';
        *out++ = ' ';
    }
    *out++ = 'G';
    *out++ = 'F';
    *out++ = 'P';
    *out++ = 'S';
    *out++ = ' ';
    if (fps >= 100) {
        *out++ = (char)('0' + (fps / 100) % 10);
    }
    if (fps >= 10) {
        *out++ = (char)('0' + (fps / 10) % 10);
    }
    *out++ = (char)('0' + fps % 10);
    *out++ = ' ';
    if (hud->quality_manual != 0) {
        *out++ = 'M';
        *out++ = 'A';
        *out++ = 'N';
    } else {
        *out++ = 'A';
        *out++ = 'U';
        *out++ = 'T';
        *out++ = 'O';
    }
    *out++ = ' ';
    *out++ = (char)('0' + hud->quality);
    *out++ = 'X';
    *out = '\0';
    if (hud->show_performance) {
        hud_text_centered(g_layout.view_y + 5, line, COL_SHADOW, 2);
        hud_text_centered(g_layout.view_y + 4, line, COL_TEXT, 2);
    }

    if (hud->show_performance) {
        char position[32];
        char *out = position;
        *out++ = 'X';
        *out++ = ' ';
        out = put_i32(out, hud->pos_x);
        *out++ = ' ';
        *out++ = 'Z';
        *out++ = ' ';
        out = put_i32(out, hud->pos_z);
        *out = '\0';
        hud_text_centered(g_layout.view_y + 19, position, COL_SHADOW, 1);
        hud_text_centered(g_layout.view_y + 18, position, COL_TEXT, 1);
        out = position;
        *out++ = 'D';
        *out++ = 'D';
        *out++ = 'A';
        *out++ = ' ';
        out = put_i32(out, (int32_t)(hud->dda_steps_x10 / 10u));
        *out++ = '.';
        *out++ = (char)('0' + hud->dda_steps_x10 % 10u);
        *out++ = '/';
        out = put_i32(out, hud->dda_steps_max);
        *out = '\0';
        hud_text_centered(g_layout.view_y + 31, position, COL_SHADOW, 1);
        hud_text_centered(g_layout.view_y + 30, position, COL_TEXT, 1);
        out = position;
        *out++ = 'R';
        *out++ = ' ';
        out = put_i32(out, hud->guest_render_us_div_100 / 10u);
        *out++ = '.';
        *out++ = (char)('0' + hud->guest_render_us_div_100 % 10u);
        *out++ = 'M';
        *out++ = 'S';
        *out++ = ' ';
        *out++ = 'B';
        *out++ = ' ';
        out = put_i32(out, hud->buffer_wait_us_div_100 / 10u);
        *out++ = '.';
        *out++ = (char)('0' + hud->buffer_wait_us_div_100 % 10u);
        *out++ = 'M';
        *out++ = 'S';
        *out = '\0';
        hud_text_centered(g_layout.view_y + 43, position, COL_SHADOW, 1);
        hud_text_centered(g_layout.view_y + 42, position, COL_TEXT, 1);
    }

    if (hud->now_ms < 10000u && !hud->show_performance) {
        hud_text_centered(34, "LEFT MOVE   RIGHT LOOK", COL_SHADOW, 1);
        hud_text_centered(33, "LEFT MOVE   RIGHT LOOK", COL_TEXT, 1);
        hud_text_centered(46, "HOLD ACTION: MINE/ATTACK/USE", COL_SHADOW, 1);
        hud_text_centered(45, "HOLD ACTION: MINE/ATTACK/USE", COL_TEXT, 1);
        hud_text_centered(58, "TAP PLACE TO BUILD  2X JUMP: FLY",
                          COL_SHADOW, 1);
        hud_text_centered(57, "TAP PLACE TO BUILD  2X JUMP: FLY",
                          COL_TEXT, 1);
        hud_text_centered(70, "PAD: Z JUMP  X ACTION  ENT PLACE  TAB ACTION",
                          COL_SHADOW, 1);
        hud_text_centered(69, "PAD: Z JUMP  X ACTION  ENT PLACE  TAB ACTION",
                          COL_TEXT, 1);
    }
    if (hud->toast != NULL && hud->toast[0] != '\0') {
        const int width = text_width(hud->toast, 1);
        const int x = g_layout.view_x + (g_layout.view_w - width) / 2;
        const int y = g_layout.hotbar_y - 14;
        hud_rect(x - 4, y - 3, width + 8, 11, COL_SHADOW);
        hud_text(x, y, hud->toast, COL_TEXT, 1);
    }
    {
        const int width = text_width(selected, 1);
        hud_text(g_layout.view_x + (g_layout.view_w - width) / 2,
                 g_layout.hotbar_y - 26, selected, COL_MUTED, 1);
    }
}

static void hud_text_centered_width(int x, int w, int y, const char *text,
                                     uint16_t color, int scale) {
    hud_text(x + (w - text_width(text, scale)) / 2, y, text, color, scale);
}

static void menu_draw_button(int index, int y, int height, const char *label,
                             uint8_t enabled) {
    const int x = g_layout.view_x + (g_layout.view_w - 200) / 2;
    const int w = 200;
    g_menu_buttons[index].x = x;
    g_menu_buttons[index].y = y;
    g_menu_buttons[index].w = w;
    g_menu_buttons[index].h = height;
    g_menu_buttons[index].enabled = enabled;
    hud_rect(x, y, w, height, enabled ? COL_PANEL_LIGHT : COL_PANEL);
    hud_rect_outline(x, y, w, height, 2,
                     enabled ? COL_SELECT : COL_SHADOW);
    hud_text_centered_width(x, w, y + (height - 10) / 2, label,
                            enabled ? COL_TEXT : COL_MUTED, 2);
}

void render_menu(const menu_state_t *menu) {
    char seed_text[28];
    char quality_text[28];
    char *out;
    int row;
    int y;
    if (menu->overlay) {
        dim_region(g_layout.view_x, g_layout.view_y, g_layout.view_w,
                   g_layout.view_h);
    } else {
        for (row = 0; row < g_layout.view_h; ++row) {
            const int t =
                row * 255 / (g_layout.view_h > 0 ? g_layout.view_h : 1);
            const int r = 12 + t * 26 / 255;
            const int g = 22 + t * 52 / 255;
            const int b = 48 + t * 96 / 255;
            hud_rect(g_layout.view_x, g_layout.view_y + row, g_layout.view_w,
                     1, RGB565(r, g, b));
        }
    }
    if (menu->screen == 2) {
        hud_text_centered(g_layout.view_y + 30, "PAUSED", COL_SELECT, 3);
    } else {
        hud_text_centered(g_layout.view_y + 34, "VOXEL CRAFT", COL_SELECT, 3);
    }

    out = seed_text;
    *out++ = 'S';
    *out++ = 'E';
    *out++ = 'E';
    *out++ = 'D';
    *out++ = ' ';
    {
        char digits[12];
        int count = 0;
        uint32_t value = menu->seed;
        char *p = seed_text + 5;
        do {
            digits[count++] = (char)('0' + value % 10u);
            value /= 10u;
        } while (value != 0 && count < (int)sizeof(digits));
        while (count != 0) {
            *p++ = digits[--count];
        }
        *p = '\0';
    }
    if (menu->screen != 2) {
        hud_text_centered(g_layout.view_y + 62, seed_text, COL_MUTED, 1);
    }

    if (menu->screen == 2) {
        g_menu_button_count = 4;
        y = g_layout.view_y + 72;
        menu_draw_button(0, y, 30, "RESUME", 1);
        menu_draw_button(1, y + 34, 30, "SAVE GAME", menu->has_game);
        menu_draw_button(2, y + 68, 30, "SETTINGS", 1);
        menu_draw_button(3, y + 102, 30, "MAIN MENU", 1);
    } else if (menu->screen == 0) {
        g_menu_button_count = 3;
        y = g_layout.view_y + 88;
        menu_draw_button(0, y, 34, "NEW GAME", 1);
        menu_draw_button(1, y + 42, 34, "LOAD SAVE", menu->has_save);
        menu_draw_button(2, y + 84, 34, "SETTINGS", 1);
    } else {
        out = quality_text;
        *out++ = 'Q';
        *out++ = 'U';
        *out++ = 'A';
        *out++ = 'L';
        *out++ = 'I';
        *out++ = 'T';
        *out++ = 'Y';
        *out++ = ':';
        *out++ = ' ';
        if (menu->quality_manual == 0) {
            *out++ = 'A';
            *out++ = 'U';
            *out++ = 'T';
            *out++ = 'O';
        } else {
            *out++ = (char)('0' + menu->quality);
            *out++ = 'X';
        }
        *out = '\0';
        g_menu_button_count = 5;
        y = g_layout.view_y + 68;
        menu_draw_button(0, y, 28, quality_text, 1);
        menu_draw_button(1, y + 30, 28,
                         menu->show_performance ? "PERFORMANCE: ON"
                                                : "PERFORMANCE: OFF",
                         1);
        menu_draw_button(2, y + 60, 28, "SAVE GAME", menu->has_game);
        menu_draw_button(3, y + 90, 28, "DELETE SAVE", menu->has_save);
        menu_draw_button(4, y + 120, 28, "BACK", 1);
    }
    if (menu->toast != NULL && menu->toast[0] != '\0') {
        hud_text_centered(g_layout.view_y + g_layout.view_h - 16,
                          menu->toast, COL_TEXT, 1);
    }
    if (menu->screen == 0) {
        hud_text_centered(g_layout.view_y + g_layout.view_h - 10,
                          "TAP TO SELECT", COL_MUTED, 1);
    }
}

void render_hud(const hud_state_t *hud) {
    render_ensure_textures();
    if (hud->inventory_open) {
        draw_inventory(hud);
        return;
    }
    draw_joystick(hud);
    draw_crosshair(hud->swing);
    draw_use_hint(hud);
    draw_mine_progress(hud->mine_progress);
    draw_hotbar(hud);
    draw_controls(hud);
    draw_status(hud);
}

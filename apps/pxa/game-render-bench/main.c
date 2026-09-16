#include <stdint.h>

#include "pxa.h"
#include "pxa_game_render.h"
#include "pxa_raster.h"

#define WIDTH 296
#define HEIGHT 240
#define CREATE_REQUEST UINT32_C(1)
#define FRAME_PERIOD_MS 16u
#define MODE_TICKS 300u
#define SPRITE_COUNT 192u
#define CUBE_COUNT 12u
#define TRIANGLES_PER_GROUP (CUBE_COUNT * 4u)
#define VERTICES_PER_GROUP (TRIANGLES_PER_GROUP * 3u)

static uint8_t g_packet[64];
static uint8_t g_upload[PXA_RASTER_UPLOAD_HEADER_BYTES + 512u];
static uint8_t g_draw[PXA_RASTER_MAX_DRAW_BYTES];
static uint8_t g_texture[16 * 16];
static uint16_t g_palette[256];
static pxa_raster_sprite_instance_t g_sprites[SPRITE_COUNT];
static pxa_raster_vertex_t g_triangles[3][VERTICES_PER_GROUP];
static uint32_t g_context;
static uint32_t g_capabilities;
static uint32_t g_tick;
static uint64_t g_frame_id;
static uint64_t g_fps_window_us;
static uint32_t g_fps_frames;
static uint32_t g_fps;
static uint32_t g_host_raster_us;

static const int16_t kSinQ10[16] = {
    0, 392, 724, 946, 1024, 946, 724, 392,
    0, -392, -724, -946, -1024, -946, -724, -392,
};

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((uint16_t)(red >> 3) << 11) |
                      ((uint16_t)(green >> 2) << 5) | (blue >> 3));
}

static void prepare_resources(void) {
    uint32_t index;
    for (index = 0; index < 256; ++index) {
        const uint8_t red = (uint8_t)((index * 37u) & 255u);
        const uint8_t green = (uint8_t)((index * 73u) & 255u);
        const uint8_t blue = (uint8_t)((index * 19u) & 255u);
        g_palette[index] = rgb565(red, green, blue);
    }
    g_palette[0] = 0;
    for (index = 0; index < sizeof(g_texture); ++index) {
        const uint32_t x = index & 15u;
        const uint32_t y = index >> 4;
        g_texture[index] = (uint8_t)(1u + ((x >> 2) + (y >> 2) * 4u) * 11u);
    }
}

static int upload_resources(void) {
    return pxa_raster_upload_palette_rgb565(
               g_context, g_palette, g_upload, sizeof(g_upload)) ==
               (int32_t)sizeof(g_upload) &&
           pxa_raster_upload_texture_index8(
               g_context, 0, 16, 16, g_texture, g_upload,
               sizeof(g_upload)) ==
               (int32_t)(PXA_RASTER_UPLOAD_HEADER_BYTES + sizeof(g_texture));
}

static void append_rect(pxa_raster_draw_list_t *list, int x, int y,
                        int width, int height, uint16_t color) {
    int16_t xy[8];
    xy[0] = xy[6] = (int16_t)(x << 4);
    xy[1] = xy[3] = (int16_t)(y << 4);
    xy[2] = xy[4] = (int16_t)((x + width) << 4);
    xy[5] = xy[7] = (int16_t)((y + height) << 4);
    (void)pxa_raster_flat_quad(list, xy, color);
}

static void append_telemetry(pxa_raster_draw_list_t *list, uint8_t mode) {
    uint32_t fps_width = g_fps * 140u / 60u;
    uint32_t raster_width = g_host_raster_us * 140u / 30000u;
    if (fps_width > 140u) fps_width = 140u;
    if (raster_width > 140u) raster_width = 140u;
    append_rect(list, 0, 0, WIDTH, 14, rgb565(10, 13, 18));
    append_rect(list, 3, 3, 8, 8,
                mode == 0 ? rgb565(20, 210, 225)
                          : rgb565(245, 165, 35));
    if (fps_width != 0)
        append_rect(list, 15, 3, (int)fps_width, 3, rgb565(40, 220, 90));
    if (raster_width != 0)
        append_rect(list, 15, 8, (int)raster_width, 3,
                    rgb565(238, 72, 64));
}

static int render_2d(pxa_raster_draw_list_t *list) {
    uint32_t index;
    for (index = 0; index < SPRITE_COUNT; ++index) {
        const uint32_t column = index % 16u;
        const uint32_t row = index / 16u;
        const int wave = (int)((g_tick * (1u + row % 3u) + index * 7u) % 22u);
        pxa_raster_sprite_instance_t *sprite = &g_sprites[index];
        sprite->x = (int16_t)(column * 18u + (row & 1u) * 8u - 2u);
        sprite->y = (int16_t)(18u + row * 18u +
                              (wave < 11 ? wave : 21 - wave) - 5);
        sprite->width = 14;
        sprite->height = 14;
        sprite->source_x = 0;
        sprite->source_y = 0;
        sprite->source_width = 16;
        sprite->source_height = 16;
    }
    return pxa_raster_sprite_batch(
        list, 0, 0, g_capabilities, g_sprites, SPRITE_COUNT, 0);
}

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} point3_t;

static void project_vertex(pxa_raster_vertex_t *out, point3_t point,
                           int center_x, int center_y, uint8_t light) {
    int32_t depth = point.z + 720;
    int32_t x = center_x + (int32_t)point.x * 420 / depth;
    int32_t y = center_y - (int32_t)point.y * 420 / depth;
    if (x < -512) x = -512;
    if (x > 511) x = 511;
    if (y < -512) y = -512;
    if (y > 511) y = 511;
    out->x_q4 = (int16_t)(x << 4);
    out->y_q4 = (int16_t)(y << 4);
    out->u_q4 = 0;
    out->v_q4 = 0;
    out->light = light;
    out->depth_q8 = (uint16_t)depth;
}

static void append_cube(uint32_t cube, uint8_t phase) {
    static const int8_t corners[8][3] = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1},
    };
    static const uint8_t faces[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
        {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0},
    };
    point3_t points[8];
    const int32_t sine = kSinQ10[phase & 15u];
    const int32_t cosine = kSinQ10[(phase + 4u) & 15u];
    const int center_x = 38 + (int)(cube % 4u) * 73;
    const int center_y = 50 + (int)(cube / 4u) * 68;
    uint8_t corner;
    uint8_t face;
    for (corner = 0; corner < 8; ++corner) {
        const int32_t x = corners[corner][0] * 72;
        const int32_t y = corners[corner][1] * 72;
        const int32_t z = corners[corner][2] * 72;
        points[corner].x = (int16_t)((x * cosine + z * sine) / 1024);
        points[corner].y = (int16_t)y;
        points[corner].z = (int16_t)((z * cosine - x * sine) / 1024 +
                                     (int32_t)(cube % 3u) * 22);
    }
    for (face = 0; face < 6; ++face) {
        const uint8_t group = face >> 1;
        const uint32_t triangle = cube * 4u + (face & 1u) * 2u;
        pxa_raster_vertex_t *vertices = &g_triangles[group][triangle * 3u];
        const uint8_t *quad = faces[face];
        project_vertex(&vertices[0], points[quad[0]], center_x, center_y,
                       (uint8_t)(245u - group * 45u));
        project_vertex(&vertices[1], points[quad[1]], center_x, center_y,
                       (uint8_t)(245u - group * 45u));
        project_vertex(&vertices[2], points[quad[2]], center_x, center_y,
                       (uint8_t)(245u - group * 45u));
        project_vertex(&vertices[3], points[quad[0]], center_x, center_y,
                       (uint8_t)(245u - group * 45u));
        project_vertex(&vertices[4], points[quad[2]], center_x, center_y,
                       (uint8_t)(245u - group * 45u));
        project_vertex(&vertices[5], points[quad[3]], center_x, center_y,
                       (uint8_t)(245u - group * 45u));
    }
}

static int render_3d(pxa_raster_draw_list_t *list) {
    static const uint16_t colors[3] = {
        UINT16_C(0xf945), UINT16_C(0x3e99), UINT16_C(0xff24)};
    uint32_t cube;
    uint8_t group;
    for (cube = 0; cube < CUBE_COUNT; ++cube)
        append_cube(cube, (uint8_t)(g_tick / 3u + cube));
    for (group = 0; group < 3; ++group) {
        if (!pxa_raster_triangle_batch(
                list, g_triangles[group], TRIANGLES_PER_GROUP, 0, 1,
                colors[group]))
            return 0;
    }
    return 1;
}

static int render_frame(void) {
    pxa_raster_draw_list_t list;
    const uint8_t mode = (uint8_t)((g_tick / MODE_TICKS) & 1u);
    pxa_raster_draw_list_begin(&list, g_draw, sizeof(g_draw), ++g_frame_id);
    (void)pxa_raster_clear(&list,
                           mode == 0 ? rgb565(7, 18, 27)
                                     : rgb565(18, 12, 20));
    if (!(mode == 0 ? render_2d(&list) : render_3d(&list))) return 0;
    append_telemetry(&list, mode);
    return pxa_raster_submit(g_context, &list) > 0;
}

static void sample_telemetry(void) {
    pxa_raster_telemetry_t telemetry;
    if (pxa_raster_query_telemetry(g_context, &telemetry) ==
        (int32_t)PXA_RASTER_TELEMETRY_BYTES)
        g_host_raster_us = telemetry.last_host_raster_us;
}

int32_t pxa_app_start(const uint8_t *config, uint32_t length) {
    (void)config;
    (void)length;
    prepare_resources();
    return pxa_game_render_create(CREATE_REQUEST, WIDTH, HEIGHT, 3, 1,
                                  g_packet, sizeof(g_packet))
               ? PXA_STATUS_OK
               : PXA_STATUS_INTERNAL;
}

int32_t pxa_app_on_event(const uint8_t *bytes, uint32_t length) {
    pxa_event_t event;
    uint64_t timestamp_us;
    if (!pxa_parse_event(bytes, length, &event)) return PXA_EVENT_UNHANDLED;
    if (event.request_id == CREATE_REQUEST) {
        pxa_game_render_create_result_t created;
        if (!pxa_game_render_parse_create(&event, &created))
            return PXA_EVENT_UNHANDLED;
        if (created.status != PXA_STATUS_OK ||
            (created.capabilities & (PXA_RASTER_CAP_SPRITE_BATCH |
                                     PXA_RASTER_CAP_TRIANGLE_BATCH)) !=
                (PXA_RASTER_CAP_SPRITE_BATCH |
                 PXA_RASTER_CAP_TRIANGLE_BATCH))
            return PXA_EVENT_HANDLED;
        g_context = created.context_handle;
        g_capabilities = created.capabilities;
        if (!upload_resources()) return PXA_EVENT_HANDLED;
        (void)render_frame();
        (void)pxa_clock_set_period(FRAME_PERIOD_MS);
        return PXA_EVENT_HANDLED;
    }
    if (!pxa_clock_tick_timestamp_us(&event, &timestamp_us) || g_context == 0)
        return PXA_EVENT_UNHANDLED;
    ++g_tick;
    ++g_fps_frames;
    if (g_fps_window_us == 0) g_fps_window_us = timestamp_us;
    if (timestamp_us - g_fps_window_us >= UINT64_C(1000000)) {
        g_fps = (uint32_t)((uint64_t)g_fps_frames * UINT64_C(1000000) /
                           (timestamp_us - g_fps_window_us));
        g_fps_frames = 0;
        g_fps_window_us = timestamp_us;
    }
    if ((g_tick % 30u) == 0) sample_telemetry();
    (void)render_frame();
    return PXA_EVENT_HANDLED;
}

void pxa_app_stop(uint32_t reason) {
    (void)reason;
    (void)pxa_clock_set_period(0);
    if (g_context != 0) (void)pxa_close_handle(g_context);
    g_context = 0;
}

/* Maze Spike: validates the maze-evil port architecture on PXA.
 *
 * The raycaster runs entirely in the Guest (a C port of maze-evil's
 * guest/runtime/raycast.cpp geometry plus micropixel's Host raster kernels) and
 * writes canonical RGB565 frames into a static buffer, submitted through the
 * PXA Surface service. The on-screen overlay shows presented/submitted/dropped
 * frames, blocked writes and the clock-tick interval so overruns are visible
 * without host logging.
 */
#include <stdint.h>

#include "assets.h"
#include "palette.h"
#include "pxa.h"
#include "pxa_surface.h"
#include "raycast.h"
#include "rc_math.h"

#define VIEW_WIDTH 296
#define VIEW_HEIGHT 240
#define FRAME_BYTES (VIEW_WIDTH * VIEW_HEIGHT * 2u)
/* 33 ms = 30 fps. Override at package time with
 * PXA_APP_DEFINES=MAZE_SPIKE_PERIOD_MS=16 to prove the headroom. */
#ifndef MAZE_SPIKE_PERIOD_MS
#define MAZE_SPIKE_PERIOD_MS 16u
#endif
#define FRAME_PERIOD_MS MAZE_SPIKE_PERIOD_MS
#define STATS_TICKS 30u
#define CREATE_REQUEST UINT32_C(1)
#define CONFIGURE_REQUEST UINT32_C(2)
#define QUERY_REQUEST UINT32_C(100)
#define CLOCK_END_REQUEST UINT32_C(201)

/* maze-evil level.cpp: '#' brick, '%' stone, '=' tech, '&' flesh, 'W' wood,
 * 'D' door, 'X' exit; doors and the exit are solid walls in the spike. */
static const char *const kLevelRows[32] = {
    "################################",
    "#......#.......%%%%%%%%%%%%%%%%#",
    "#.S....#.......%.......T......%#",
    "#......D.......%..............%#",
    "#......#.......%...B......B...%#",
    "#..T...#.......%..............%#",
    "########.......%.....E....E...%#",
    "#......#.......%..............%#",
    "#..A...#..E....%..H...........%#",
    "#......D.......%%%%D%%%%%%%%%%%#",
    "#......#...............#.......#",
    "########...............#.......#",
    "====================D==#..E.A..#",
    "=....=....=....=......=#.......#",
    "=.E..=..T.=..E.=......=#.......#",
    "=....D....D....D......=#...T...#",
    "=....=....=....=......=####.####",
    "==D=====D=====D=......=&&&&D&&&&",
    "=.....................=&.......&",
    "=..T......A.......T...=&..E....&",
    "=.....................D........&",
    "=..............H......=&.......&",
    "=...E......B....B.....=&&&&&&D&&",
    "==========D===========D........&",
    "WWWWWWWWWW.WWWWWWWWWWWW&.......&",
    "W.......W.............W&..E.E..&",
    "W..A.H..D......T......W&.......&",
    "W.......W.............W&..H....&",
    "W..T....W.....E..E....W&&&&D&&&&",
    "W.......W.............D.......X#",
    "W.......W......E......W.......X#",
    "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
};

static int8_t g_map[32 * 32];
static uint16_t g_pixels[VIEW_WIDTH * VIEW_HEIGHT];
static target_t g_target;
static uint8_t g_packet[128];
static ray_camera_t g_camera;
static float g_angle;

static uint32_t g_surface_handle;
static uint64_t g_frame_id;
static uint64_t g_prev_tick_us;
static uint64_t g_tick_sum_us;
static uint32_t g_tick_count;
static uint64_t g_tick_max_us;
static uint32_t g_ticks_since_stats;
static uint32_t g_query_pending;
static uint64_t g_query_sent_us;
static uint64_t g_sample_ts_us;
static uint64_t g_sample_presented;

static uint32_t g_fps_x10;
static uint64_t g_submitted;
static uint64_t g_presented;
static uint64_t g_dropped;
static uint32_t g_free_buffers;
static uint32_t g_blocked;
static uint64_t g_render_ref_us;
static uint64_t g_render_sum_us;
static uint64_t g_render_max_us;
static uint32_t g_render_count;
static uint32_t g_render_us;

static int8_t texture_for(char symbol) {
    switch (symbol) {
        case '#':
            return TEX_BRICK;
        case '%':
            return TEX_STONE;
        case '=':
            return TEX_TECH;
        case '&':
            return TEX_FLESH;
        case 'W':
            return TEX_WOOD;
        case 'D':
            return TEX_DOOR;
        case 'X':
            return TEX_EXIT;
        default:
            return -1;
    }
}

static void build_map(void) {
    int y;
    for (y = 0; y < 32; ++y) {
        int x;
        for (x = 0; x < 32; ++x) {
            g_map[y * 32 + x] = texture_for(kLevelRows[y][x]);
        }
    }
}

static char *put_str(char *out, const char *text) {
    while (*text != '\0') {
        *out++ = *text++;
    }
    return out;
}

static char *put_u64(char *out, uint64_t value) {
    char reversed[20];
    int digits = 0;
    do {
        reversed[digits++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    while (digits != 0) {
        *out++ = reversed[--digits];
    }
    return out;
}

static char *put_x10(char *out, uint32_t value) {
    out = put_u64(out, value / 10u);
    *out++ = '.';
    *out++ = (char)('0' + value % 10u);
    return out;
}

static void draw_metrics(void) {
    const uint16_t white = palette_color((uint8_t)(15 * 16 + 15));
    const uint16_t green = palette_color((uint8_t)(6 * 16 + 14));
    char line[48];
    char *p;

    raster_rect(&g_target, 0, 0, 168, 64, 0x0000);
    p = put_str(line, "FPS ");
    p = put_x10(p, g_fps_x10);
    *p = '\0';
    raster_text(&g_target, 3, 3, line, green, 2);
    p = put_str(line, "Q");
    p = put_u64(p, g_submitted);
    p = put_str(p, " P");
    p = put_u64(p, g_presented);
    p = put_str(p, " D");
    p = put_u64(p, g_dropped);
    *p = '\0';
    raster_text(&g_target, 3, 15, line, white, 2);
    p = put_str(line, "FREE ");
    p = put_u64(p, g_free_buffers);
    p = put_str(p, " BLK ");
    p = put_u64(p, g_blocked);
    *p = '\0';
    raster_text(&g_target, 3, 27, line, white, 2);
    p = put_str(line, "TICK AVG ");
    p = put_u64(p, g_tick_count == 0 ? 0 : g_tick_sum_us / g_tick_count / 1000u);
    p = put_str(p, " MAX ");
    p = put_u64(p, g_tick_max_us / 1000u);
    *p = '\0';
    raster_text(&g_target, 3, 39, line, white, 2);
    p = put_str(line, "R ");
    p = put_u64(p, g_render_us);
    p = put_str(p, " AV ");
    p = put_u64(p, g_render_count == 0 ? 0 : g_render_sum_us / g_render_count);
    p = put_str(p, " X ");
    p = put_u64(p, g_render_max_us);
    p = put_str(p, " US");
    *p = '\0';
    raster_text(&g_target, 3, 51, line, green, 2);
}

static void render_scene(void) {
    g_angle += 0.02F;
    if (g_angle > RC_TWO_PI) {
        g_angle -= RC_TWO_PI;
    }
    g_camera.x = 16.5F;
    g_camera.y = 18.5F;
    g_camera.dir_x = rc_cos(g_angle);
    g_camera.dir_y = rc_sin(g_angle);
    g_camera.plane_x = -g_camera.dir_y * 0.66F;
    g_camera.plane_y = g_camera.dir_x * 0.66F;
    raycast_frame(g_map, 32, 32, &g_camera, kTextures, &g_target);
    draw_metrics();
}

static int submit_frame(void) {
    int32_t status =
        pxa_surface_write_frame(g_surface_handle, (uint8_t *)g_pixels,
                                FRAME_BYTES);
    if (status == PXA_STATUS_WOULD_BLOCK) {
        ++g_blocked;
        return 1;
    }
    if (status != (int32_t)FRAME_BYTES) {
        return 0;
    }
    status = pxa_surface_queue_frame(g_surface_handle, g_frame_id + 1u, NULL, 0,
                                     g_packet, sizeof(g_packet));
    if (status != PXA_STATUS_OK) {
        return 0;
    }
    ++g_frame_id;
    return 1;
}

#if defined(MAZE_SPIKE_METRICS_LOG)
/* The Guest SDK has no logging import. The Host logs any control message it
 * rejects at error level, so the spike emits metrics as unknown opcodes on the
 * Core service and reads them back with `pxadb logcat`. Debug builds only. */
static void emit_metric(uint16_t id, uint32_t value) {
    uint8_t message[12];
    pxa_writer_t writer;
    pxa_writer_init(&writer, message, sizeof(message));
    if (pxa_put_u16(&writer, PXA_SERVICE_CORE) &&
        pxa_put_u16(&writer, (uint16_t)(0x9000u + id)) &&
        pxa_put_u32(&writer, value) && pxa_put_u32(&writer, 0)) {
        (void)pxa_control(message, (uint32_t)writer.length);
    }
}

static void log_metrics(void) {
    emit_metric(1, g_fps_x10);
    emit_metric(2, (uint32_t)(g_render_count == 0 ? 0
                                                  : g_render_sum_us /
                                                        g_render_count));
    emit_metric(3, (uint32_t)g_render_max_us);
    emit_metric(4, g_tick_count == 0 ? 0
                                     : (uint32_t)(g_tick_sum_us /
                                                  g_tick_count / 1000u));
    emit_metric(5, (uint32_t)(g_tick_max_us / 1000u));
    emit_metric(6, g_blocked);
    emit_metric(7, (uint32_t)g_presented);
    emit_metric(8, (uint32_t)g_dropped);
}
#endif

static void send_query(uint64_t timestamp_us) {
    if (!pxa_surface_query_state(QUERY_REQUEST, g_surface_handle, g_packet,
                                 sizeof(g_packet))) {
        return;
    }
    g_query_sent_us = timestamp_us;
    g_query_pending = 1;
}

int32_t pxa_app_start(const uint8_t *config, uint32_t length) {
    (void)config;
    (void)length;
    g_surface_handle = 0;
    g_frame_id = 0;
    g_prev_tick_us = 0;
    g_ticks_since_stats = 0;
    g_query_pending = 0;
    g_angle = 0.0F;
    g_render_ref_us = 0;
    g_render_sum_us = 0;
    g_render_max_us = 0;
    g_render_count = 0;
    g_render_us = 0;
    g_target.pixels = g_pixels;
    g_target.width = VIEW_WIDTH;
    g_target.height = VIEW_HEIGHT;
    palette_build();
    raycast_init_light();
    build_map();
    if (!pxa_window_fullscreen()) {
        return PXA_STATUS_INTERNAL;
    }
    if (!pxa_surface_create_rgb565(CREATE_REQUEST, VIEW_WIDTH, VIEW_HEIGHT, 3,
                                   g_packet, sizeof(g_packet))) {
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    uint64_t timestamp_us;
    if (!pxa_parse_event(event, length, &parsed)) {
        return PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_SERVICE_SURFACE &&
        parsed.opcode == PXA_SURFACE_CREATE &&
        parsed.request_id == CREATE_REQUEST) {
        pxa_surface_create_result_t result;
        if (!pxa_surface_parse_create(&parsed, &result) ||
            result.status != PXA_STATUS_OK ||
            result.frame_bytes != FRAME_BYTES) {
            return PXA_STATUS_INTERNAL;
        }
        g_surface_handle = result.surface_handle;
        return pxa_surface_configure_layer(
                   CONFIGURE_REQUEST, g_surface_handle, 0, 0, VIEW_WIDTH,
                   VIEW_HEIGHT, 0, 1, g_packet, sizeof(g_packet))
                   ? PXA_EVENT_HANDLED
                   : PXA_STATUS_INTERNAL;
    }
    if (parsed.service == PXA_SERVICE_SURFACE &&
        parsed.opcode == PXA_SURFACE_CONFIGURE_LAYER &&
        parsed.request_id == CONFIGURE_REQUEST) {
        if (parsed.payload_length != 4 ||
            (int32_t)pxa_read_u32(parsed.payload) != PXA_STATUS_OK) {
            return PXA_STATUS_INTERNAL;
        }
        render_scene();
        if (!submit_frame()) {
            return PXA_STATUS_INTERNAL;
        }
        return pxa_clock_set_period(FRAME_PERIOD_MS) ? PXA_EVENT_HANDLED
                                                     : PXA_STATUS_INTERNAL;
    }
    if (parsed.service == PXA_SERVICE_CLOCK &&
        parsed.opcode == PXA_CLOCK_NOW_RESULT) {
        int32_t status;
        uint64_t timestamp;
        if (pxa_clock_parse_now(&parsed, &status, &timestamp) &&
            status == PXA_STATUS_OK) {
            if (parsed.request_id == CLOCK_END_REQUEST &&
                g_render_ref_us != 0 && timestamp >= g_render_ref_us) {
                const uint64_t us = timestamp - g_render_ref_us;
                if (us < UINT64_C(2000000)) {
                    g_render_us = (uint32_t)us;
                    g_render_sum_us += us;
                    ++g_render_count;
                    if (us > g_render_max_us) {
                        g_render_max_us = us;
                    }
                }
            }
        }
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_SERVICE_SURFACE &&
        parsed.opcode == PXA_SURFACE_QUERY_STATE && g_query_pending) {
        pxa_surface_state_result_t result;
        g_query_pending = 0;
        if (!pxa_surface_parse_state(&parsed, &result) ||
            result.status != PXA_STATUS_OK) {
            return PXA_STATUS_INTERNAL;
        }
        if (g_sample_ts_us != 0 && g_query_sent_us > g_sample_ts_us &&
            result.presented_frames >= g_sample_presented) {
            const uint64_t elapsed_us = g_query_sent_us - g_sample_ts_us;
            uint64_t measured = (result.presented_frames -
                                 g_sample_presented) *
                                UINT64_C(10000000) / elapsed_us;
            if (measured > 99999u) {
                measured = 99999u;
            }
            g_fps_x10 = (uint32_t)measured;
        }
        g_sample_ts_us = g_query_sent_us;
        g_sample_presented = result.presented_frames;
        g_submitted = result.submitted_frames;
        g_presented = result.presented_frames;
        g_dropped = result.dropped_frames;
        g_free_buffers = result.free_buffers;
        return PXA_EVENT_HANDLED;
    }
    if (!pxa_clock_tick_timestamp_us(&parsed, &timestamp_us) ||
        g_surface_handle == 0) {
        return PXA_EVENT_UNHANDLED;
    }
    if (g_prev_tick_us != 0 && timestamp_us > g_prev_tick_us) {
        const uint64_t delta_us = timestamp_us - g_prev_tick_us;
        g_tick_sum_us += delta_us;
        ++g_tick_count;
        if (delta_us > g_tick_max_us) {
            g_tick_max_us = delta_us;
        }
    }
    g_prev_tick_us = timestamp_us;

    /* pxa_control is synchronous, so the Host stamps CLOCK_NOW while it is
     * handled inside this callback. The tick timestamp is taken just before
     * the tick event is posted, so the reply bounds event delivery plus the
     * Guest-side render (geometry + raster + overlay). Only one CLOCK_NOW per
     * tick: transient Host replies coalesce by service/opcode. */
    g_render_ref_us = timestamp_us;
    render_scene();
    (void)pxa_clock_now(CLOCK_END_REQUEST);
    if (!submit_frame()) {
        return PXA_STATUS_INTERNAL;
    }
    ++g_ticks_since_stats;
    if (g_ticks_since_stats >= STATS_TICKS && !g_query_pending) {
#if defined(MAZE_SPIKE_METRICS_LOG)
        log_metrics();
#endif
        g_ticks_since_stats = 0;
        g_tick_sum_us = 0;
        g_tick_count = 0;
        g_tick_max_us = 0;
        g_render_sum_us = 0;
        g_render_count = 0;
        g_render_max_us = 0;
        send_query(timestamp_us);
    }
    return PXA_EVENT_HANDLED;
}

void pxa_app_stop(uint32_t reason) {
    (void)reason;
    (void)pxa_clock_set_period(0);
    if (g_surface_handle != 0) {
        (void)pxa_close_handle(g_surface_handle);
        g_surface_handle = 0;
    }
}

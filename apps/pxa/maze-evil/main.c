/* Maze Evil: full port of micropixel's maze-evil to PXA.
 *
 * The raycaster and the micropixel Host raster kernels run in the Guest and
 * write canonical RGB565 frames into a static buffer submitted through the PXA
 * Surface service. Input is the PXA pointer stream over a full-size transparent
 * Canvas node; audio is a Guest-side port of the Host tone synth writing 16 kHz
 * mono PCM to the PXA audio sink. The Ogg Opus BGM and the IMU controls have no
 * PXA equivalent in this product and are not ported.
 */
#include <stdint.h>

#include "assets.h"
#include "audio.h"
#include "font.h"
#include "input.h"
#include "palette.h"
#include "pxa.h"
#include "pxa_canvas.h"
#include "pxa_surface.h"
#include "raycast.h"
#include "render.h"
#include "world.h"

#define VIEW_WIDTH 296
#define VIEW_HEIGHT 240
#define FRAME_BYTES (VIEW_WIDTH * VIEW_HEIGHT * 2u)
#define FRAME_PERIOD_MS 33u
#define STATS_TICKS 30u
#define POINTER_NODE 2u
#define CREATE_REQUEST UINT32_C(1)
#define CONFIGURE_REQUEST UINT32_C(2)
#define QUERY_REQUEST UINT32_C(100)
#define CLOCK_END_REQUEST UINT32_C(201)

static uint16_t g_pixels[VIEW_WIDTH * VIEW_HEIGHT];
static target_t g_target;
static uint8_t g_packet[128];
static uint8_t g_canvas_buffer[64];
static uint8_t g_canvas_packet[128];
static uint32_t g_canvas_generation;
static uint8_t g_canvas_initialized;
static uint32_t g_surface_handle;
static uint64_t g_frame_id;
static uint64_t g_prev_tick_us;
static uint64_t g_now_us;
static int g_started;
static int g_start_touch_down;
static uint32_t g_start_touch_id;

static world_t g_world;
static renderer_t g_renderer;
static touch_controls_t g_touch;
static game_audio_t g_audio;
static hud_stats_t g_hud;

static uint64_t g_tick_sum_us;
static uint32_t g_tick_count;
static uint64_t g_tick_max_us;
static uint32_t g_ticks_since_stats;
static uint32_t g_query_pending;
static uint64_t g_query_sent_us;
static uint64_t g_sample_ts_us;
static uint64_t g_sample_presented;
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

static void pump_sounds(void) {
    sound_event_t sounds[MAX_PENDING_SOUNDS];
    const int count = world_take_sounds(&g_world, sounds, MAX_PENDING_SOUNDS);
    int index;
    for (index = 0; index < count; ++index) {
        audio_play(&g_audio, sounds[index].id, sounds[index].gain);
    }
}

static int submit_frame(void) {
    int32_t status = pxa_surface_write_frame(
        g_surface_handle, (uint8_t *)g_pixels, FRAME_BYTES);
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

static void render_frame(void) {
    if (g_started) {
        renderer_render(&g_renderer, &g_world, &g_hud, &g_target);
        if (g_touch.stick.down) {
            renderer_draw_stick(&g_renderer, &g_target, 1,
                                g_touch.stick.origin_x, g_touch.stick.origin_y,
                                g_touch.stick.x, g_touch.stick.y);
        }
    } else {
        hud_stats_t hidden = g_hud;
        hidden.visible = 0;
        renderer_render(&g_renderer, &g_world, &hidden, &g_target);
        renderer_draw_instructions(&g_renderer, &g_target);
    }
}

static void update_stats_from_state(const pxa_surface_state_result_t *state) {
    if (g_sample_ts_us != 0 && g_query_sent_us > g_sample_ts_us &&
        state->presented_frames >= g_sample_presented) {
        const uint64_t elapsed_us = g_query_sent_us - g_sample_ts_us;
        uint64_t measured =
            (state->presented_frames - g_sample_presented) *
            UINT64_C(10000000) / elapsed_us;
        if (measured > 99999u) {
            measured = 99999u;
        }
        g_hud.fps = (uint32_t)measured / 10u;
    }
    g_sample_ts_us = g_query_sent_us;
    g_sample_presented = state->presented_frames;
    g_submitted = state->submitted_frames;
    g_presented = state->presented_frames;
    g_dropped = state->dropped_frames;
    g_free_buffers = state->free_buffers;
}

#if defined(MAZE_EVIL_METRICS_LOG)
/* The Guest SDK has no logging import. The Host logs any control message it
 * rejects at error level, so the port emits metrics as unknown opcodes on the
 * Core service and reads them back with `pxadb logcat`. */
static void emit_metric(uint16_t id, uint32_t value) {
    uint8_t message[12];
    pxa_writer_t writer;
    pxa_writer_init(&writer, message, sizeof(message));
    if (pxa_put_u16(&writer, PXA_SERVICE_CORE) &&
        pxa_put_u16(&writer, (uint16_t)(0x9100u + id)) &&
        pxa_put_u32(&writer, value) && pxa_put_u32(&writer, 0)) {
        (void)pxa_control(message, (uint32_t)writer.length);
    }
}

static void log_metrics(void) {
    emit_metric(1, g_hud.fps);
    emit_metric(2, (uint32_t)(g_render_count == 0
                                  ? 0
                                  : g_render_sum_us / g_render_count));
    emit_metric(3, (uint32_t)g_render_max_us);
    emit_metric(4, g_tick_count == 0
                       ? 0
                       : (uint32_t)(g_tick_sum_us / g_tick_count / 1000u));
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

static int setup_pointer_node(void) {
    pxa_canvas_frame_t frame;
    pxa_canvas_begin(&frame, g_canvas_buffer, sizeof(g_canvas_buffer));
    return pxa_canvas_present_with_root_event_mask(
        POINTER_NODE, &frame, &g_canvas_generation, &g_canvas_initialized,
        PXA_UI_EVENT_MASK_POINTER, g_canvas_packet, sizeof(g_canvas_packet),
        g_packet, sizeof(g_packet));
}

int32_t pxa_app_start(const uint8_t *config, uint32_t length) {
    (void)config;
    (void)length;
    g_target.pixels = g_pixels;
    g_target.width = VIEW_WIDTH;
    g_target.height = VIEW_HEIGHT;
    palette_build();
    raycast_init_light();
    font_build_atlas();
    world_reset(&g_world);
    renderer_init(&g_renderer, VIEW_WIDTH, VIEW_HEIGHT, 1);
    touch_init(&g_touch, VIEW_WIDTH);
    audio_init(&g_audio);
    g_hud.visible = 1;
    g_hud.show_perf = 1;
    if (!setup_pointer_node() || !pxa_window_fullscreen()) {
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
    if (audio_handle_event(&g_audio, &parsed, g_packet, sizeof(g_packet))) {
        return PXA_EVENT_HANDLED;
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
        render_frame();
        if (!submit_frame()) {
            return PXA_STATUS_INTERNAL;
        }
        return pxa_clock_set_period(FRAME_PERIOD_MS) ? PXA_EVENT_HANDLED
                                                     : PXA_STATUS_INTERNAL;
    }
    if (parsed.service == PXA_SERVICE_UI && parsed.opcode == PXA_UI_EVENT) {
        pxa_ui_pointer_data_t pointer;
        if (!pxa_ui_parse_pointer(&parsed, &pointer) ||
            (pointer.node != POINTER_NODE && pointer.node != 1u)) {
            return PXA_EVENT_UNHANDLED;
        }
        if (!g_started) {
            if (pointer.phase == PXA_POINTER_DOWN) {
                g_start_touch_down = 1;
                g_start_touch_id = pointer.pointer_id;
            } else if (g_start_touch_down &&
                       pointer.pointer_id == g_start_touch_id &&
                       (pointer.phase == PXA_POINTER_UP ||
                        pointer.phase == PXA_POINTER_CANCEL)) {
                g_start_touch_down = 0;
                if (pointer.phase == PXA_POINTER_UP) {
                    g_started = 1;
                    g_prev_tick_us = 0;
                    g_tick_sum_us = 0;
                    g_tick_count = 0;
                    g_tick_max_us = 0;
                    g_render_sum_us = 0;
                    g_render_max_us = 0;
                    g_render_count = 0;
                }
            }
            return PXA_EVENT_HANDLED;
        }
        if (pointer.phase == PXA_POINTER_DOWN) {
            touch_on_down(&g_touch, pointer.pointer_id, pointer.x, pointer.y,
                          pointer.timestamp_us);
        } else if (pointer.phase == PXA_POINTER_MOVE) {
            touch_on_move(&g_touch, pointer.pointer_id, pointer.x, pointer.y,
                          pointer.timestamp_us);
        } else {
            touch_on_up(&g_touch, pointer.pointer_id, pointer.timestamp_us);
        }
        return PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_SERVICE_CLOCK &&
        parsed.opcode == PXA_CLOCK_NOW_RESULT) {
        int32_t status;
        uint64_t timestamp;
        if (pxa_clock_parse_now(&parsed, &status, &timestamp) &&
            status == PXA_STATUS_OK &&
            parsed.request_id == CLOCK_END_REQUEST && g_render_ref_us != 0 &&
            timestamp >= g_render_ref_us) {
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
        update_stats_from_state(&result);
        return PXA_EVENT_HANDLED;
    }
    if (!pxa_clock_tick_timestamp_us(&parsed, &timestamp_us) ||
        g_surface_handle == 0) {
        return PXA_EVENT_UNHANDLED;
    }

    {
        uint64_t dt_us = 0;
        float dt;
        if (g_prev_tick_us != 0 && timestamp_us > g_prev_tick_us) {
            const uint64_t delta_us = timestamp_us - g_prev_tick_us;
            dt_us = delta_us;
            g_tick_sum_us += delta_us;
            ++g_tick_count;
            if (delta_us > g_tick_max_us) {
                g_tick_max_us = delta_us;
            }
        }
        g_prev_tick_us = timestamp_us;
        g_now_us = timestamp_us;
        if (dt_us > 50000u) {
            dt_us = 50000u;
        }
        dt = (float)dt_us * 1e-6F;

        if (g_started) {
            const controls_t controls = touch_consume(&g_touch, timestamp_us);
            const uint8_t phase_before = g_world.phase;
            world_update(&g_world, dt, &controls);
            if (phase_before != PHASE_PLAYING &&
                g_world.phase == PHASE_PLAYING) {
                /* Win and death retries return to the start screen. */
                g_started = 0;
                g_start_touch_down = 0;
                touch_init(&g_touch, VIEW_WIDTH);
            }
            pump_sounds();
        }
        audio_tick(&g_audio, &parsed);

        /* pxa_control is synchronous, so the Host stamps CLOCK_NOW while it
         * is handled inside this callback. The tick timestamp is taken just
         * before the tick event is posted, so the reply bounds event delivery
         * plus the Guest-side render. One CLOCK_NOW per tick: transient Host
         * replies coalesce by service/opcode. */
        g_render_ref_us = timestamp_us;
        render_frame();
        (void)pxa_clock_now(CLOCK_END_REQUEST);
        if (!submit_frame()) {
            return PXA_STATUS_INTERNAL;
        }
    }

    ++g_ticks_since_stats;
    if (g_ticks_since_stats >= STATS_TICKS && !g_query_pending) {
        g_hud.render_ms_x10 =
            (uint32_t)(g_render_count == 0
                           ? 0
                           : g_render_sum_us / g_render_count / 100u);
        g_hud.tick_avg_ms =
            g_tick_count == 0
                ? 0
                : (uint32_t)(g_tick_sum_us / g_tick_count / 1000u);
        g_hud.tick_max_ms = (uint32_t)(g_tick_max_us / 1000u);
#if defined(MAZE_EVIL_METRICS_LOG)
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
    audio_stop(&g_audio);
    if (g_surface_handle != 0) {
        (void)pxa_close_handle(g_surface_handle);
        g_surface_handle = 0;
    }
}

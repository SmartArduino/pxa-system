#undef NDEBUG
#define PXSYS_PRODUCT_RUNNER_LIBRARY 1
#include "../product_runner.c"
#include "../../../libpxa/tests/raster_snapshot_scenario.h"

static lv_obj_t *guest_ui, *system_dialog;
static lv_indev_t *test_pointer;
static lv_indev_state_t test_pointer_state;
static unsigned guest_presses, dialog_presses;

static void read_test_pointer(lv_indev_t *input, lv_indev_data_t *data) {
    (void)input;
    data->point.x = data->point.y = 2;
    data->state = test_pointer_state;
}

static void count_press(lv_event_t *event) {
    ++*(unsigned *)lv_event_get_user_data(event);
}

static void press_test_pointer(void) {
    test_pointer_state = LV_INDEV_STATE_PRESSED;
    lv_tick_inc(30);
    lv_indev_read(test_pointer);
    test_pointer_state = LV_INDEV_STATE_RELEASED;
    lv_tick_inc(30);
    lv_indev_read(test_pointer);
}

static lv_obj_t *opaque_input(lv_obj_t *parent, unsigned *presses) {
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, 0, 0);
    lv_obj_set_size(object, 4, 4);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(object, false);
    lv_obj_add_event_cb(object, count_press, LV_EVENT_PRESSED, presses);
    return object;
}

static void present_snapshot(void *context, uint64_t id, uint16_t color) {
    product_host_t *host = context;
    (void)id;
    assert(surface_process_pending(host));
    for (unsigned i = 0; i < 16; ++i)
        assert(((const uint16_t *)host->surface_display_buffer)[i] == color);
    assert(lv_obj_get_parent(host->surface_image) == host->content_parent);
    assert(lv_obj_get_index(host->surface_image) > lv_obj_get_index(guest_ui));
    assert(!lv_obj_is_clickable(host->surface_image));
    /* A visibility/configure update must not put a paused game back behind
     * the opaque Guest root while no new frame arrives. */
    pxa_surface_layer_t layer = host->surface_layer;
    assert(surface_configure(host, 1, &layer) == PXA_STATUS_OK);
    assert(lv_obj_get_index(host->surface_image) > lv_obj_get_index(guest_ui));
    if (guest_presses == 0) {
        lv_obj_update_layout(lv_display_get_screen_active(host->display));
        press_test_pointer();
        assert(guest_presses == 1 && dialog_presses == 0);
        lv_obj_set_hidden(system_dialog, false);
        lv_obj_update_layout(lv_display_get_layer_top(host->display));
        press_test_pointer();
        assert(guest_presses == 1 && dialog_presses == 1);
        lv_obj_set_hidden(system_dialog, true);
    }
}

static void borrowed_scenario(product_host_t *host,
                               const pxa_game_render_backend_t *backend) {
    const pxa_game_render_desc_t desc = {
        4, 4, 2, PXA_GAME_RENDER_FLAG_BORROW_COMMANDS,
        PXA_GAME_RENDER_SCRATCH_DEPTH16, 4096};
    uint64_t surface;
    uint32_t capabilities;
    uint8_t draw[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_CLEAR_BYTES] = {0};
    uint8_t *record = draw + PXA_RASTER_DRAW_HEADER_BYTES;
    assert(backend->create(host, &desc, &surface, &capabilities) == 0);
    assert(host->raster_depth_buffer != NULL);
    assert(host->raster_draw_list == NULL);
    pxa_write_u32(draw, PXA_RASTER_DRAW_MAGIC);
    pxa_write_u16(draw + 4, PXA_RASTER_ABI_MAJOR);
    pxa_write_u16(draw + 6, PXA_RASTER_ABI_MINOR);
    pxa_write_u32(draw + 8, sizeof(draw));
    pxa_write_u32(draw + 16, 1);
    pxa_write_u64(draw + 20, 1);
    record[0] = PXA_RASTER_RECORD_CLEAR_RGB565;
    pxa_write_u16(record + 2, PXA_RASTER_CLEAR_BYTES);
    pxa_write_u16(record + 4, 0x1234);

    host->focused = 0;
    assert(backend->submit(host, surface, draw, sizeof(draw)) == PXA_STATUS_WOULD_BLOCK);
    host->focused = 1;
    host->surface_layer.visible = 0;
    assert(backend->submit(host, surface, draw, sizeof(draw)) == PXA_STATUS_WOULD_BLOCK);
    host->surface_layer.visible = 1;
    assert(host->raster_last_frame_id == 0);
    assert(backend->submit(host, surface, draw, sizeof(draw)) == 0);
    assert(host->raster_telemetry.rendered_frames == 1);
    assert(host->raster_telemetry.visible_frames == 0);
    pxa_write_u64(draw + 20, 2);
    pxa_write_u16(record + 4, 0x5678);
    assert(backend->submit(host, surface, draw, sizeof(draw)) == PXA_STATUS_WOULD_BLOCK);
    assert(host->raster_last_frame_id == 1);
    host->focused = 0;
    assert(!surface_process_pending(host));
    host->focused = 1;
    present_snapshot(host, 1, 0x1234); // mutated Guest bytes must not change the frame
    assert(!surface_process_pending(host)); // present exactly once
    record[0] = 255;
    assert(backend->submit(host, surface, draw, sizeof(draw)) == PXA_STATUS_UNSUPPORTED);
    assert(host->raster_last_frame_id == 1);
    record[0] = PXA_RASTER_RECORD_CLEAR_RGB565;
    assert(backend->submit(host, surface, draw, sizeof(draw)) == 0); // same ID retry
    uint8_t next_draw[sizeof(draw)];
    memcpy(next_draw, draw, sizeof(draw));
    memset(draw, 0, sizeof(draw));
    present_snapshot(host, 2, 0x5678);
    assert(host->raster_telemetry.rendered_frames == 2 &&
           host->raster_telemetry.visible_frames == 2 &&
           host->raster_telemetry.submitted_frames == 2 &&
           host->raster_telemetry.dropped_frames == 0);
    backend->close(host, surface);
    assert(host->raster_buffers[0] == NULL && host->raster_depth_buffer == NULL);
    // Closing a completed, not yet presented frame retains no Guest storage.
    assert(backend->create(host, &desc, &surface, &capabilities) == 0);
    pxa_write_u64(next_draw + 20, 1);
    assert(backend->submit(host, surface, next_draw, sizeof(next_draw)) == 0);
    backend->close(host, surface);
}

int main(void) {
    product_host_t *host = calloc(1, sizeof(*host));
    pxa_game_render_backend_t backend = {0};
    assert(host != NULL);
    host->focused = 1;
    assert(resource_memory_init(host, 128 * 1024, 2 * 1024 * 1024, 16 * 1024, 512 * 1024) == 0);
    lv_init();
    host->display = lv_display_create(4, 4);
    assert(host->display != NULL);
    host->width = host->height = 4;
    host->content_parent = lv_display_get_screen_active(host->display);
    guest_ui = opaque_input(host->content_parent, &guest_presses);
    system_dialog = opaque_input(lv_display_get_layer_top(host->display),
                                 &dialog_presses);
    lv_obj_set_hidden(system_dialog, true);
    test_pointer = lv_indev_create();
    lv_indev_set_type(test_pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(test_pointer, host->display);
    lv_indev_set_read_cb(test_pointer, read_test_pointer);
    lv_indev_set_mode(test_pointer, LV_INDEV_MODE_EVENT);
    backend.context = host;
    backend.create = game_render_create;
    backend.upload = surface_raster_upload;
    backend.submit = surface_raster_submit;
    backend.bind_assets = surface_raster_bind_assets;
    backend.close = surface_close;
    borrowed_scenario(host, &backend);
    raster_snapshot_scenario(&backend, present_snapshot, host);
    assert(host->raster_bindings.palette == NULL);
    assert(host->raster_frame_bindings.palette == NULL);
    /* Guest-mapped Surface keeps the existing UI-on-top composition policy. */
    pxa_surface_desc_t mapped = {0};
    mapped.width = mapped.height = 4;
    mapped.buffer_count = 2;
    mapped.format = PXA_SURFACE_FORMAT_RGB565;
    mapped.flags = PXA_SURFACE_FLAG_GUEST_MAPPED;
    uint64_t mapped_surface = 0;
    uint32_t mapped_stride = 0;
    uint16_t mapped_pixels[32] = {0};
    assert(surface_create(host, &mapped, &mapped_surface, &mapped_stride) == PXA_STATUS_OK);
    assert(surface_register_buffers(host, mapped_surface, (uint8_t *)mapped_pixels,
                                    sizeof(mapped_pixels)) == PXA_STATUS_OK);
    assert(lv_obj_get_index(host->surface_image) < lv_obj_get_index(guest_ui));
    surface_close(host, mapped_surface);
    assert(host->surface_image == NULL);
    lv_indev_delete(test_pointer);
    lv_display_delete(host->display);
    assert(resource_memory_end(host) == 0);
    free(host);
    puts("desktop raster: queued snapshots and synchronous borrowed lifetime/backpressure passed");
    return 0;
}

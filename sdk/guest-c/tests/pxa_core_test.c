#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "pxa_ui.h"
#include "pxa_canvas.h"
#include "pxa_raster.h"
#include "pxa_store_installer.h"
#include "pxa.h"
#include "pxa_device.h"
#include "pxa_game_render.h"
#include "pxa_pending.h"
#include "pxa_window.h"
#include "pxa_permission.h"
#include "pxa_storage.h"
#include "pxa_fs.h"
#include "pxa_ipc.h"

_Static_assert(sizeof(pxa_canvas_handle_t) == 8, "Canvas uses 64-bit handles");
_Static_assert(sizeof(pxa_raster_handle_t) == 8, "Raster uses 64-bit handles");

static uint8_t submitted[277];
static uint32_t submitted_size;
static unsigned submit_calls;
static int32_t next_submit_status;

int32_t pxa_submit(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(submitted));
    memcpy(submitted, data, length);
    submitted_size = length;
    ++submit_calls;
    return next_submit_status;
}

int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t *data,
                  uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return -3;
}

int main(void) {
    uint8_t packet[32];
    uint8_t event_packet[80];
    pxa_event_t event;
    uint64_t token_cursor = 0;
    uint8_t lifecycle_state = 255;
    pxa_device_runtime_info_t info;
    uint32_t size = 9;
    const uint8_t payload[] = {0xaa, 0xbb};
    assert(PXA_CORE_VERSION_MAJOR == 1u);
    assert(pxa_next_token(&token_cursor) == 1u);
    assert(pxa_next_token(&token_cursor) == 2u);
    token_cursor = UINT64_MAX;
    assert(pxa_next_token(&token_cursor) == 1u);
    assert(pxa_next_token(NULL) == 0u);
    event.service = PXA_SERVICE_SYSTEM;
    event.opcode = PXA_SYSTEM_LIFECYCLE_EVENT;
    event.token = 0;
    event.payload = (const uint8_t[]){PXA_SYSTEM_LIFECYCLE_BACKGROUND};
    event.payload_size = 1;
    assert(pxa_lifecycle_parse(&event, &lifecycle_state));
    assert(lifecycle_state == PXA_SYSTEM_LIFECYCLE_BACKGROUND);
    event.token = 1;
    assert(!pxa_lifecycle_parse(&event, &lifecycle_state));
    assert(pxa_build_message(packet, sizeof(packet), 19, 1,
                                UINT64_C(0x0123456789abcdef), payload,
                                sizeof(payload), &size));
    assert(size == 22);
    assert(memcmp(packet,
                  (const uint8_t[]){
                      19, 0, 1, 0, 0xef, 0xcd, 0xab, 0x89,
                      0x67, 0x45, 0x23, 0x01, 2, 0, 0, 0,
                      0, 0, 0, 0, 0xaa, 0xbb},
                  size) == 0);
    {
        uint8_t in_place[22] = {0};
        uint32_t in_place_size = 0;
        in_place[20] = payload[0];
        in_place[21] = payload[1];
        assert(pxa_build_message(in_place, sizeof(in_place), 19, 1,
                                    UINT64_C(0x0123456789abcdef),
                                    in_place + PXA_HEADER_BYTES,
                                    sizeof(payload), &in_place_size));
        assert(in_place_size == size && memcmp(in_place, packet, size) == 0);
        assert(!pxa_build_message(in_place, sizeof(in_place) - 1u, 19, 1,
                                     1, in_place + PXA_HEADER_BYTES,
                                     sizeof(payload), &in_place_size));
        assert(in_place_size == 0);
        assert(!pxa_build_message(in_place, sizeof(in_place), 19, 1,
                                     1, in_place + PXA_HEADER_BYTES,
                                     SIZE_MAX, &in_place_size));
    }
    assert(pxa_parse_event(packet, size, &event));
    assert(event.service == 19 && event.opcode == 1 &&
           event.token == UINT64_C(0x0123456789abcdef) &&
           event.payload_size == 2 && event.payload[0] == 0xaa);
    assert(!pxa_parse_event(packet, size - 1, &event));
    assert(event.payload == NULL);
    packet[16] = 1;
    assert(!pxa_parse_event(packet, size, &event));
    packet[16] = 0;
    assert(!pxa_build_message(packet, 21, 19, 1, 0, payload, 2,
                                 &size));
    assert(size == 0);
    assert(!pxa_build_message(packet, sizeof(packet), 0, 1, 0,
                                 payload, 2, &size));
    assert(pxa_log_write(2, "v1") == 0);
    assert(submit_calls == 1 && submitted_size == 23);
    assert(submitted[0] == 19 && submitted[2] == 1 &&
           submitted[12] == 3 && submitted[20] == 2 &&
           submitted[21] == 'v' && submitted[22] == '1');
    assert(pxa_log_write(5, "invalid") != 0 && submit_calls == 1);
    assert(pxa_device_request_runtime_info(
               UINT64_C(0x123456789abcdef0)) == 0);
    assert(submit_calls == 2 && submitted_size == 20 &&
           submitted[0] == 15 && submitted[2] == 2 &&
           submitted[4] == 0xf0 && submitted[11] == 0x12);
    {
        static const uint8_t runtime_info[] = {
            0, 0, 0, 0,
            1, 0, 5, 0, 'l', 'i', 'n', 'u', 'x',
            2, 0, 6, 0, 'x', '8', '6', '_', '6', '4',
            3, 0, 4, 0, 'w', 'a', 'm', 'r',
            4, 0, 3, 0, 'a', 'b', 'i',
            5, 0, 4, 0, 3, 0, 0, 0,
        };
        assert(pxa_build_message(
            event_packet, sizeof(event_packet), PXA_DEVICE_SERVICE,
            PXA_DEVICE_GET_RUNTIME_INFO,
            UINT64_C(0x123456789abcdef0), runtime_info,
            sizeof(runtime_info), &size));
        assert(pxa_parse_event(event_packet, size, &event));
        assert(pxa_device_parse_runtime_info(
            &event, UINT64_C(0x123456789abcdef0), &info));
        assert(info.status == 0 && strcmp(info.target, "linux") == 0 &&
               strcmp(info.architecture, "x86_64") == 0 &&
               info.formats == 3);
        assert(!pxa_device_parse_runtime_info(&event, 1, &info));
        event.payload_size--;
        assert(!pxa_device_parse_runtime_info(
            &event, UINT64_C(0x123456789abcdef0), &info));
    }
    {
        uint8_t result[4 + PXA_WINDOW_SNAPSHOT_RECORD_BYTES] = {0};
        uint8_t packet_window[PXA_HEADER_BYTES + sizeof(result)];
        uint8_t fields[8][16] = {{0}};
        const size_t lengths[8] = {8, 8, 8, 8, 16, 16, 1, 1};
        pxa_window_snapshot_t snapshot;
        size_t offset = 4;
        size_t written = 0;
        size_t orientation_offset = 0;
        assert(pxa_window_build_snapshot(packet, sizeof(packet),
                                            UINT64_C(0x100000002), &size));
        assert(size == PXA_HEADER_BYTES && packet[0] == 2 &&
               packet[2] == 2);
        pxa_store_u64(fields[0], 7);
        pxa_store_u32(fields[1], 296);
        pxa_store_u32(fields[1] + 4, 240);
        pxa_store_u32(fields[2], 296);
        pxa_store_u32(fields[2] + 4, 240);
        pxa_store_u32(fields[3], 1);
        pxa_store_u32(fields[3] + 4, 1);
        pxa_store_u32(fields[4], 4);
        fields[6][0] = 1;
        fields[7][0] = 1;
        for (uint16_t tag = 1; tag <= 8; ++tag) {
            if (tag == 7) orientation_offset = offset + 4;
            assert(pxa_wire_record_encode(result + offset,
                                          sizeof(result) - offset, tag,
                                          fields[tag - 1], lengths[tag - 1],
                                          &written));
            offset += written;
        }
        assert(offset == sizeof(result));
        assert(pxa_build_message(packet_window, sizeof(packet_window),
                                    PXA_WINDOW_SERVICE,
                                    PXA_WINDOW_GET_SNAPSHOT,
                                    UINT64_C(0x100000002), result,
                                    sizeof(result), &size));
        assert(pxa_parse_event(packet_window, size, &event));
        assert(pxa_window_parse_snapshot(
            &event, UINT64_C(0x100000002), &snapshot));
        assert(snapshot.status == 0 && snapshot.revision == 7 &&
               snapshot.logical_width == 296 &&
               snapshot.safe_insets.left == 4 && snapshot.focused == 1);
        assert(!pxa_window_parse_snapshot(&event, 1, &snapshot));
        event.payload_size--;
        assert(!pxa_window_parse_snapshot(
            &event, UINT64_C(0x100000002), &snapshot));
        event.payload_size++;
        packet_window[PXA_HEADER_BYTES + orientation_offset] = 3;
        assert(!pxa_window_parse_snapshot(
            &event, UINT64_C(0x100000002), &snapshot));
        packet_window[PXA_HEADER_BYTES + orientation_offset] = 1;
        packet_window[PXA_HEADER_BYTES + 4] = 2;
        assert(!pxa_window_parse_snapshot(
            &event, UINT64_C(0x100000002), &snapshot));
        packet_window[PXA_HEADER_BYTES + 4] = 1;
        event.opcode = PXA_WINDOW_METRICS_CHANGED;
        event.token = 0;
        event.payload += 4;
        event.payload_size -= 4;
        assert(pxa_window_parse_metrics_changed(&event, &snapshot));
        assert(snapshot.revision == 7 && snapshot.logical_width == 296);
        event.payload_size--;
        assert(!pxa_window_parse_metrics_changed(&event, &snapshot));
        event.opcode = PXA_WINDOW_BACK_REQUESTED;
        event.payload_size = 0;
        assert(pxa_window_is_back_requested(&event));
        event.token = 1;
        assert(!pxa_window_is_back_requested(&event));
    }
    {
        uint8_t window_packet[PXA_WINDOW_MAX_CONFIG_PACKET];
        uint8_t toast_packet[PXA_WINDOW_MAX_TOAST_PACKET];
        pxa_window_config_t config = {0};
        config.status_bar_mode = 1;
        config.navigation_bar_color = UINT32_C(0xff112233);
        assert(pxa_window_build_configure(window_packet,
                                              sizeof(window_packet),
                                              &config, &size));
        assert(size == sizeof(window_packet) && window_packet[0] == 2 &&
               window_packet[2] == 1 && window_packet[12] == 41 &&
               window_packet[20] == 1 && window_packet[25] == 2 &&
               window_packet[30] == 3 && window_packet[45] == 6 &&
               window_packet[53] == 7 && window_packet[57] == 0x33);
        config.edge_to_edge = 2;
        assert(!pxa_window_build_configure(window_packet,
                                               sizeof(window_packet),
                                               &config, &size));
        assert(size == 0);
        assert(pxa_window_build_toast(toast_packet,
                                          sizeof(toast_packet), 500,
                                          "hello", 5, &size));
        assert(size == 27 && toast_packet[0] == 2 &&
               toast_packet[2] == 3 && toast_packet[20] == 0xf4 &&
               toast_packet[21] == 1 && toast_packet[22] == 'h');
        assert(!pxa_window_build_toast(toast_packet,
                                           sizeof(toast_packet), 499,
                                           "hello", 5, &size));
    }
    {
        const pxa_game_render_options_t options = {
            .width = 32, .height = 32, .buffer_count = 2,
            .scratch_mode = PXA_GAME_RENDER_SCRATCH_NONE,
            .max_draw_bytes = 64};
        static const uint8_t create_result[24] = {
            0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0,
            3, 0, 0, 0, 64, 0, 0, 0, 128, 0, 48, 0};
        pxa_game_render_create_result_t render;
        pxa_game_render_telemetry_t telemetry = {0};
        assert(pxa_game_render_map_coord(400, 800, 400) == 200);
        assert(pxa_game_render_map_coord(-1, 800, 400) == -1);
        assert(pxa_game_render_map_coord(1, 0, 400) == 0);
        assert(pxa_game_render_build_create(
            packet, sizeof(packet), UINT64_C(0x100000001), &options,
            &size));
        assert(size == 32 && packet[0] == 18 && packet[2] == 1 &&
               packet[20] == 32 && packet[24] == 2 && packet[27] == 1 &&
               packet[28] == 64);
        assert(pxa_build_message(
            event_packet, sizeof(event_packet), PXA_GAME_RENDER_SERVICE,
            PXA_GAME_RENDER_CREATE, UINT64_C(0x100000001),
            create_result, sizeof(create_result), &size));
        assert(pxa_parse_event(event_packet, size, &event));
        assert(pxa_game_render_parse_create(
            &event, UINT64_C(0x100000001), &render));
        assert(render.status == 0 &&
               render.handle == UINT64_C(0x200000001) &&
               render.capabilities == 3 && render.max_draw_bytes == 64 &&
               render.max_texture_dimension == 128 &&
               render.max_textures == 48);
        event.payload_size--;
        assert(!pxa_game_render_parse_create(
            &event, UINT64_C(0x100000001), &render));
        assert(pxa_game_render_close(UINT64_C(0x200000001)) == 0);
        assert(submit_calls == 3 && submitted_size == 28 &&
               submitted[0] == 1 && submitted[2] == 2 &&
               submitted[20] == 1 && submitted[24] == 2);
        assert(pxa_game_render_query_telemetry(
                   UINT64_C(0x200000001), &telemetry) == -3);
        assert(pxa_game_render_upload(
                   UINT64_C(0x200000001), packet, 1) == -3);
        assert(pxa_game_render_submit(
                   UINT64_C(0x200000001), packet, 1) == -3);
        assert(pxa_game_render_submit(0, packet, 1) == -1);
    }
    {
        pxa_pending_entry_t entries[2];
        pxa_pending_table_t pending = {0};
        int first = 1;
        int second = 2;
        void *user = NULL;
        unsigned calls = submit_calls;
        assert(pxa_pending_init(&pending, entries, 2));
        assert(pxa_build_message(packet, sizeof(packet), 15, 2, 100,
                                    NULL, 0, &size));
        assert(pxa_send_tracked(&pending, packet, size, &first) == 0);
        assert(pxa_send_tracked(&pending, packet, size, &first) == -6);
        assert(submit_calls == calls + 1);
        assert(pxa_parse_event(packet, size, &event));
        event.opcode = 3;
        assert(!pxa_pending_take(&pending, &event, &user));
        event.opcode = 2;
        assert(pxa_pending_take(&pending, &event, &user));
        assert(user == &first && entries[0].token == 0);
        assert(!pxa_pending_take(&pending, &event, &user));
        assert(user == NULL);
        assert(pxa_build_message(packet, sizeof(packet), 15, 2, 101,
                                    NULL, 0, &size));
        assert(pxa_send_tracked(&pending, packet, size, &first) == 0);
        assert(pxa_build_message(packet, sizeof(packet), 15, 2, 102,
                                    NULL, 0, &size));
        assert(pxa_send_tracked(&pending, packet, size, &second) == 0);
        assert(pxa_build_message(packet, sizeof(packet), 15, 2, 103,
                                    NULL, 0, &size));
        assert(pxa_send_tracked(&pending, packet, size, NULL) == -9);
        assert(pxa_parse_event(packet, size, &event));
        event.token = 101;
        assert(pxa_pending_take(&pending, &event, &user) && user == &first);
        next_submit_status = -3;
        assert(pxa_send_tracked(&pending, packet, size, NULL) == -3);
        next_submit_status = 0;
        assert(pxa_send_tracked(&pending, packet, size, NULL) == 0);
        event.token = 103;
        assert(pxa_pending_take(&pending, &event, &user) && user == NULL);
        event.token = 102;
        assert(pxa_pending_take(&pending, &event, &user) && user == &second);
        assert(pxa_build_message(packet, sizeof(packet), 15, 2, 0,
                                    NULL, 0, &size));
        assert(pxa_send_tracked(&pending, packet, size, NULL) == -1);
        packet[16] = 1;
        assert(pxa_send_tracked(&pending, packet, size, NULL) == -1);
    }
    {
        unsigned calls = submit_calls;
        assert(pxa_cancel(0) == -1 && submit_calls == calls);
        assert(pxa_cancel(UINT64_C(0x123456789abcdef0)) == 0);
        assert(submit_calls == calls + 1 && submitted_size == 28 &&
               submitted[0] == PXA_CORE_SERVICE &&
               submitted[2] == PXA_CORE_CANCEL_REQUEST &&
               submitted[4] == 0 && submitted[12] == 8 &&
               submitted[20] == 0xf0 && submitted[27] == 0x12);
    }
    {
        uint8_t permission_packet[64];
        uint8_t permission_result[12] = {0};
        uint8_t event_bytes[48];
        pxa_permission_acquire_result_t acquired;
        pxa_permission_check_result_t checked;
        pxa_permission_revoked_t revoked;
        assert(pxa_permission_build(
            permission_packet, sizeof(permission_packet),
            PXA_PERMISSION_ACQUIRE, UINT64_C(0x100000010),
            "fs.private", 10, NULL, 0, &size));
        assert(size == 34 && permission_packet[0] == 11 &&
               permission_packet[2] == 2 && permission_packet[20] == 1 &&
               permission_packet[22] == 10);
        pxa_store_u64(permission_result + 4, UINT64_C(0x200000001));
        assert(pxa_build_message(
            event_bytes, sizeof(event_bytes), PXA_PERMISSION_SERVICE,
            PXA_PERMISSION_ACQUIRE, UINT64_C(0x100000010),
            permission_result, sizeof(permission_result), &size));
        assert(pxa_parse_event(event_bytes, size, &event));
        assert(pxa_permission_parse_acquire(
            &event, UINT64_C(0x100000010), &acquired));
        assert(acquired.status == 0 &&
               acquired.handle == UINT64_C(0x200000001));
        assert(!pxa_permission_parse_acquire(&event, 1, &acquired));
        event.payload_size = 8;
        assert(!pxa_permission_parse_acquire(
            &event, UINT64_C(0x100000010), &acquired));
        event.opcode = PXA_PERMISSION_CHECK;
        event.payload_size = 5;
        event_bytes[PXA_HEADER_BYTES + 4] = 1;
        assert(pxa_permission_parse_check(
            &event, UINT64_C(0x100000010), &checked));
        assert(checked.status == 0 && checked.decision == 1);
        {
            uint8_t records[32];
            size_t first_size = 0;
            size_t second_size = 0;
            assert(pxa_wire_record_encode(records, sizeof(records), 1,
                                          (const uint8_t *)"fs.private", 10,
                                          &first_size));
            assert(pxa_wire_record_encode(records + first_size,
                                          sizeof(records) - first_size, 2,
                                          NULL, 0, &second_size));
            assert(pxa_build_message(
                event_bytes, sizeof(event_bytes),
                PXA_PERMISSION_SERVICE, PXA_PERMISSION_REVOKED, 0,
                records, first_size + second_size, &size));
            assert(pxa_parse_event(event_bytes, size, &event));
            assert(pxa_permission_parse_revoked(&event, &revoked));
            assert(revoked.name_size == 10 && revoked.scope_size == 0 &&
                   memcmp(revoked.name, "fs.private", 10) == 0);
            event.token = 1;
            assert(!pxa_permission_parse_revoked(&event, &revoked));
        }
    }
    {
        uint8_t mac_packet[PXA_DEVICE_GET_MAC_PACKET_BYTES];
        uint8_t event_bytes[48];
        uint8_t result[28] = {0};
        pxa_device_mac_result_t mac;
        const uint64_t handle = UINT64_C(0x200000001);
        assert(pxa_device_build_get_mac(
            mac_packet, sizeof(mac_packet), UINT64_C(0x100000020),
            PXA_DEVICE_MAC_WIFI_STATION_HARDWARE, handle, &size));
        assert(size == sizeof(mac_packet) && mac_packet[0] == 15 &&
               mac_packet[2] == 1 && mac_packet[20] == 1 &&
               mac_packet[22] == 2 && mac_packet[24] == 1 &&
               mac_packet[26] == 2 && mac_packet[28] == 8 &&
               mac_packet[30] == 1 && mac_packet[34] == 2);
        assert(!pxa_device_build_get_mac(
            mac_packet, sizeof(mac_packet), UINT64_C(0x100000020),
            PXA_DEVICE_MAC_WIFI_STATION_HARDWARE, 1, &size));
        assert(size == 0);
        pxa_store_u16(result + 4, 1);
        pxa_store_u16(result + 6, 2);
        pxa_store_u16(result + 8, 1);
        pxa_store_u16(result + 10, 2);
        pxa_store_u16(result + 12, 6);
        result[14] = 2;
        result[15] = 0x50;
        pxa_store_u16(result + 20, 3);
        pxa_store_u16(result + 22, 4);
        pxa_store_u32(result + 24, 5);
        assert(pxa_build_message(
            event_bytes, sizeof(event_bytes), PXA_DEVICE_SERVICE,
            PXA_DEVICE_GET_MAC, UINT64_C(0x100000020), result,
            sizeof(result), &size));
        assert(pxa_parse_event(event_bytes, size, &event));
        assert(pxa_device_parse_get_mac(
            &event, UINT64_C(0x100000020), &mac));
        assert(mac.status == 0 && mac.kind == 1 && mac.mac[0] == 2 &&
               mac.mac[1] == 0x50 && mac.flags == 5);
        event.payload_size--;
        assert(!pxa_device_parse_get_mac(
            &event, UINT64_C(0x100000020), &mac));
    }
    {
        uint8_t storage_packet[PXA_STORAGE_MAX_PACKET];
        uint8_t max_value[PXA_STORAGE_MAX_VALUE] = {0};
        uint8_t result[32] = {0};
        uint8_t event_bytes[64];
        pxa_storage_get_result_t got;
        pxa_storage_list_result_t listed;
        size_t offset = 4;
        size_t record_size = 0;
        int32_t status = -1;
        const uint64_t token = UINT64_C(0x100000030);
        max_value[0] = 0xa5;
        assert(pxa_storage_build(storage_packet,
                                     sizeof(storage_packet),
                                     PXA_STORAGE_SET, token,
                                     "save", 4, max_value,
                                     sizeof(max_value), &size));
        assert(size == PXA_HEADER_BYTES + 4u + 4u + 4u +
                       sizeof(max_value) && storage_packet[0] == 6 &&
               storage_packet[2] == 2 && storage_packet[20] == 1 &&
               storage_packet[28] == 2 && storage_packet[32] == 0xa5);
        assert(!pxa_storage_build(storage_packet,
                                      sizeof(storage_packet),
                                      PXA_STORAGE_SET, token,
                                      "save", 4, max_value,
                                      sizeof(max_value) + 1u, &size));
        assert(size == 0);
        assert(pxa_storage_request_small(
                   PXA_STORAGE_GET, token, "save", 4) == 0);
        assert(submitted_size == 28 && submitted[0] == 6 &&
               submitted[2] == 1);
        assert(pxa_wire_record_encode(result + 4, sizeof(result) - 4,
                                      2, (const uint8_t *)"ok", 2,
                                      &record_size));
        assert(pxa_build_message(event_bytes, sizeof(event_bytes),
                                    PXA_STORAGE_SERVICE,
                                    PXA_STORAGE_GET, token, result,
                                    4 + record_size, &size));
        assert(pxa_parse_event(event_bytes, size, &event));
        assert(pxa_storage_parse_get(&event, token, &got));
        assert(got.status == 0 && got.value.size == 2 &&
               memcmp(got.value.data, "ok", 2) == 0);
        event.payload_size--;
        assert(!pxa_storage_parse_get(&event, token, &got));
        assert(pxa_wire_record_encode(result + offset,
                                      sizeof(result) - offset, 1,
                                      (const uint8_t *)"alpha", 5,
                                      &record_size));
        offset += record_size;
        assert(pxa_wire_record_encode(result + offset,
                                      sizeof(result) - offset, 1,
                                      (const uint8_t *)"beta", 4,
                                      &record_size));
        offset += record_size;
        assert(pxa_build_message(event_bytes, sizeof(event_bytes),
                                    PXA_STORAGE_SERVICE,
                                    PXA_STORAGE_LIST, token, result,
                                    offset, &size));
        assert(pxa_parse_event(event_bytes, size, &event));
        assert(pxa_storage_parse_list(&event, token, &listed));
        assert(listed.status == 0 && listed.count == 2 &&
               listed.keys[0].size == 5 && listed.keys[1].size == 4);
        event_bytes[PXA_HEADER_BYTES + 4 + 4 + 5 + 4] = 'a';
        assert(!pxa_storage_parse_list(&event, token, &listed));
        pxa_store_u32(result, 0);
        assert(pxa_storage_parse_status(&event, token,
                                           PXA_STORAGE_LIST,
                                           &status) == 0);
    }
    {
        uint8_t fs_packet[PXA_FS_MAX_OPEN_PACKET];
        uint8_t event_bytes[48];
        uint8_t result[16] = {0};
        pxa_fs_open_result_t opened;
        pxa_fs_seek_result_t seek;
        pxa_fs_directory_result_t directory;
        const uint64_t token = UINT64_C(0x100000040);
        const uint64_t handle = UINT64_C(0x200000001);
        assert(pxa_fs_build_open(fs_packet, sizeof(fs_packet), token,
                                    "note.txt", 8,
                                    PXA_FS_OPEN_READ |
                                    PXA_FS_OPEN_WRITE |
                                    PXA_FS_OPEN_CREATE, &size));
        assert(size == 40 && fs_packet[0] == 5 && fs_packet[2] == 1 &&
               fs_packet[20] == 1 && fs_packet[32] == 3);
        assert(!pxa_fs_build_open(fs_packet, sizeof(fs_packet), token,
                                     "../escape", 9,
                                     PXA_FS_OPEN_READ, &size));
        assert(size == 0);
        assert(!pxa_fs_build_open(fs_packet, sizeof(fs_packet), token,
                                     "note.txt", 8,
                                     PXA_FS_OPEN_EXCLUSIVE, &size));
        pxa_store_u64(result + 4, handle);
        assert(pxa_build_message(event_bytes, sizeof(event_bytes),
                                    PXA_FS_SERVICE, PXA_FS_OPEN,
                                    token, result, 12, &size));
        assert(pxa_parse_event(event_bytes, size, &event));
        assert(pxa_fs_parse_open(&event, token, &opened));
        assert(opened.status == 0 && opened.handle == handle);
        event.payload_size--;
        assert(!pxa_fs_parse_open(&event, token, &opened));
        assert(pxa_fs_build_seek(fs_packet, sizeof(fs_packet), token,
                                    handle, -3, PXA_FS_SEEK_END, &size));
        assert(size == 37 && fs_packet[2] == PXA_FS_SEEK &&
               fs_packet[20] == 1 && fs_packet[28] == 0xfd &&
               fs_packet[36] == PXA_FS_SEEK_END);
        assert(!pxa_fs_build_seek(fs_packet, sizeof(fs_packet), token,
                                     1, 0, PXA_FS_SEEK_START, &size));
        pxa_store_u64(result + 4, 7);
        assert(pxa_build_message(event_bytes, sizeof(event_bytes),
                                    PXA_FS_SERVICE, PXA_FS_SEEK,
                                    token, result, 12, &size));
        assert(pxa_parse_event(event_bytes, size, &event));
        assert(pxa_fs_parse_seek(&event, token, &seek));
        assert(seek.status == 0 && seek.position == 7);
        pxa_store_u16(result + 4, 7);
        pxa_store_u16(result + 6, 0);
        assert(pxa_build_message(
            event_bytes, sizeof(event_bytes), PXA_FS_SERVICE,
            PXA_FS_READ_DIRECTORY, token, result, 8, &size));
        assert(pxa_parse_event(event_bytes, size, &event));
        assert(pxa_fs_parse_directory(&event, token, &directory));
        assert(directory.status == 0 && directory.end == 1);
        event.payload_size--;
        assert(!pxa_fs_parse_directory(&event, token, &directory));
    }
    {
        uint8_t ipc_packet[80];
        uint8_t event_bytes[80];
        uint8_t result_bytes[32] = {0};
        size_t record_size = 0;
        size_t second_record_size = 0;
        size_t optional_size = 0;
        pxa_ipc_call_result_t call;
        pxa_ipc_request_t request;
        pxa_ipc_result_t reply;
        const uint64_t token = UINT64_C(0x1234567800000001);
        assert(pxa_ipc_build_call(ipc_packet, sizeof(ipc_packet), token,
                                     "echo", 4, (const uint8_t *)"hi", 2,
                                     &size));
        assert(size == 34 && ipc_packet[0] == 7 && ipc_packet[2] == 1);
        assert(!pxa_ipc_build_call(ipc_packet, sizeof(ipc_packet), token,
                                      "a/b", 3, NULL, 0, &size));
        assert(!pxa_ipc_build_reply(ipc_packet, sizeof(ipc_packet),
                                       token, 0, 0, NULL, 0, &size));
        assert(pxa_ipc_build_reply(ipc_packet, sizeof(ipc_packet),
                                      token, 9, 0,
                                      (const uint8_t *)"ok", 2, &size));
        assert(size == 42 && ipc_packet[2] == PXA_IPC_REPLY);
        pxa_store_u32(result_bytes + 4, 9);
        assert(pxa_build_message(event_bytes, sizeof(event_bytes),
                                    PXA_IPC_SERVICE, PXA_IPC_CALL,
                                    token, result_bytes, 8, &size));
        assert(pxa_parse_event(event_bytes, size, &event));
        assert(pxa_ipc_parse_call_result(&event, token, &call));
        assert(call.status == 0 && call.call_id == 9);
        assert(!pxa_ipc_parse_call_result(&event, token + 1, &call));
        assert(pxa_wire_record_encode(result_bytes, sizeof(result_bytes),
                                      1, (const uint8_t *)"echo", 4,
                                      &record_size));
        assert(pxa_wire_record_encode(result_bytes + record_size,
                                      sizeof(result_bytes) - record_size,
                                      2, (const uint8_t *)"hi", 2,
                                      &second_record_size));
        assert(pxa_build_message(event_bytes, sizeof(event_bytes),
                                    PXA_IPC_SERVICE,
                                    PXA_IPC_REQUEST_EVENT, 9,
                                    result_bytes, record_size + second_record_size,
                                    &size));
        assert(pxa_parse_event(event_bytes, size, &event));
        assert(pxa_ipc_parse_request(&event, &request));
        assert(request.call_id == 9 && request.endpoint.size == 4 &&
               request.payload.size == 2 &&
               memcmp(request.payload.data, "hi", 2) == 0);
        event.token = UINT64_C(0x100000009);
        assert(!pxa_ipc_parse_request(&event, &request));
        assert(pxa_wire_record_encode(
            result_bytes + record_size + second_record_size,
            sizeof(result_bytes) - record_size - second_record_size,
            UINT16_C(0x8004), (const uint8_t *)"x", 1,
            &optional_size));
        assert(pxa_build_message(
            event_bytes, sizeof(event_bytes), PXA_IPC_SERVICE,
            PXA_IPC_REQUEST_EVENT, 9, result_bytes,
            record_size + second_record_size + optional_size, &size));
        assert(pxa_parse_event(event_bytes, size, &event));
        assert(pxa_ipc_parse_request(&event, &request));
        event_bytes[PXA_HEADER_BYTES + record_size +
                    second_record_size + 1] = 0;
        assert(!pxa_ipc_parse_request(&event, &request));
        pxa_store_u32(result_bytes, 0);
        assert(pxa_wire_record_encode(result_bytes + 4,
                                      sizeof(result_bytes) - 4,
                                      3, (const uint8_t *)"ok", 2,
                                      &record_size));
        assert(pxa_build_message(event_bytes, sizeof(event_bytes),
                                    PXA_IPC_SERVICE,
                                    PXA_IPC_RESULT_EVENT, 9,
                                    result_bytes, 4 + record_size, &size));
        assert(pxa_parse_event(event_bytes, size, &event));
        assert(pxa_ipc_parse_result(&event, &reply));
        assert(reply.call_id == 9 && reply.status == 0 &&
               reply.payload.size == 2 &&
               memcmp(reply.payload.data, "ok", 2) == 0);
        event.payload_size--;
        assert(!pxa_ipc_parse_result(&event, &reply));
    }
    return 0;
}

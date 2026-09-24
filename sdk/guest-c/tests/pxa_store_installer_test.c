#include "pxa_store_installer.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[512];
static uint32_t captured_length;

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

int main(void) {
    static const char digest[] =
        "2e24a4aed4e3b2b71f8cdce606d1d2867f5b4d7f71b3dcad3fb1eb1735dbd0f3";
    static const char ticket[] = "/api/v2/artifacts/3/download?expires=1&signature="
        "3c93b01415b64e5f12741f0ca5918a99a9a49afcac761c648a0fc00b629f95ad";
    uint8_t packet[512];
    uint8_t status_bytes[4] = {0};
    pxa_event_t event = {0};
    int32_t status;
    assert(pxa_store_install(7, "pxa-voxel-craft", ticket, digest,
                             538882u, packet, sizeof(packet)));
    assert(pxa_read_u16(captured) == PXA_SERVICE_STORE_INSTALLER);
    assert(pxa_read_u16(captured + 2) == PXA_STORE_INSTALL_REQUEST);
    assert(pxa_read_u32(captured + 4) == 7u);
    assert(captured_length == 12u + 43u + 15u + strlen(ticket));
    assert(captured[12] == 15u);
    assert(pxa_read_u16(captured + 13) == strlen(ticket));
    assert(pxa_read_u64(captured + 15) == 538882u);
    assert(captured[23] == 0x2eu && captured[54] == 0xf3u);
    assert(memcmp(captured + 55, "pxa-voxel-craft", 15u) == 0);
    assert(memcmp(captured + 70, ticket, strlen(ticket)) == 0);
    assert(pxa_store_download(9, "pxa-voxel-craft", ticket, digest,
                              538882u, packet, sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_STORE_DOWNLOAD_REQUEST);
    assert(pxa_read_u32(captured + 4) == 9u);
    assert(pxa_store_install_file(10, ".store-1234abcd.pxa", packet,
                                  sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_STORE_INSTALL_FILE_REQUEST);
    assert(captured_length == 31u);
    assert(memcmp(captured + 12, ".store-1234abcd.pxa", 19u) == 0);
    assert(!pxa_store_install_file(11, "../app.pxa", packet, sizeof(packet)));
    assert(!pxa_store_install(8, "demo", ticket, "abcd", 10u,
                              packet, sizeof(packet)));
    assert(!pxa_store_install(8, "demo", ticket, digest, 0u,
                              packet, sizeof(packet)));
    assert(pxa_store_manage(PXA_STORE_INSTALLED_LIST_REQUEST, 12u, NULL,
                            packet, sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_STORE_INSTALLED_LIST_REQUEST);
    assert(captured_length == 12u);
    assert(pxa_store_manage(PXA_STORE_DELETE_REQUEST, 13u,
                            ".store-1234abcd.pxa", packet, sizeof(packet)));
    assert(captured_length == 31u);
    assert(!pxa_store_manage(PXA_STORE_DELETE_REQUEST, 13u,
                             "../unsafe.pxa", packet, sizeof(packet)));
    assert(pxa_store_manage(PXA_STORE_LAUNCH_REQUEST, 14u, "demo",
                            packet, sizeof(packet)));
    assert(captured_length == 16u);
    assert(pxa_store_manage(PXA_STORE_UNINSTALL_REQUEST, 15u, "demo",
                            packet, sizeof(packet)));
    assert(pxa_read_u16(captured + 2) == PXA_STORE_UNINSTALL_REQUEST);
    assert(captured_length == 16u);
    assert(!pxa_store_manage(PXA_STORE_UNINSTALL_REQUEST, 16u, NULL,
                             packet, sizeof(packet)));
    {
        uint8_t installed_payload[5u + 12u + 4u + 5u + 10u] = {0};
        pxa_store_installed_app_t installed[PXA_STORE_INSTALLED_MAX] = {0};
        size_t count = 0;
        installed_payload[4] = 1;
        installed_payload[5] = 4;
        installed_payload[6] = 5;
        installed_payload[7] = 10;
        installed_payload[8] = 42;
        installed_payload[16] = 1;
        memcpy(installed_payload + 17, "demo", 4);
        memcpy(installed_payload + 21, "1.2.3", 5);
        memcpy(installed_payload + 26, "root:demo", 9);
        event.service = PXA_SERVICE_STORE_INSTALLER;
        event.opcode = PXA_STORE_INSTALLED_LIST_REQUEST;
        event.request_id = 12;
        event.payload = installed_payload;
        event.payload_length = sizeof(installed_payload);
        assert(pxa_store_parse_installed(&event, installed, PXA_STORE_INSTALLED_MAX,
                                         &count) && count == 1u);
        assert(installed[0].sequence == 42u);
        assert(installed[0].uninstallable == 1u);
        assert(strcmp(installed[0].app_id, "demo") == 0);
        assert(strcmp(installed[0].identity, "root:demo") == 0);
        installed_payload[7] = 130;
        assert(!pxa_store_parse_installed(&event, installed,
                                          PXA_STORE_INSTALLED_MAX, &count));
    }
    {
        uint8_t downloads_payload[5u + 1u + 19u + 4u] = {0};
        pxa_store_download_entry_t downloads[PXA_STORE_DOWNLOAD_MAX] = {0};
        size_t count = 0;
        downloads_payload[4] = 1;
        downloads_payload[5] = 4;
        memcpy(downloads_payload + 6, ".store-1234abcd.pxa", 19);
        memcpy(downloads_payload + 25, "demo", 4);
        event.opcode = PXA_STORE_DOWNLOAD_LIST_REQUEST;
        event.request_id = 10;
        event.payload = downloads_payload;
        event.payload_length = sizeof(downloads_payload);
        assert(pxa_store_parse_downloads(&event, downloads, PXA_STORE_DOWNLOAD_MAX,
                                         &count) && count == 1u);
        assert(strcmp(downloads[0].filename, ".store-1234abcd.pxa") == 0);
        downloads_payload[6] = '/';
        assert(!pxa_store_parse_downloads(&event, downloads,
                                          PXA_STORE_DOWNLOAD_MAX, &count));
    }
    event.service = PXA_SERVICE_STORE_INSTALLER;
    event.opcode = PXA_STORE_INSTALL_REQUEST;
    event.request_id = 7u;
    event.payload = status_bytes;
    event.payload_length = sizeof(status_bytes);
    assert(pxa_store_install_result(&event, &status) && status == PXA_STATUS_OK);
    status_bytes[0] = (uint8_t)PXA_STATUS_DENIED;
    status_bytes[1] = status_bytes[2] = status_bytes[3] = 0xffu;
    assert(pxa_store_install_result(&event, &status) && status == PXA_STATUS_DENIED);
    {
        uint8_t download_payload[23] = {0};
        char filename[20];
        memcpy(download_payload + 4, ".store-1234abcd.pxa", 19u);
        event.opcode = PXA_STORE_DOWNLOAD_REQUEST;
        event.payload = download_payload;
        event.payload_length = sizeof(download_payload);
        assert(pxa_store_download_result(&event, &status, filename));
        assert(status == PXA_STATUS_OK);
        assert(strcmp(filename, ".store-1234abcd.pxa") == 0);
        download_payload[4] = '/';
        assert(!pxa_store_download_result(&event, &status, filename));
        event.opcode = PXA_STORE_INSTALL_FILE_REQUEST;
        event.payload = status_bytes;
        event.payload_length = sizeof(status_bytes);
        assert(pxa_store_install_result(&event, &status) &&
               status == PXA_STATUS_DENIED);
    }
    event.opcode = PXA_STORE_UNINSTALL_REQUEST;
    assert(pxa_store_uninstall_result(&event, &status) &&
           status == PXA_STATUS_DENIED);
    return 0;
}

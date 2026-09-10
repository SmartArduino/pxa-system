#include "pxa_fs.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[512];
static uint32_t captured_length;
static int32_t control_status;

int32_t pxa_control(const uint8_t* data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return control_status;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t* data,
               uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

static void test_open_encoding(void) {
    uint8_t payload[128];
    uint8_t packet[160];
    control_status = PXA_STATUS_OK;
    assert(pxa_fs_open(71, "notes/today.txt", 15,
                       PXA_FS_OPEN_READ | PXA_FS_OPEN_WRITE | PXA_FS_OPEN_CREATE,
                       payload, sizeof(payload), packet, sizeof(packet)));
    assert(captured_length == 39);
    assert(pxa_read_u16(captured) == PXA_SERVICE_FS);
    assert(pxa_read_u16(captured + 2) == PXA_FS_OPEN);
    assert(pxa_read_u32(captured + 4) == 71);
    assert(pxa_read_u32(captured + 8) == 27);
    assert(pxa_read_u16(captured + 12) == PXA_FS_PATH);
    assert(pxa_read_u16(captured + 14) == 15);
    assert(memcmp(captured + 16, "notes/today.txt", 15) == 0);
    assert(pxa_read_u16(captured + 31) == PXA_FS_OPEN_FLAGS);
    assert(pxa_read_u32(captured + 35) ==
           (PXA_FS_OPEN_READ | PXA_FS_OPEN_WRITE | PXA_FS_OPEN_CREATE));
}

static void test_input_and_result_validation(void) {
    uint8_t payload[32];
    uint8_t packet[64];
    assert(!pxa_fs_open(0, "file", 4, PXA_FS_OPEN_READ, payload, sizeof(payload),
                        packet, sizeof(packet)));
    assert(!pxa_fs_open(1, "../file", 7, PXA_FS_OPEN_READ, payload, sizeof(payload),
                        packet, sizeof(packet)));
    assert(!pxa_fs_open(1, "file", 4, PXA_FS_OPEN_TRUNCATE, payload,
                        sizeof(payload), packet, sizeof(packet)));
    assert(!pxa_fs_open(1, "dir", 3, PXA_FS_OPEN_READ | PXA_FS_OPEN_DIRECTORY |
                        PXA_FS_OPEN_CREATE, payload, sizeof(payload), packet,
                        sizeof(packet)));
    assert(!pxa_fs_valid_path("bad\xc0\xaf", 5));

    const uint8_t success[] = {PXA_SERVICE_FS, 0, PXA_FS_OPEN, 0, 9, 0, 0, 0,
                               8, 0, 0, 0, 0, 0, 0, 0, 4, 3, 2, 1};
    pxa_event_t event;
    pxa_fs_open_result_t parsed;
    assert(pxa_parse_event(success, sizeof(success), &event));
    assert(pxa_fs_parse_open_result(&event, &parsed));
    assert(parsed.status == PXA_STATUS_OK && parsed.handle == 0x01020304);
    const uint8_t denied[] = {PXA_SERVICE_FS, 0, PXA_FS_OPEN, 0, 9, 0, 0, 0,
                              4, 0, 0, 0, 0xfc, 0xff, 0xff, 0xff};
    assert(pxa_parse_event(denied, sizeof(denied), &event));
    assert(pxa_fs_parse_open_result(&event, &parsed));
    assert(parsed.status == PXA_STATUS_DENIED);
}

static void test_rename_and_directory_result(void) {
    uint8_t payload[128];
    uint8_t packet[160];
    control_status = PXA_STATUS_OK;
    assert(pxa_fs_rename(12, "notes/old", 9, "notes/new", 9, payload,
                         sizeof(payload), packet, sizeof(packet)));
    assert(pxa_read_u16(captured) == PXA_SERVICE_FS);
    assert(pxa_read_u16(captured + 2) == PXA_FS_RENAME);
    assert(!pxa_fs_rename(12, "same", 4, "same", 4, payload,
                          sizeof(payload), packet, sizeof(packet)));
    assert(pxa_fs_close(0x10203040, packet, sizeof(packet)));
    assert(pxa_read_u16(captured) == PXA_SERVICE_CORE);
    assert(pxa_read_u16(captured + 2) == PXA_CORE_CLOSE_HANDLE);
    assert(pxa_read_u32(captured + 12) == 0x10203040);

    const uint8_t entry[] = {
        PXA_SERVICE_FS, 0, PXA_FS_READ_DIRECTORY, 0, 6, 0, 0, 0,
        29, 0, 0, 0,
        0, 0, 0, 0,
        PXA_FS_ENTRY_NAME, 0, 4, 0, 'n', 'o', 't', 'e',
        PXA_FS_ENTRY_KIND, 0, 1, 0, PXA_FS_KIND_REGULAR,
        PXA_FS_ENTRY_SIZE, 0, 8, 0, 5, 0, 0, 0, 0, 0, 0, 0,
    };
    pxa_event_t event;
    pxa_fs_directory_result_t directory;
    assert(pxa_parse_event(entry, sizeof(entry), &event));
    assert(pxa_fs_parse_directory_result(&event, &directory));
    assert(directory.status == PXA_STATUS_OK && !directory.end &&
           directory.name_length == 4 && memcmp(directory.name, "note", 4) == 0 &&
           directory.kind == PXA_FS_KIND_REGULAR && directory.size == 5);

    const uint8_t end[] = {
        PXA_SERVICE_FS, 0, PXA_FS_READ_DIRECTORY, 0, 7, 0, 0, 0,
        8, 0, 0, 0,
        0, 0, 0, 0,
        PXA_FS_END_OF_DIRECTORY, 0, 0, 0,
    };
    assert(pxa_parse_event(end, sizeof(end), &event));
    assert(pxa_fs_parse_directory_result(&event, &directory));
    assert(directory.status == PXA_STATUS_OK && directory.end);
}

int main(void) {
    test_open_encoding();
    test_input_and_result_validation();
    test_rename_and_directory_result();
    return 0;
}

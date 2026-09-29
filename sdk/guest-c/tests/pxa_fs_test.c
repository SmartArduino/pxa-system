#include "pxa_fs.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[512];
static uint32_t captured_length;

int32_t pxa_submit(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

static int make_event(uint8_t *packet, size_t capacity, uint16_t opcode,
                      uint64_t token, const uint8_t *payload,
                      size_t payload_size, pxa_event_t *event) {
    uint32_t size = 0;
    return pxa_build_message(packet, capacity, PXA_FS_SERVICE, opcode, token,
                             payload, payload_size, &size) &&
           pxa_parse_event(packet, size, event);
}

static void test_path_and_flag_validation(void) {
    assert(pxa_fs_valid_path("file", 4));
    assert(pxa_fs_valid_path("notes/today.txt", 15));
    assert(!pxa_fs_valid_path(NULL, 1));
    assert(!pxa_fs_valid_path("file", 0));
    assert(!pxa_fs_valid_path("/absolute", 9));
    assert(!pxa_fs_valid_path("trailing/", 9));
    assert(!pxa_fs_valid_path("dir//file", 9));
    assert(!pxa_fs_valid_path("./file", 6));
    assert(!pxa_fs_valid_path("dir/../file", 11));
    assert(!pxa_fs_valid_path("bad\\path", 8));
    assert(!pxa_fs_valid_path("bad\xc0\xaf", 5));
    assert(!pxa_fs_valid_path(".pxa-private", 12));
    assert(!pxa_fs_valid_path("bad\tname", 8));
    assert(!pxa_fs_valid_utf8("\xed\xa0\x80", 3));

    assert(pxa_fs_flags_valid(PXA_FS_OPEN_READ));
    assert(pxa_fs_flags_valid(PXA_FS_OPEN_READ | PXA_FS_OPEN_DIRECTORY));
    assert(pxa_fs_flags_valid(PXA_FS_OPEN_WRITE | PXA_FS_OPEN_CREATE |
                              PXA_FS_OPEN_EXCLUSIVE | PXA_FS_OPEN_TRUNCATE));
    assert(!pxa_fs_flags_valid(0));
    assert(!pxa_fs_flags_valid(PXA_FS_OPEN_DIRECTORY));
    assert(!pxa_fs_flags_valid(PXA_FS_OPEN_READ | PXA_FS_OPEN_WRITE |
                               PXA_FS_OPEN_DIRECTORY));
    assert(!pxa_fs_flags_valid(PXA_FS_OPEN_EXCLUSIVE));
    assert(!pxa_fs_flags_valid(PXA_FS_OPEN_TRUNCATE));
    assert(!pxa_fs_flags_valid(PXA_FS_OPEN_READ | UINT32_C(0x80)));
}

static void test_build_open(void) {
    const uint64_t token = UINT64_C(0x1122334455667788);
    const uint32_t flags = PXA_FS_OPEN_READ | PXA_FS_OPEN_WRITE |
                           PXA_FS_OPEN_CREATE;
    uint8_t packet[PXA_FS_MAX_OPEN_PACKET];
    uint32_t size = 0;
    size_t consumed = 0;
    pxa_wire_record_view_t record;
    pxa_event_t event;

    assert(pxa_fs_build_open(packet, sizeof(packet), token, "notes/today.txt",
                             15, flags, &size));
    assert(pxa_parse_event(packet, size, &event));
    assert(event.service == PXA_FS_SERVICE && event.opcode == PXA_FS_OPEN &&
           event.token == token);
    assert(pxa_wire_record_decode(event.payload, event.payload_size, &record,
                                  &consumed));
    assert(record.raw_tag == 1 && record.payload_size == 15 &&
           memcmp(record.payload, "notes/today.txt", 15) == 0);
    assert(pxa_wire_record_decode(event.payload + consumed,
                                  event.payload_size - consumed, &record,
                                  &consumed));
    assert(record.raw_tag == 3 && record.payload_size == 4 &&
           pxa_load_u32(record.payload) == flags);

    assert(!pxa_fs_build_open(packet, sizeof(packet), 0, "file", 4,
                              PXA_FS_OPEN_READ, &size));
    assert(!pxa_fs_build_open(packet, sizeof(packet), token, "../file", 7,
                              PXA_FS_OPEN_READ, &size));
    assert(!pxa_fs_build_open(packet, sizeof(packet), token, "file", 4,
                              PXA_FS_OPEN_TRUNCATE, &size));
    assert(!pxa_fs_build_open(packet, PXA_HEADER_BYTES + 8u + 3u, token,
                              "file", 4, PXA_FS_OPEN_READ, &size));

    assert(pxa_fs_request_open(token, "file", 4, PXA_FS_OPEN_READ) ==
           PXA_STATUS_OK);
    assert(captured_length == size || captured_length != 0);
    assert(pxa_parse_event(captured, captured_length, &event));
    assert(event.service == PXA_FS_SERVICE && event.opcode == PXA_FS_OPEN &&
           event.token == token);
}

static void test_build_path_and_rename(void) {
    const uint64_t token = 12;
    uint8_t packet[PXA_FS_MAX_RENAME_PACKET];
    uint32_t size = 0;
    pxa_event_t event;

    assert(pxa_fs_build_path(packet, sizeof(packet), PXA_FS_MAKE_DIRECTORY,
                             token, "notes", 5, &size));
    assert(pxa_parse_event(packet, size, &event));
    assert(event.service == PXA_FS_SERVICE &&
           event.opcode == PXA_FS_MAKE_DIRECTORY && event.token == token);
    assert(!pxa_fs_build_path(packet, sizeof(packet), PXA_FS_OPEN, token,
                              "notes", 5, &size));
    assert(!pxa_fs_build_path(packet, sizeof(packet), PXA_FS_REMOVE, 0,
                              "notes", 5, &size));

    assert(pxa_fs_build_rename(packet, sizeof(packet), token, "notes/old", 9,
                               "notes/new", 9, &size));
    assert(pxa_parse_event(packet, size, &event));
    assert(event.opcode == PXA_FS_RENAME && event.token == token);
    assert(!pxa_fs_build_rename(packet, sizeof(packet), token, "same", 4,
                                "same", 4, &size));
    assert(!pxa_fs_build_rename(packet, sizeof(packet), token, "../old", 6,
                                "notes/new", 9, &size));
}

static void test_parse_results(void) {
    const uint64_t token = 7;
    uint8_t packet[192];
    uint8_t payload[128];
    uint8_t entry[32];
    uint8_t size_bytes[8];
    size_t entry_size = 0;
    size_t written = 0;
    pxa_event_t event;
    pxa_fs_open_result_t opened;
    pxa_fs_seek_result_t seek;
    pxa_fs_directory_result_t directory;

    pxa_store_u32(payload, 0);
    pxa_store_u64(payload + 4, UINT64_C(0x0000000200000001));
    assert(make_event(packet, sizeof(packet), PXA_FS_OPEN, token, payload, 12,
                      &event));
    assert(!pxa_fs_parse_open(&event, token + 1, &opened));
    assert(!pxa_fs_parse_open(&event, 0, &opened));
    assert(pxa_fs_parse_open(&event, token, &opened));
    assert(opened.status == PXA_STATUS_OK &&
           opened.handle == UINT64_C(0x0000000200000001));

    pxa_store_u32(payload, (uint32_t)PXA_STATUS_DENIED);
    assert(make_event(packet, sizeof(packet), PXA_FS_OPEN, token, payload, 4,
                      &event));
    assert(pxa_fs_parse_open(&event, token, &opened));
    assert(opened.status == PXA_STATUS_DENIED && opened.handle == 0);

    pxa_store_u32(payload, 0);
    pxa_store_u64(payload + 4, 4096);
    assert(make_event(packet, sizeof(packet), PXA_FS_SEEK, token, payload, 12,
                      &event));
    assert(pxa_fs_parse_seek(&event, token, &seek));
    assert(seek.status == PXA_STATUS_OK && seek.position == 4096);

    entry_size = 0;
    assert(pxa_wire_record_encode(entry, sizeof(entry), 4,
                                  (const uint8_t *)"note", 4, &written));
    entry_size += written;
    assert(pxa_wire_record_encode(entry + entry_size,
                                  sizeof(entry) - entry_size, 5,
                                  (const uint8_t[]){PXA_FS_KIND_REGULAR}, 1,
                                  &written));
    entry_size += written;
    pxa_store_u64(size_bytes, 5);
    assert(pxa_wire_record_encode(entry + entry_size,
                                  sizeof(entry) - entry_size, 6, size_bytes,
                                  8, &written));
    entry_size += written;
    pxa_store_u32(payload, 0);
    memcpy(payload + 4, entry, entry_size);
    assert(make_event(packet, sizeof(packet), PXA_FS_READ_DIRECTORY, token,
                      payload, 4 + entry_size, &event));
    assert(pxa_fs_parse_directory(&event, token, &directory));
    assert(directory.status == PXA_STATUS_OK && !directory.end &&
           directory.name_size == 4 &&
           memcmp(directory.name, "note", 4) == 0 &&
           directory.kind == PXA_FS_KIND_REGULAR && directory.size == 5);

    pxa_store_u32(payload, 0);
    assert(pxa_wire_record_encode(payload + 4, sizeof(payload) - 4, 7, NULL, 0,
                                  &written));
    assert(make_event(packet, sizeof(packet), PXA_FS_READ_DIRECTORY, token,
                      payload, 4 + written, &event));
    assert(pxa_fs_parse_directory(&event, token, &directory));
    assert(directory.status == PXA_STATUS_OK && directory.end);
}

int main(void) {
    test_path_and_flag_validation();
    test_build_open();
    test_build_path_and_rename();
    test_parse_results();
    return 0;
}

#include "../../testing/pxa_control_capture.h"
#include "pxa_fs.h"
#include "pxa_fs_roots.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[512];
static uint32_t captured_length;

int32_t pxa_test_control(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    assert(handle>>32);
    if(operation==16) {
        assert(length==28 && pxa_load_u64(data)==9 && pxa_load_u64(data+8)==7);
        assert(pxa_load_u32(data+16)==4 && !pxa_load_u32(data+20));
        memcpy(data+24,"read",4);return 4;
    }
    if(operation==17){assert(length==8 && pxa_load_u64(data)==7);return 0;}
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
    assert(pxa_fs_build_replace(packet,sizeof(packet),token,"notes/old",9,"notes/new",9,&size));
    assert(pxa_parse_event(packet,size,&event) && event.opcode==PXA_FS_REPLACE);
    uint8_t positional[28];
    assert(pxa_fs_read_at(UINT64_C(0x100000001),9,7,positional,28)==4);
    assert(!memcmp(positional+24,"read",4));
    assert(!pxa_fs_cancel_read_at(UINT64_C(0x100000001),7));
    assert(pxa_fs_read_at(UINT64_C(0x100000001),UINT64_MAX,7,positional,28)==PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_fs_read_at(UINT64_C(0x100000001),9,0,positional,28)==PXA_STATUS_INVALID_ARGUMENT);
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

static void test_scoped_roots(void) {
    uint8_t id[16]={7},packet[128],payload[76]={0}; pxa_event_t event;
    pxa_fs_root_result_t root; pxa_fs_at_result_t file; pxa_fs_root_info_result_t info;
    const uint64_t handle=UINT64_C(0x100000007);
    assert(!pxa_fs_request_open_grant(30,handle,id));
    assert(pxa_parse_event(captured,captured_length,&event));
    assert(event.opcode==9 && event.payload_size==28 && event.payload[0]==4);
    assert(pxa_load_u64(event.payload+4)==handle && event.payload[12]==7);
    assert(!pxa_fs_request_directory(31,handle,"Data",4,3,1));
    assert(pxa_parse_event(captured,captured_length,&event));
    assert(event.opcode==10 && event.payload_size==22 && pxa_load_u32(event.payload+8)==3);
    assert(pxa_fs_request_directory(31,handle,"x\0y",3,3,1)==-1);
    pxa_store_u64(payload+4,handle);pxa_store_u32(payload+12,3);payload[16]=7;
    assert(make_event(packet,sizeof(packet),10,31,payload,32,&event));
    assert(pxa_fs_parse_root(&event,31,&root) && root.handle==handle && root.rights==3 && root.grant_id[0]==7);
    event.payload_size--;assert(!pxa_fs_parse_root(&event,31,&root));
    assert(!pxa_fs_request_open_at(32,handle,"map.bin",7,1));
    assert(pxa_parse_event(captured,captured_length,&event));
    assert(event.opcode==11 && event.payload_size==21 && pxa_load_u16(event.payload+12)==7);
    memset(payload,0,sizeof(payload));pxa_store_u64(payload+4,handle);
    payload[12]=1;pxa_store_u64(payload+24,8192);
    assert(make_event(packet,sizeof(packet),11,32,payload,32,&event));
    assert(pxa_fs_parse_open_at(&event,32,&file) && file.size==8192 && file.kind==1);
    payload[13]=1;assert(make_event(packet,sizeof(packet),11,32,payload,32,&event));
    assert(!pxa_fs_parse_open_at(&event,32,&file));payload[13]=0;
    memset(payload,0,sizeof(payload));payload[4]=9;pxa_store_u64(payload+20,99);
    pxa_store_u32(payload+28,3);pxa_store_u16(payload+40,255);pxa_store_u16(payload+42,255);
    assert(make_event(packet,sizeof(packet),17,33,payload,76,&event));
    assert(pxa_fs_parse_root_info(&event,33,&info) && info.mount_epoch==99 && info.volume_id[0]==9);
    assert(!pxa_fs_parse_root_info(&event,34,&info));
}
int main(void) {
    assert(pxa_fs_request_app_media(1)==0 && captured_length==48);
    assert(captured[20]==5);
    for(unsigned i=21;i<48;++i) assert(captured[i]==0);
    assert(pxa_fs_request_app_media(0)==-1);
    test_path_and_flag_validation();
    test_build_open();
    test_build_path_and_rename();
    test_parse_results();
    test_scoped_roots();
    return 0;
}

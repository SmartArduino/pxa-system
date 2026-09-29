#ifndef PXA_GUEST_FS_H
#define PXA_GUEST_FS_H

#include "pxa_core.h"
#include "pxa_fs_path.h"

#define PXA_FS_SERVICE 5u
#define PXA_FS_OPEN 1u
#define PXA_FS_MAKE_DIRECTORY 2u
#define PXA_FS_REMOVE 3u
#define PXA_FS_RENAME 4u
#define PXA_FS_STAT 5u
#define PXA_FS_SEEK 6u
#define PXA_FS_READ_DIRECTORY 7u

#define PXA_FS_OPEN_READ 1u
#define PXA_FS_OPEN_WRITE 2u
#define PXA_FS_OPEN_CREATE 4u
#define PXA_FS_OPEN_EXCLUSIVE 8u
#define PXA_FS_OPEN_TRUNCATE 16u
#define PXA_FS_OPEN_APPEND 32u
#define PXA_FS_OPEN_DIRECTORY 64u
#define PXA_FS_KIND_REGULAR 1u
#define PXA_FS_KIND_DIRECTORY 2u
#define PXA_FS_SEEK_START 0u
#define PXA_FS_SEEK_CURRENT 1u
#define PXA_FS_SEEK_END 2u
#define PXA_FS_IO_READ 1u
#define PXA_FS_IO_WRITE 2u
#define PXA_FS_MAX_PATH 255u
#define PXA_FS_MAX_OPEN_PACKET (PXA_HEADER_BYTES + 8u + PXA_FS_MAX_PATH)
#define PXA_FS_MAX_RENAME_PACKET (PXA_HEADER_BYTES + 8u + 2u * PXA_FS_MAX_PATH)

typedef struct {
    int32_t status;
    uint64_t handle;
} pxa_fs_open_result_t;

typedef struct {
    int32_t status;
    uint8_t kind;
    uint64_t size;
} pxa_fs_stat_result_t;

typedef struct {
    int32_t status;
    uint64_t position;
} pxa_fs_seek_result_t;

/* name points into the event and expires when the callback returns. */
typedef struct {
    int32_t status;
    const uint8_t *name;
    uint16_t name_size;
    uint8_t kind;
    uint64_t size;
    uint8_t end;
} pxa_fs_directory_result_t;

static inline int pxa_fs_flags_valid(uint32_t flags) {
    const uint32_t known = UINT32_C(0x7f);
    const int directory = (flags & PXA_FS_OPEN_DIRECTORY) != 0;
    const int readable = (flags & PXA_FS_OPEN_READ) != 0;
    const int writable = (flags & PXA_FS_OPEN_WRITE) != 0;
    return flags != 0 && (flags & ~known) == 0 &&
           (!directory || flags == (PXA_FS_OPEN_READ |
                                    PXA_FS_OPEN_DIRECTORY)) &&
           (directory || readable || writable) &&
           ((flags & PXA_FS_OPEN_EXCLUSIVE) == 0 ||
            (flags & PXA_FS_OPEN_CREATE) != 0) &&
           ((flags & (PXA_FS_OPEN_TRUNCATE |
                      PXA_FS_OPEN_APPEND)) == 0 || writable);
}

static inline int pxa_fs_build_open(
    uint8_t *out, size_t capacity, uint64_t token,
    const char *path, size_t path_size, uint32_t flags,
    uint32_t *written) {
    size_t first_size = 0;
    size_t second_size = 0;
    uint8_t encoded_flags[4];
    uint8_t *payload;
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL || token == 0 ||
        !pxa_fs_valid_path(path, path_size) ||
        !pxa_fs_flags_valid(flags) ||
        capacity < PXA_HEADER_BYTES + 8u + path_size) return 0;
    payload = out + PXA_HEADER_BYTES;
    pxa_store_u32(encoded_flags, flags);
    if (!pxa_wire_record_encode(payload,
                                capacity - PXA_HEADER_BYTES,
                                1, (const uint8_t *)path, path_size,
                                &first_size) ||
        !pxa_wire_record_encode(payload + first_size,
                                capacity - PXA_HEADER_BYTES - first_size,
                                3, encoded_flags, sizeof(encoded_flags),
                                &second_size)) return 0;
    return pxa_build_message(out, capacity, PXA_FS_SERVICE,
                                PXA_FS_OPEN, token, payload,
                                first_size + second_size, written);
}

static inline int32_t pxa_fs_request_open(
    uint64_t token, const char *path, size_t path_size, uint32_t flags) {
    uint8_t packet[PXA_FS_MAX_OPEN_PACKET];
    uint32_t size = 0;
    if (!pxa_fs_build_open(packet, sizeof(packet), token,
                              path, path_size, flags, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_fs_build_path(
    uint8_t *out, size_t capacity, uint16_t opcode, uint64_t token,
    const char *path, size_t path_size, uint32_t *written) {
    uint8_t *payload;
    size_t payload_size = 0;
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL || token == 0 ||
        (opcode != PXA_FS_MAKE_DIRECTORY &&
         opcode != PXA_FS_REMOVE && opcode != PXA_FS_STAT) ||
        !pxa_fs_valid_path(path, path_size) ||
        capacity < PXA_HEADER_BYTES + 4u + path_size) return 0;
    payload = out + PXA_HEADER_BYTES;
    if (!pxa_wire_record_encode(payload,
                                capacity - PXA_HEADER_BYTES,
                                1, (const uint8_t *)path, path_size,
                                &payload_size)) return 0;
    return pxa_build_message(out, capacity, PXA_FS_SERVICE,
                                opcode, token, payload, payload_size,
                                written);
}

static inline int32_t pxa_fs_request_path(
    uint16_t opcode, uint64_t token, const char *path, size_t path_size) {
    uint8_t packet[PXA_HEADER_BYTES + 4u + PXA_FS_MAX_PATH];
    uint32_t size = 0;
    if (!pxa_fs_build_path(packet, sizeof(packet), opcode, token,
                              path, path_size, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int pxa_fs_build_rename(
    uint8_t *out, size_t capacity, uint64_t token,
    const char *source, size_t source_size,
    const char *destination, size_t destination_size,
    uint32_t *written) {
    size_t first_size = 0;
    size_t second_size = 0;
    uint8_t *payload;
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL || token == 0 ||
        !pxa_fs_valid_path(source, source_size) ||
        !pxa_fs_valid_path(destination, destination_size) ||
        (source_size == destination_size &&
         pxa_fs_equal_bytes(source, destination, source_size)) ||
        capacity < PXA_HEADER_BYTES + 8u + source_size +
                       destination_size) return 0;
    payload = out + PXA_HEADER_BYTES;
    if (!pxa_wire_record_encode(payload,
                                capacity - PXA_HEADER_BYTES,
                                1, (const uint8_t *)source, source_size,
                                &first_size) ||
        !pxa_wire_record_encode(payload + first_size,
                                capacity - PXA_HEADER_BYTES - first_size,
                                2, (const uint8_t *)destination,
                                destination_size, &second_size)) return 0;
    return pxa_build_message(out, capacity, PXA_FS_SERVICE,
                                PXA_FS_RENAME, token, payload,
                                first_size + second_size, written);
}

static inline int pxa_fs_build_seek(
    uint8_t *out, size_t capacity, uint64_t token,
    uint64_t handle, int64_t offset, uint8_t origin,
    uint32_t *written) {
    uint8_t payload[17];
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL || token == 0 ||
        (handle >> 32) == 0 || origin > PXA_FS_SEEK_END) return 0;
    pxa_store_u64(payload, handle);
    pxa_store_u64(payload + 8, (uint64_t)offset);
    payload[16] = origin;
    return pxa_build_message(out, capacity, PXA_FS_SERVICE,
                                PXA_FS_SEEK, token, payload,
                                sizeof(payload), written);
}

static inline int32_t pxa_fs_request_seek(
    uint64_t token, uint64_t handle, int64_t offset, uint8_t origin) {
    uint8_t packet[PXA_HEADER_BYTES + 17u];
    uint32_t size = 0;
    if (!pxa_fs_build_seek(packet, sizeof(packet), token,
                              handle, offset, origin, &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int32_t pxa_fs_request_read_directory(
    uint64_t token, uint64_t handle) {
    uint8_t packet[PXA_HEADER_BYTES + 8u];
    uint8_t payload[8];
    uint32_t size = 0;
    if (token == 0 || (handle >> 32) == 0) return -1;
    pxa_store_u64(payload, handle);
    if (!pxa_build_message(packet, sizeof(packet), PXA_FS_SERVICE,
                              PXA_FS_READ_DIRECTORY, token, payload,
                              sizeof(payload), &size)) return -1;
    return pxa_submit(packet, size);
}

static inline int32_t pxa_fs_read(uint64_t handle, uint8_t *data,
                                     uint32_t capacity) {
    return (handle >> 32) == 0 || (data == NULL && capacity != 0)
               ? -1 : pxa_io(handle, PXA_FS_IO_READ, data, capacity);
}

static inline int32_t pxa_fs_write(uint64_t handle, uint8_t *data,
                                      uint32_t size) {
    return (handle >> 32) == 0 || (data == NULL && size != 0)
               ? -1 : pxa_io(handle, PXA_FS_IO_WRITE, data, size);
}

static inline int32_t pxa_fs_close(uint64_t handle) {
    return (handle >> 32) == 0 ? -1 : pxa_close_handle(handle);
}

static inline int pxa_fs_parse_status(
    const pxa_event_t *event, uint64_t token, uint16_t opcode,
    int32_t *status) {
    if (status == NULL || event == NULL || token == 0 ||
        event->service != PXA_FS_SERVICE || event->opcode != opcode ||
        event->token != token || event->payload == NULL ||
        event->payload_size != 4) return 0;
    *status = (int32_t)pxa_load_u32(event->payload);
    return 1;
}

static inline int pxa_fs_parse_open(
    const pxa_event_t *event, uint64_t token,
    pxa_fs_open_result_t *output) {
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || token == 0 ||
        event->service != PXA_FS_SERVICE ||
        event->opcode != PXA_FS_OPEN || event->token != token ||
        event->payload == NULL || event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status != 0) return event->payload_size == 4;
    if (event->payload_size != 12) return 0;
    output->handle = pxa_load_u64(event->payload + 4);
    return (output->handle >> 32) != 0;
}

static inline int pxa_fs_parse_stat(
    const pxa_event_t *event, uint64_t token,
    pxa_fs_stat_result_t *output) {
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || token == 0 ||
        event->service != PXA_FS_SERVICE ||
        event->opcode != PXA_FS_STAT || event->token != token ||
        event->payload == NULL || event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status != 0) return event->payload_size == 4;
    if (event->payload_size != 13 ||
        (event->payload[4] != PXA_FS_KIND_REGULAR &&
         event->payload[4] != PXA_FS_KIND_DIRECTORY)) return 0;
    output->kind = event->payload[4];
    output->size = pxa_load_u64(event->payload + 5);
    return 1;
}

static inline int pxa_fs_parse_seek(
    const pxa_event_t *event, uint64_t token,
    pxa_fs_seek_result_t *output) {
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || token == 0 ||
        event->service != PXA_FS_SERVICE ||
        event->opcode != PXA_FS_SEEK || event->token != token ||
        event->payload == NULL || event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status != 0) return event->payload_size == 4;
    if (event->payload_size != 12) return 0;
    output->position = pxa_load_u64(event->payload + 4);
    return 1;
}

static inline int pxa_fs_parse_directory(
    const pxa_event_t *event, uint64_t token,
    pxa_fs_directory_result_t *output) {
    pxa_wire_record_view_t record;
    size_t consumed = 0;
    size_t offset = 4;
    if (output == NULL) return 0;
    pxa_zero(output, sizeof(*output));
    if (event == NULL || token == 0 ||
        event->service != PXA_FS_SERVICE ||
        event->opcode != PXA_FS_READ_DIRECTORY ||
        event->token != token || event->payload == NULL ||
        event->payload_size < 4) return 0;
    output->status = (int32_t)pxa_load_u32(event->payload);
    if (output->status != 0) return event->payload_size == 4;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed)) return 0;
    if (record.raw_tag == 7 && record.payload_size == 0 &&
        offset + consumed == event->payload_size) {
        output->end = 1;
        return 1;
    }
    if (record.raw_tag != 4 ||
        !pxa_fs_valid_path((const char *)record.payload,
                           record.payload_size) ||
        pxa_fs_contains_byte(record.payload, record.payload_size, '/'))
        return 0;
    output->name = record.payload;
    output->name_size = record.payload_size;
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) ||
        record.raw_tag != 5 || record.payload_size != 1 ||
        (record.payload[0] != PXA_FS_KIND_REGULAR &&
         record.payload[0] != PXA_FS_KIND_DIRECTORY)) return 0;
    output->kind = record.payload[0];
    offset += consumed;
    if (!pxa_wire_record_decode(event->payload + offset,
                                event->payload_size - offset,
                                &record, &consumed) ||
        record.raw_tag != 6 || record.payload_size != 8 ||
        offset + consumed != event->payload_size) return 0;
    output->size = pxa_load_u64(record.payload);
    return 1;
}

#endif

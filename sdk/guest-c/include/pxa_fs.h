#ifndef PXA_FS_H
#define PXA_FS_H

#include "pxa.h"

#define PXA_FS_OPEN 1u
#define PXA_FS_MAKE_DIRECTORY 2u
#define PXA_FS_REMOVE 3u
#define PXA_FS_RENAME 4u
#define PXA_FS_STAT 5u
#define PXA_FS_SEEK 6u
#define PXA_FS_READ_DIRECTORY 7u

#define PXA_FS_PATH 1u
#define PXA_FS_DESTINATION_PATH 2u
#define PXA_FS_OPEN_FLAGS 3u
#define PXA_FS_ENTRY_NAME 4u
#define PXA_FS_ENTRY_KIND 5u
#define PXA_FS_ENTRY_SIZE 6u
#define PXA_FS_END_OF_DIRECTORY 7u

#define PXA_FS_OPEN_READ (1u << 0)
#define PXA_FS_OPEN_WRITE (1u << 1)
#define PXA_FS_OPEN_CREATE (1u << 2)
#define PXA_FS_OPEN_EXCLUSIVE (1u << 3)
#define PXA_FS_OPEN_TRUNCATE (1u << 4)
#define PXA_FS_OPEN_APPEND (1u << 5)
#define PXA_FS_OPEN_DIRECTORY (1u << 6)

#define PXA_FS_KIND_REGULAR 1u
#define PXA_FS_KIND_DIRECTORY 2u

#define PXA_FS_SEEK_START 0u
#define PXA_FS_SEEK_CURRENT 1u
#define PXA_FS_SEEK_END 2u

typedef struct {
    int32_t status;
    uint32_t handle;
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

typedef struct {
    int32_t status;
    const uint8_t* name;
    uint16_t name_length;
    uint8_t kind;
    uint64_t size;
    uint8_t end;
} pxa_fs_directory_result_t;

static inline int pxa_fs_equal_bytes(const void* left, const void* right,
                                     size_t length) {
    const uint8_t* left_bytes = (const uint8_t*)left;
    const uint8_t* right_bytes = (const uint8_t*)right;
    if ((left_bytes == NULL || right_bytes == NULL) && length != 0) return 0;
    for (size_t index = 0; index < length; ++index)
        if (left_bytes[index] != right_bytes[index]) return 0;
    return 1;
}

static inline int pxa_fs_contains_byte(const uint8_t* value, size_t length,
                                       uint8_t needle) {
    if (value == NULL && length != 0) return 0;
    for (size_t index = 0; index < length; ++index)
        if (value[index] == needle) return 1;
    return 0;
}

static inline int pxa_fs_valid_utf8(const char* text, size_t length) {
    size_t index = 0;
    if (text == NULL) return 0;
    while (index < length) {
        const uint8_t first = (uint8_t)text[index++];
        if (first < 0x80) continue;
        uint32_t codepoint;
        uint32_t minimum;
        size_t continuation;
        if ((first & 0xe0) == 0xc0) {
            codepoint = first & 0x1f;
            minimum = 0x80;
            continuation = 1;
        } else if ((first & 0xf0) == 0xe0) {
            codepoint = first & 0x0f;
            minimum = 0x800;
            continuation = 2;
        } else if ((first & 0xf8) == 0xf0) {
            codepoint = first & 0x07;
            minimum = 0x10000;
            continuation = 3;
        } else {
            return 0;
        }
        if (continuation > length - index) return 0;
        for (size_t count = 0; count < continuation; ++count) {
            const uint8_t next = (uint8_t)text[index++];
            if ((next & 0xc0) != 0x80) return 0;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return 0;
    }
    return 1;
}

static inline int pxa_fs_valid_path(const char* path, size_t length) {
    size_t segment_start = 0;
    if (path == NULL || length == 0 || length > 255 || path[0] == '/' ||
        path[length - 1] == '/') return 0;
    for (size_t index = 0; index <= length; ++index) {
        if (index != length && path[index] != '/') {
            const uint8_t byte = (uint8_t)path[index];
            if (byte == 0 || byte < 0x20 || byte == 0x7f || byte == '\\') return 0;
            continue;
        }
        const size_t segment_length = index - segment_start;
        if (segment_length == 0 || segment_length > 64 ||
            (segment_length == 1 && path[segment_start] == '.') ||
            (segment_length == 2 && path[segment_start] == '.' &&
             path[segment_start + 1] == '.') ||
            (segment_length >= 5 && path[segment_start] == '.' &&
             path[segment_start + 1] == 'p' && path[segment_start + 2] == 'x' &&
             path[segment_start + 3] == 'a' && path[segment_start + 4] == '-')) return 0;
        segment_start = index + 1;
    }
    return pxa_fs_valid_utf8(path, length);
}

static inline int pxa_fs_send(uint16_t opcode, uint32_t request_id,
                              const uint8_t* payload, size_t payload_length,
                              uint8_t* packet, size_t packet_capacity) {
    pxa_writer_t writer;
    if (request_id == 0 || packet == NULL || payload_length > UINT32_MAX ||
        packet_capacity < 12 || payload_length > packet_capacity - 12) return 0;
    pxa_writer_init(&writer, packet, packet_capacity);
    return pxa_message(&writer, PXA_SERVICE_FS, opcode, request_id, payload,
                       payload_length) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int pxa_fs_open(uint32_t request_id, const char* path,
                              size_t path_length, uint32_t flags,
                              uint8_t* payload, size_t payload_capacity,
                              uint8_t* packet, size_t packet_capacity) {
    pxa_writer_t writer;
    uint8_t encoded_flags[4];
    const uint32_t known = PXA_FS_OPEN_READ | PXA_FS_OPEN_WRITE |
        PXA_FS_OPEN_CREATE | PXA_FS_OPEN_EXCLUSIVE | PXA_FS_OPEN_TRUNCATE |
        PXA_FS_OPEN_APPEND | PXA_FS_OPEN_DIRECTORY;
    const uint8_t directory = (flags & PXA_FS_OPEN_DIRECTORY) != 0;
    const uint8_t readable = (flags & PXA_FS_OPEN_READ) != 0;
    const uint8_t writable = (flags & PXA_FS_OPEN_WRITE) != 0;
    if (!pxa_fs_valid_path(path, path_length) || flags == 0 ||
        (flags & ~known) != 0 ||
        (directory && flags != (PXA_FS_OPEN_READ | PXA_FS_OPEN_DIRECTORY)) ||
        (!directory && !readable && !writable) ||
        ((flags & PXA_FS_OPEN_EXCLUSIVE) != 0 &&
         (flags & PXA_FS_OPEN_CREATE) == 0) ||
        ((flags & (PXA_FS_OPEN_TRUNCATE | PXA_FS_OPEN_APPEND)) != 0 &&
         !writable) || payload == NULL) return 0;
    encoded_flags[0] = (uint8_t)flags;
    encoded_flags[1] = (uint8_t)(flags >> 8);
    encoded_flags[2] = (uint8_t)(flags >> 16);
    encoded_flags[3] = (uint8_t)(flags >> 24);
    pxa_writer_init(&writer, payload, payload_capacity);
    return pxa_record(&writer, PXA_FS_PATH, (const uint8_t*)path, path_length) &&
           pxa_record(&writer, PXA_FS_OPEN_FLAGS, encoded_flags,
                      sizeof(encoded_flags)) &&
           pxa_fs_send(PXA_FS_OPEN, request_id, writer.data, writer.length,
                       packet, packet_capacity);
}

static inline int pxa_fs_path_request(uint16_t opcode, uint32_t request_id,
                                      const char* path, size_t path_length,
                                      uint8_t* payload, size_t payload_capacity,
                                      uint8_t* packet, size_t packet_capacity) {
    pxa_writer_t writer;
    if (!pxa_fs_valid_path(path, path_length) || payload == NULL) return 0;
    pxa_writer_init(&writer, payload, payload_capacity);
    return pxa_record(&writer, PXA_FS_PATH, (const uint8_t*)path, path_length) &&
           pxa_fs_send(opcode, request_id, writer.data, writer.length,
                       packet, packet_capacity);
}

static inline int pxa_fs_make_directory(uint32_t request_id, const char* path,
                                        size_t path_length, uint8_t* payload,
                                        size_t payload_capacity, uint8_t* packet,
                                        size_t packet_capacity) {
    return pxa_fs_path_request(PXA_FS_MAKE_DIRECTORY, request_id, path, path_length,
                               payload, payload_capacity, packet, packet_capacity);
}

static inline int pxa_fs_remove(uint32_t request_id, const char* path,
                                size_t path_length, uint8_t* payload,
                                size_t payload_capacity, uint8_t* packet,
                                size_t packet_capacity) {
    return pxa_fs_path_request(PXA_FS_REMOVE, request_id, path, path_length,
                               payload, payload_capacity, packet, packet_capacity);
}

static inline int pxa_fs_stat(uint32_t request_id, const char* path,
                              size_t path_length, uint8_t* payload,
                              size_t payload_capacity, uint8_t* packet,
                              size_t packet_capacity) {
    return pxa_fs_path_request(PXA_FS_STAT, request_id, path, path_length,
                               payload, payload_capacity, packet, packet_capacity);
}

static inline int pxa_fs_rename(uint32_t request_id, const char* source,
                                size_t source_length, const char* destination,
                                size_t destination_length, uint8_t* payload,
                                size_t payload_capacity, uint8_t* packet,
                                size_t packet_capacity) {
    pxa_writer_t writer;
    if (!pxa_fs_valid_path(source, source_length) ||
        !pxa_fs_valid_path(destination, destination_length) ||
        (source_length == destination_length &&
         pxa_fs_equal_bytes(source, destination, source_length)) ||
        payload == NULL) return 0;
    pxa_writer_init(&writer, payload, payload_capacity);
    return pxa_record(&writer, PXA_FS_PATH, (const uint8_t*)source, source_length) &&
           pxa_record(&writer, PXA_FS_DESTINATION_PATH, (const uint8_t*)destination,
                      destination_length) &&
           pxa_fs_send(PXA_FS_RENAME, request_id, writer.data, writer.length,
                       packet, packet_capacity);
}

static inline int pxa_fs_close(uint32_t handle, uint8_t* packet,
                               size_t packet_capacity) {
    pxa_writer_t writer;
    uint8_t payload[4];
    if (handle == 0 || packet == NULL || packet_capacity < 16) return 0;
    payload[0] = (uint8_t)handle;
    payload[1] = (uint8_t)(handle >> 8);
    payload[2] = (uint8_t)(handle >> 16);
    payload[3] = (uint8_t)(handle >> 24);
    pxa_writer_init(&writer, packet, packet_capacity);
    return pxa_message(&writer, PXA_SERVICE_CORE, PXA_CORE_CLOSE_HANDLE, 0, payload,
                       sizeof(payload)) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int32_t pxa_fs_read(uint32_t handle, uint8_t* data,
                                  uint32_t capacity) {
    return handle == 0 || (data == NULL && capacity != 0)
               ? PXA_STATUS_INVALID_ARGUMENT
               : pxa_io(handle, PXA_IO_READ, data, capacity);
}

static inline int32_t pxa_fs_write(uint32_t handle, uint8_t* data,
                                   uint32_t length) {
    return handle == 0 || (data == NULL && length != 0)
               ? PXA_STATUS_INVALID_ARGUMENT
               : pxa_io(handle, PXA_IO_WRITE, data, length);
}

static inline int pxa_fs_seek(uint32_t request_id, uint32_t handle,
                              int64_t offset, uint8_t origin,
                              uint8_t* packet, size_t packet_capacity) {
    uint8_t payload[13];
    if (handle == 0 || origin > PXA_FS_SEEK_END || request_id == 0 ||
        packet == NULL || packet_capacity < sizeof(payload) + 12) return 0;
    payload[0] = (uint8_t)handle;
    payload[1] = (uint8_t)(handle >> 8);
    payload[2] = (uint8_t)(handle >> 16);
    payload[3] = (uint8_t)(handle >> 24);
    for (size_t index = 0; index < 8; ++index)
        payload[4 + index] = (uint8_t)((uint64_t)offset >> (index * 8));
    payload[12] = origin;
    return pxa_fs_send(PXA_FS_SEEK, request_id, payload, sizeof(payload),
                       packet, packet_capacity);
}

static inline int pxa_fs_read_directory(uint32_t request_id, uint32_t handle,
                                        uint8_t* packet, size_t packet_capacity) {
    uint8_t payload[4];
    if (handle == 0) return 0;
    payload[0] = (uint8_t)handle;
    payload[1] = (uint8_t)(handle >> 8);
    payload[2] = (uint8_t)(handle >> 16);
    payload[3] = (uint8_t)(handle >> 24);
    return pxa_fs_send(PXA_FS_READ_DIRECTORY, request_id, payload,
                       sizeof(payload), packet, packet_capacity);
}

static inline int pxa_fs_parse_open_result(const pxa_event_t* event,
                                           pxa_fs_open_result_t* output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_FS ||
        event->opcode != PXA_FS_OPEN || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 8) return 0;
    output->handle = pxa_read_u32(event->payload + 4);
    return output->handle != 0;
}

static inline int pxa_fs_parse_status_result(const pxa_event_t* event,
                                             uint16_t opcode, int32_t* status) {
    if (event == NULL || status == NULL || event->service != PXA_SERVICE_FS ||
        event->opcode != opcode || event->request_id == 0 ||
        event->payload_length != 4) return 0;
    *status = (int32_t)pxa_read_u32(event->payload);
    return 1;
}

static inline int pxa_fs_parse_stat_result(const pxa_event_t* event,
                                           pxa_fs_stat_result_t* output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_FS ||
        event->opcode != PXA_FS_STAT || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 13 ||
        (event->payload[4] != PXA_FS_KIND_REGULAR &&
         event->payload[4] != PXA_FS_KIND_DIRECTORY)) return 0;
    output->kind = event->payload[4];
    output->size = pxa_read_u64(event->payload + 5);
    return 1;
}

static inline int pxa_fs_parse_seek_result(const pxa_event_t* event,
                                           pxa_fs_seek_result_t* output) {
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_FS ||
        event->opcode != PXA_FS_SEEK || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    if (event->payload_length != 12) return 0;
    output->position = pxa_read_u64(event->payload + 4);
    return 1;
}

static inline int pxa_fs_parse_directory_result(
    const pxa_event_t* event, pxa_fs_directory_result_t* output) {
    size_t offset = 4;
    uint16_t previous_tag = 0;
    uint8_t seen_name = 0;
    uint8_t seen_kind = 0;
    uint8_t seen_size = 0;
    if (event == NULL || output == NULL || event->service != PXA_SERVICE_FS ||
        event->opcode != PXA_FS_READ_DIRECTORY || event->request_id == 0 ||
        event->payload_length < 4) return 0;
    output->status = (int32_t)pxa_read_u32(event->payload);
    output->name = NULL;
    output->name_length = 0;
    output->kind = 0;
    output->size = 0;
    output->end = 0;
    if (output->status != PXA_STATUS_OK) return event->payload_length == 4;
    while (offset < event->payload_length) {
        uint16_t tag;
        uint16_t length;
        const uint8_t* value;
        if (event->payload_length - offset < 4) return 0;
        tag = pxa_read_u16(event->payload + offset);
        length = pxa_read_u16(event->payload + offset + 2);
        offset += 4;
        if (tag <= previous_tag || length > event->payload_length - offset) return 0;
        previous_tag = tag;
        value = event->payload + offset;
        offset += length;
        if (tag == PXA_FS_END_OF_DIRECTORY) {
            if (length != 0 || seen_name || seen_kind || seen_size || offset != event->payload_length)
                return 0;
            output->end = 1;
            return 1;
        }
        if (tag == PXA_FS_ENTRY_NAME) {
            if (seen_name || !pxa_fs_valid_path((const char*)value, length) ||
                pxa_fs_contains_byte(value, length, '/')) return 0;
            output->name = value;
            output->name_length = length;
            seen_name = 1;
        } else if (tag == PXA_FS_ENTRY_KIND) {
            if (seen_kind || length != 1 ||
                (value[0] != PXA_FS_KIND_REGULAR && value[0] != PXA_FS_KIND_DIRECTORY))
                return 0;
            output->kind = value[0];
            seen_kind = 1;
        } else if (tag == PXA_FS_ENTRY_SIZE) {
            if (seen_size || length != 8) return 0;
            output->size = pxa_read_u64(value);
            seen_size = 1;
        } else {
            return 0;
        }
    }
    return seen_name && seen_kind && seen_size;
}

#endif

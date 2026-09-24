#ifndef PXA_STORE_INSTALLER_H
#define PXA_STORE_INSTALLER_H

#include "pxa.h"

#define PXA_SERVICE_STORE_INSTALLER 20u
#define PXA_STORE_INSTALL_REQUEST 1u
#define PXA_STORE_DOWNLOAD_REQUEST 2u
#define PXA_STORE_INSTALL_FILE_REQUEST 3u
#define PXA_STORE_INSTALLED_LIST_REQUEST 4u
#define PXA_STORE_DOWNLOAD_LIST_REQUEST 5u
#define PXA_STORE_DELETE_REQUEST 6u
#define PXA_STORE_LAUNCH_REQUEST 7u
#define PXA_STORE_UNINSTALL_REQUEST 8u
#define PXA_STORE_DOWNLOAD_PROGRESS 0x8001u

#define PXA_STORE_INSTALLED_MAX 12u
#define PXA_STORE_DOWNLOAD_MAX 8u
typedef struct {
    char app_id[65];
    char version[32];
    char identity[130];
    uint64_t sequence;
    uint8_t uninstallable;
} pxa_store_installed_app_t;
typedef struct {
    char filename[20];
    char app_id[65];
} pxa_store_download_entry_t;

static inline int pxa_store_parse_download_progress(const pxa_event_t *event,
                                                      uint32_t *request_id,
                                                      uint64_t *received,
                                                      uint64_t *total) {
    if (event == NULL || request_id == NULL || received == NULL || total == NULL ||
        event->service != PXA_SERVICE_STORE_INSTALLER ||
        event->opcode != PXA_STORE_DOWNLOAD_PROGRESS || event->request_id != 0 ||
        event->payload == NULL || event->payload_length != 20u) return 0;
    *request_id = pxa_read_u32(event->payload);
    *received = pxa_read_u64(event->payload + 4u);
    *total = pxa_read_u64(event->payload + 12u);
    return *request_id != 0u && *total != 0u && *received <= *total;
}

static inline void pxa_store_copy_bytes(char *output, const uint8_t *input,
                                        size_t length) {
    for (size_t index = 0; index < length; ++index)
        output[index] = (char)input[index];
}

static inline int pxa_store_request_package(uint16_t opcode,
                                             uint32_t request_id,
                                             const char *app_id,
                                             const char *ticket,
                                             const char *sha256,
                                             uint64_t size, uint8_t *packet,
                                             size_t packet_capacity) {
    uint8_t payload[43u + 64u + 255u];
    size_t app_size = 0;
    size_t ticket_size = 0;
    pxa_writer_t writer;
    if ((opcode != PXA_STORE_INSTALL_REQUEST &&
         opcode != PXA_STORE_DOWNLOAD_REQUEST) || request_id == 0 ||
        app_id == NULL || ticket == NULL || sha256 == NULL ||
        size == 0 || size > UINT64_C(4194304) || packet == NULL) return 0;
    while (app_size <= 64u && app_id[app_size] != '\0') ++app_size;
    while (ticket_size <= 255u && ticket[ticket_size] != '\0') ++ticket_size;
    if (app_size == 0 || app_size > 64u || ticket_size == 0 ||
        ticket_size > 255u) return 0;
    size_t digest_size = 0;
    while (digest_size <= 64u && sha256[digest_size] != '\0') ++digest_size;
    if (digest_size != 64u) return 0;
    payload[0] = (uint8_t)app_size;
    payload[1] = (uint8_t)ticket_size;
    payload[2] = (uint8_t)(ticket_size >> 8);
    for (size_t index = 0; index < 8u; ++index)
        payload[3u + index] = (uint8_t)(size >> (index * 8u));
    for (size_t index = 0; index < 32u; ++index) {
        uint8_t high = (uint8_t)sha256[index * 2u];
        uint8_t low = (uint8_t)sha256[index * 2u + 1u];
        if (high >= '0' && high <= '9') high -= '0';
        else if (high >= 'a' && high <= 'f') high = high - 'a' + 10u;
        else return 0;
        if (low >= '0' && low <= '9') low -= '0';
        else if (low >= 'a' && low <= 'f') low = low - 'a' + 10u;
        else return 0;
        payload[11u + index] = (uint8_t)((high << 4) | low);
    }
    for (size_t index = 0; index < app_size; ++index)
        payload[43u + index] = (uint8_t)app_id[index];
    for (size_t index = 0; index < ticket_size; ++index)
        payload[43u + app_size + index] = (uint8_t)ticket[index];
    pxa_writer_init(&writer, packet, packet_capacity);
    return pxa_message(&writer, PXA_SERVICE_STORE_INSTALLER,
                       opcode, request_id, payload,
                       43u + app_size + ticket_size) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int pxa_store_install(uint32_t request_id, const char *app_id,
                                    const char *ticket, const char *sha256,
                                    uint64_t size, uint8_t *packet,
                                    size_t packet_capacity) {
    return pxa_store_request_package(PXA_STORE_INSTALL_REQUEST, request_id,
                                     app_id, ticket, sha256, size, packet,
                                     packet_capacity);
}

static inline int pxa_store_download(uint32_t request_id, const char *app_id,
                                     const char *ticket, const char *sha256,
                                     uint64_t size, uint8_t *packet,
                                     size_t packet_capacity) {
    return pxa_store_request_package(PXA_STORE_DOWNLOAD_REQUEST, request_id,
                                     app_id, ticket, sha256, size, packet,
                                     packet_capacity);
}

static inline int pxa_store_filename_valid(const char *filename) {
    size_t length = 0;
    if (filename == NULL) return 0;
    while (length < 20u && filename[length] != '\0') ++length;
    if (length != 19u || filename[19] != '\0' ||
        filename[0] != '.' || filename[1] != 's' ||
        filename[2] != 't' || filename[3] != 'o' ||
        filename[4] != 'r' || filename[5] != 'e' ||
        filename[6] != '-' ||
        filename[15] != '.' || filename[16] != 'p' ||
        filename[17] != 'x' || filename[18] != 'a') return 0;
    for (size_t index = 7u; index < 15u; ++index) {
        char digit = filename[index];
        if (!((digit >= '0' && digit <= '9') ||
              (digit >= 'a' && digit <= 'f'))) return 0;
    }
    return 1;
}

static inline int pxa_store_install_file(uint32_t request_id,
                                         const char *filename,
                                         uint8_t *packet,
                                         size_t packet_capacity) {
    pxa_writer_t writer;
    if (request_id == 0 || packet == NULL ||
        !pxa_store_filename_valid(filename)) return 0;
    pxa_writer_init(&writer, packet, packet_capacity);
    return pxa_message(&writer, PXA_SERVICE_STORE_INSTALLER,
                       PXA_STORE_INSTALL_FILE_REQUEST, request_id,
                       (const uint8_t *)filename, 19u) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int pxa_store_manage(uint16_t opcode, uint32_t request_id,
                                   const char *value, uint8_t *packet,
                                   size_t capacity) {
    pxa_writer_t writer;
    size_t length = 0;
    if (request_id == 0 || packet == NULL ||
        (opcode != PXA_STORE_INSTALLED_LIST_REQUEST &&
         opcode != PXA_STORE_DOWNLOAD_LIST_REQUEST &&
         opcode != PXA_STORE_DELETE_REQUEST &&
         opcode != PXA_STORE_LAUNCH_REQUEST &&
         opcode != PXA_STORE_UNINSTALL_REQUEST)) return 0;
    if (opcode == PXA_STORE_DELETE_REQUEST) {
        if (!pxa_store_filename_valid(value)) return 0;
        length = 19u;
    } else if (opcode == PXA_STORE_LAUNCH_REQUEST ||
               opcode == PXA_STORE_UNINSTALL_REQUEST) {
        if (value == NULL) return 0;
        while (length < 65u && value[length] != '\0') ++length;
        if (length == 0 || length > 64u) return 0;
    }
    pxa_writer_init(&writer, packet, capacity);
    return pxa_message(&writer, PXA_SERVICE_STORE_INSTALLER, opcode,
                       request_id, (const uint8_t *)value, length) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int pxa_store_parse_installed(const pxa_event_t *event,
                                            pxa_store_installed_app_t *apps,
                                            size_t capacity, size_t *count) {
    size_t offset = 5u;
    if (event == NULL || apps == NULL || count == NULL ||
        event->service != PXA_SERVICE_STORE_INSTALLER ||
        event->opcode != PXA_STORE_INSTALLED_LIST_REQUEST ||
        event->payload == NULL || event->payload_length < 5u ||
        (int32_t)pxa_read_u32(event->payload) != PXA_STATUS_OK ||
        event->payload[4] > capacity) return 0;
    *count = event->payload[4];
    for (size_t index = 0; index < *count; ++index) {
        size_t app_length, version_length, identity_length;
        if (offset + 12u > event->payload_length) return 0;
        app_length = event->payload[offset++];
        version_length = event->payload[offset++];
        identity_length = event->payload[offset++];
        if (app_length == 0 || app_length > 64u || version_length > 31u ||
            identity_length == 0 || identity_length > 129u ||
            offset + 9u + app_length + version_length + identity_length >
                event->payload_length) return 0;
        apps[index].sequence = 0;
        for (size_t byte = 0; byte < 8u; ++byte)
            apps[index].sequence |= (uint64_t)event->payload[offset++] << (byte * 8u);
        apps[index].uninstallable = event->payload[offset++] & 1u;
        pxa_store_copy_bytes(apps[index].app_id, event->payload + offset, app_length);
        apps[index].app_id[app_length] = '\0'; offset += app_length;
        pxa_store_copy_bytes(apps[index].version, event->payload + offset, version_length);
        apps[index].version[version_length] = '\0'; offset += version_length;
        pxa_store_copy_bytes(apps[index].identity, event->payload + offset, identity_length);
        apps[index].identity[identity_length] = '\0'; offset += identity_length;
    }
    return offset == event->payload_length;
}

static inline int pxa_store_parse_downloads(const pxa_event_t *event,
                                             pxa_store_download_entry_t *entries,
                                             size_t capacity, size_t *count) {
    size_t offset = 5u;
    if (event == NULL || entries == NULL || count == NULL ||
        event->service != PXA_SERVICE_STORE_INSTALLER ||
        event->opcode != PXA_STORE_DOWNLOAD_LIST_REQUEST ||
        event->payload == NULL || event->payload_length < 5u ||
        (int32_t)pxa_read_u32(event->payload) != PXA_STATUS_OK ||
        event->payload[4] > capacity) return 0;
    *count = event->payload[4];
    for (size_t index = 0; index < *count; ++index) {
        size_t length;
        if (offset + 20u > event->payload_length) return 0;
        length = event->payload[offset++];
        if (length == 0 || length > 64u || offset + 19u + length > event->payload_length)
            return 0;
        pxa_store_copy_bytes(entries[index].filename, event->payload + offset, 19u);
        entries[index].filename[19] = '\0'; offset += 19u;
        if (!pxa_store_filename_valid(entries[index].filename)) return 0;
        pxa_store_copy_bytes(entries[index].app_id, event->payload + offset, length);
        entries[index].app_id[length] = '\0'; offset += length;
    }
    return offset == event->payload_length;
}

static inline int pxa_store_download_result(const pxa_event_t *event,
                                             int32_t *status,
                                             char filename[20]) {
    if (event == NULL || status == NULL || filename == NULL ||
        event->service != PXA_SERVICE_STORE_INSTALLER ||
        event->opcode != PXA_STORE_DOWNLOAD_REQUEST ||
        event->request_id == 0 || event->payload == NULL ||
        event->payload_length < 4u) return 0;
    *status = (int32_t)pxa_read_u32(event->payload);
    if (*status != PXA_STATUS_OK) return event->payload_length == 4u;
    if (event->payload_length != 23u) return 0;
    for (size_t index = 0; index < 19u; ++index)
        filename[index] = (char)event->payload[4u + index];
    filename[19] = '\0';
    return pxa_store_filename_valid(filename);
}

static inline int pxa_store_install_result(const pxa_event_t *event,
                                            int32_t *status) {
    if (event == NULL || status == NULL ||
        event->service != PXA_SERVICE_STORE_INSTALLER ||
        (event->opcode != PXA_STORE_INSTALL_REQUEST &&
         event->opcode != PXA_STORE_INSTALL_FILE_REQUEST) ||
        event->request_id == 0 || event->payload == NULL ||
        event->payload_length != 4u) return 0;
    *status = (int32_t)pxa_read_u32(event->payload);
    return 1;
}

static inline int pxa_store_uninstall_result(const pxa_event_t *event,
                                              int32_t *status) {
    if (event == NULL || status == NULL ||
        event->service != PXA_SERVICE_STORE_INSTALLER ||
        event->opcode != PXA_STORE_UNINSTALL_REQUEST ||
        event->request_id == 0 || event->payload == NULL ||
        event->payload_length != 4u) return 0;
    *status = (int32_t)pxa_read_u32(event->payload);
    return 1;
}
#endif

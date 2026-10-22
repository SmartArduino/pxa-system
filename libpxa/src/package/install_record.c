#include "pxa/install_record.h"
#include "pxa/container.h"
#include <string.h>

static int nonzero(const uint8_t *bytes, size_t size) {
    uint8_t value = 0;
    size_t i;
    for (i = 0; i < size; ++i) value |= bytes[i];
    return value != 0;
}

static int app_length(const char *id, size_t *length) {
    size_t i;
    if (id[0] < 'a' || id[0] > 'z') return 0;
    for (i = 0; i < PXA_INSTALL_APP_ID_BYTES; ++i) {
        unsigned char c = (unsigned char)id[i];
        if (!c) { *length = i; return 1; }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.')) return 0;
    }
    return 0;
}

static int valid(const pxa_install_record_t *record, size_t *length) {
    return record && record->generation &&
        !(record->flags & ~PXA_INSTALL_RECORD_FLAGS) &&
        nonzero(record->install_id, sizeof(record->install_id)) &&
        nonzero(record->publisher_root, sizeof(record->publisher_root)) &&
        nonzero(record->app_volume.bytes, sizeof(record->app_volume.bytes)) &&
        nonzero(record->manifest_sha256, sizeof(record->manifest_sha256)) &&
        app_length(record->app_id, length) &&
        (!(record->flags & PXA_INSTALL_RECORD_HAS_MEDIA) ||
         nonzero(record->media_volume.bytes, sizeof(record->media_volume.bytes))) &&
        (!(record->flags & PXA_INSTALL_RECORD_MANAGED_MEDIA) ||
         ((record->flags & PXA_INSTALL_RECORD_HAS_MEDIA) &&
          nonzero(record->media_set_sha256, sizeof(record->media_set_sha256))));
}

pxa_status_t pxa_install_record_encode(const pxa_install_record_t *record,
    uint8_t output[PXA_INSTALL_RECORD_BYTES]) {
    size_t length;
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, PXA_INSTALL_RECORD_BYTES);
    if (!valid(record, &length)) return PXA_STATUS_INVALID_ARGUMENT;
    memcpy(output, "PXIR", 4);
    pxa_write_u16(output + 4, 2);
    pxa_write_u16(output + 6, PXA_INSTALL_RECORD_BYTES);
    pxa_write_u64(output + 8, record->generation);
    pxa_write_u32(output + 16, record->flags);
    pxa_write_u16(output + 20, (uint16_t)length);
    memcpy(output + 24, record->install_id, 16);
    memcpy(output + 40, record->publisher_root, 32);
    memcpy(output + 72, record->app_volume.bytes, 16);
    memcpy(output + 88, record->media_volume.bytes, 16);
    memcpy(output + 104, record->manifest_sha256, 32);
    memcpy(output + 136, record->media_set_sha256, 32);
    pxa_write_u64(output + 168, record->release_sequence);
    memcpy(output + 176, record->app_id, length);
    pxa_write_u64(output + 240, record->data_generation);
    pxa_write_u32(output + PXA_INSTALL_RECORD_BYTES-4,
        pxa_container_crc32(output, PXA_INSTALL_RECORD_BYTES-4));
    return PXA_STATUS_OK;
}

pxa_status_t pxa_install_record_decode(pxa_bytes_t bytes,
    const pxa_volume_id_t *expected, pxa_install_record_t *output) {
    pxa_install_record_t record = {0};
    size_t length, actual, i;
    const uint8_t *p = bytes.data;
    if (!output || !expected) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (!p || (bytes.size != PXA_INSTALL_RECORD_BYTES && bytes.size != PXA_INSTALL_RECORD_V1_BYTES) ||
        memcmp(p, "PXIR", 4) ||
        pxa_read_u16(p + 4) != (bytes.size==PXA_INSTALL_RECORD_V1_BYTES?1:2) ||
        pxa_read_u16(p + 6) != bytes.size ||
        pxa_read_u16(p + 22) != 0 ||
        pxa_read_u32(p + bytes.size-4) != pxa_container_crc32(p, bytes.size-4))
        return PXA_STATUS_DENIED;
    length = pxa_read_u16(p + 20);
    if (!length || length >= PXA_INSTALL_APP_ID_BYTES) return PXA_STATUS_DENIED;
    for (i = 176 + length; i < 240; ++i) if (p[i]) return PXA_STATUS_DENIED;
    record.generation = pxa_read_u64(p + 8);
    record.flags = pxa_read_u32(p + 16);
    memcpy(record.install_id, p + 24, 16);
    memcpy(record.publisher_root, p + 40, 32);
    memcpy(record.app_volume.bytes, p + 72, 16);
    memcpy(record.media_volume.bytes, p + 88, 16);
    memcpy(record.manifest_sha256, p + 104, 32);
    memcpy(record.media_set_sha256, p + 136, 32);
    record.release_sequence = pxa_read_u64(p + 168);
    memcpy(record.app_id, p + 176, length);
    if(bytes.size==PXA_INSTALL_RECORD_BYTES)record.data_generation=pxa_read_u64(p+240);
    if (!valid(&record, &actual) || actual != length ||
        memcmp(&record.app_volume, expected, sizeof(*expected)))
        return PXA_STATUS_DENIED;
    *output = record;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_install_record_select(pxa_bytes_t a, pxa_bytes_t b,
    const pxa_volume_id_t *expected, pxa_install_record_t *output, uint8_t *slot) {
    pxa_install_record_t first, second;
    int have_first, have_second;
    if (!output || !slot || !expected) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    *slot = 0;
    if (!a.size && !b.size) return PXA_STATUS_NOT_FOUND;
    have_first = pxa_install_record_decode(a, expected, &first) == PXA_STATUS_OK;
    have_second = pxa_install_record_decode(b, expected, &second) == PXA_STATUS_OK;
    if (!have_first && !have_second) return PXA_STATUS_DENIED;
    if (have_first && have_second) {
        if (memcmp(first.install_id, second.install_id, sizeof(first.install_id)) ||
            memcmp(first.publisher_root, second.publisher_root, sizeof(first.publisher_root)) ||
            strcmp(first.app_id, second.app_id) ||
            (first.generation == second.generation && (a.size!=b.size || memcmp(a.data, b.data, a.size))))
            return PXA_STATUS_DENIED;
    }
    if (have_second && (!have_first || second.generation > first.generation)) {
        *output = second;
        *slot = 1;
    } else *output = first;
    return PXA_STATUS_OK;
}

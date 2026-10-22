#include "pxa/volume_identity.h"
#include "pxa/container.h"
#include <string.h>

pxa_status_t pxa_volume_identity_encode(const pxa_volume_id_t *id,
    uint8_t output[PXA_VOLUME_IDENTITY_BYTES]) {
    uint8_t any = 0;
    unsigned i;
    if (!output || !id) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, PXA_VOLUME_IDENTITY_BYTES);
    for (i = 0; i < sizeof(id->bytes); ++i) any |= id->bytes[i];
    if (!any) return PXA_STATUS_INVALID_ARGUMENT;
    memcpy(output, "PXVL", 4);
    pxa_write_u16(output + 4, 1);
    pxa_write_u16(output + 6, PXA_VOLUME_IDENTITY_BYTES);
    memcpy(output + 8, id->bytes, sizeof(id->bytes));
    pxa_write_u32(output + 28, pxa_container_crc32(output, 28));
    return PXA_STATUS_OK;
}

pxa_status_t pxa_volume_identity_decode(pxa_bytes_t bytes, pxa_volume_id_t *output) {
    uint8_t canonical[PXA_VOLUME_IDENTITY_BYTES];
    pxa_volume_id_t id;
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (!bytes.data || bytes.size != PXA_VOLUME_IDENTITY_BYTES ||
        memcmp(bytes.data, "PXVL", 4) || pxa_read_u16(bytes.data + 4) != 1 ||
        pxa_read_u16(bytes.data + 6) != PXA_VOLUME_IDENTITY_BYTES ||
        pxa_read_u32(bytes.data + 24) ||
        pxa_read_u32(bytes.data + 28) != pxa_container_crc32(bytes.data, 28))
        return PXA_STATUS_DENIED;
    memcpy(id.bytes, bytes.data + 8, sizeof(id.bytes));
    if (pxa_volume_identity_encode(&id, canonical) != PXA_STATUS_OK)
        return PXA_STATUS_DENIED;
    *output = id;
    return PXA_STATUS_OK;
}

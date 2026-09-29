#ifndef PXA_ASSET_OBJECT_INTERNAL_H
#define PXA_ASSET_OBJECT_INTERNAL_H
#include "pxa/asset_object.h"
struct pxa_asset_object {
    uint32_t references, payload_bytes;
    pxa_asset_object_free_fn deallocate;
    void *context;
    uint16_t width, height;
    uint8_t kind; /* PXA_ASSET_*, not a raster upload opcode. */
    uint8_t encoding;
};
#endif

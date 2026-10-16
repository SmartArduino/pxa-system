#ifndef PXA_IMAGE_JPEG_H
#define PXA_IMAGE_JPEG_H
#include "pxa/image_decode.h"

/* Small, forward-only metadata validator, shared by contiguous and block
 * input. The platform still owns entropy decoding. No segment-sized scratch.
 * Only bounded scanline/MCU adapters may accept the larger source extent;
 * an adapter allocating full-size pixels must impose its own smaller limit. */
typedef struct {
    void *context;
    size_t (*read)(void *, uint8_t *, size_t);
    size_t remaining;
} pxa_image_jpeg_reader_t;
static inline int pxa_image_jpeg_read(pxa_image_jpeg_reader_t *r, uint8_t *p, size_t n) {
    if (n > r->remaining || r->read(r->context, p, n) != n) return 0;
    r->remaining -= n;
    return 1;
}
static inline pxa_status_t pxa_image_jpeg_check(pxa_image_jpeg_reader_t *r,
    uint16_t *width, uint16_t *height) {
    *width = *height = 0;
    uint8_t p[17];
    if (!pxa_image_jpeg_read(r, p, 2) || p[0] != 255 || p[1] != 216)
        return PXA_STATUS_PROTOCOL_ERROR;
    while (r->remaining) {
        if (!pxa_image_jpeg_read(r, p, 1) || p[0] != 255) return PXA_STATUS_PROTOCOL_ERROR;
        do {
            if (!pxa_image_jpeg_read(r, p, 1)) return PXA_STATUS_PROTOCOL_ERROR;
        } while (p[0] == 255);
        uint8_t marker = p[0];
        if (!marker || marker == 216 || marker == 217 || (marker >= 208 && marker <= 215))
            return PXA_STATUS_PROTOCOL_ERROR;
        if (!pxa_image_jpeg_read(r, p, 2)) return PXA_STATUS_PROTOCOL_ERROR;
        size_t length = (size_t)p[0] * 256 + p[1];
        if (length < 2 || length - 2 > r->remaining) return PXA_STATUS_PROTOCOL_ERROR;
        size_t n = length - 2;
        if (marker == 192) {
            if (*width || n < 6 || !pxa_image_jpeg_read(r, p, 6) || p[0] != 8)
                return PXA_STATUS_PROTOCOL_ERROR;
            *height = (uint16_t)(p[1] * 256u + p[2]);
            *width = (uint16_t)(p[3] * 256u + p[4]);
            if (!*width || !*height || *width > 4096 || *height > 4096)
                return PXA_STATUS_LIMIT_EXCEEDED;
            unsigned components = p[5];
            if ((components != 1 && components != 3) || n != 6u + 3u * components)
                return PXA_STATUS_UNSUPPORTED;
            for (unsigned i = 0; i < components; ++i) {
                if (!pxa_image_jpeg_read(r, p, 3)) return PXA_STATUS_PROTOCOL_ERROR;
                uint8_t sampling = p[1];
                if (!(sampling >> 4) || !(sampling & 15) || (sampling >> 4) > 2 ||
                    (sampling & 15) > 2 || p[2] > 3) return PXA_STATUS_UNSUPPORTED;
            }
        } else if (marker >= 193 && marker <= 207 && marker != 196 && marker != 200 && marker != 204)
            return PXA_STATUS_UNSUPPORTED;
        else if (marker == 196) {
            while (n) {
                if (n < 17 || !pxa_image_jpeg_read(r, p, 17) || (p[0] & 0xee))
                    return PXA_STATUS_PROTOCOL_ERROR;
                int ac = p[0] >> 4, available = 1;
                unsigned count = 0;
                for (unsigned k = 1; k <= 16; ++k) {
                    available = available * 2 - p[k];
                    if (available < 0) return PXA_STATUS_PROTOCOL_ERROR;
                    count += p[k];
                }
                n -= 17;
                if (count > 256 || count > n) return PXA_STATUS_PROTOCOL_ERROR;
                for (unsigned k = 0; k < count; ++k) {
                    if (!pxa_image_jpeg_read(r, p, 1)) return PXA_STATUS_PROTOCOL_ERROR;
                    uint8_t value = p[0];
                    if ((!ac && value > 11) ||
                        (ac && ((value & 15) > 10 || (!(value & 15) && value != 0 && value != 240))))
                        return PXA_STATUS_PROTOCOL_ERROR;
                }
                n -= count;
            }
        } else if (marker == 218) {
            if (!*width || n < 4 || n > 10 || !pxa_image_jpeg_read(r, p, n))
                return PXA_STATUS_PROTOCOL_ERROR;
            if (n != 1u + 2u * p[0] + 3u || (p[0] != 1 && p[0] != 3) ||
                p[n - 3] != 0 || p[n - 2] != 63 || p[n - 1] != 0)
                return PXA_STATUS_UNSUPPORTED;
            for (unsigned i = 0; i < p[0]; ++i)
                if (p[2 + 2 * i] & 0xee) return PXA_STATUS_UNSUPPORTED;
            return PXA_STATUS_OK;
        } else if (!pxa_image_jpeg_read(r, NULL, n)) return PXA_STATUS_PROTOCOL_ERROR;
    }
    return PXA_STATUS_PROTOCOL_ERROR;
}
#endif

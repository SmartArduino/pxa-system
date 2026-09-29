#ifndef PXA_GUEST_DEVICE_FORMAT_H
#define PXA_GUEST_DEVICE_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#define PXA_DEVICE_FORMAT_WASM 1u
#define PXA_DEVICE_FORMAT_AOT 2u

static inline int pxa_device_format_mac_colon(const uint8_t mac[6], char *output,
                                               size_t output_capacity) {
    static const char hex[] = "0123456789ABCDEF";
    if (mac == NULL || output == NULL || output_capacity < 18u) return 0;
    for (size_t index = 0; index < 6u; ++index) {
        output[index * 3u] = hex[mac[index] >> 4];
        output[index * 3u + 1u] = hex[mac[index] & 15u];
        if (index != 5u) output[index * 3u + 2u] = ':';
    }
    output[17] = '\0';
    return 1;
}

#endif

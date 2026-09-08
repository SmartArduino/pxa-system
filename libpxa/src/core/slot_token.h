#ifndef PXA_INTERNAL_SLOT_TOKEN_H
#define PXA_INTERNAL_SLOT_TOKEN_H

#include <stdint.h>

static int pxa_internal_slot_token_decode(uint32_t token, uint16_t capacity,
                                          uint32_t *index,
                                          uint16_t *generation) {
    uint16_t encoded_index = (uint16_t)token;
    uint16_t encoded_generation = (uint16_t)(token >> 16);
    if (index == NULL || generation == NULL || encoded_index == 0 ||
        encoded_generation == 0 || (uint32_t)encoded_index > capacity) {
        return 0;
    }
    *index = (uint32_t)encoded_index - 1u;
    *generation = encoded_generation;
    return 1;
}

static uint32_t pxa_internal_slot_token_encode(uint32_t index,
                                               uint16_t generation) {
    if (index >= UINT16_MAX || generation == 0) return 0;
    return ((uint32_t)generation << 16) | (index + 1u);
}

#endif

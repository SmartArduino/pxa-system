/* Generated from spec/draft/pxa-window.json. Do not edit by hand. */
#ifndef PXA_GUEST_WINDOW_SNAPSHOT_GENERATED_H
#define PXA_GUEST_WINDOW_SNAPSHOT_GENERATED_H

#include <stddef.h>
#include <stdint.h>

#ifndef PXA_GUEST_WIRE_H
#error "include the generated PXA wire codec first"
#endif

#define PXA_WINDOW_SNAPSHOT_RECORD_BYTES ((size_t)98)

typedef struct {
    uint32_t left, top, right, bottom;
} pxa_window_wire_insets_t;

typedef struct {
    int32_t status;
    uint64_t revision;
    uint32_t logical_width, logical_height;
    uint32_t pixel_width, pixel_height;
    uint32_t density_numerator, density_denominator;
    pxa_window_wire_insets_t safe_insets;
    pxa_window_wire_insets_t system_bar_insets;
    uint8_t orientation, focused;
} pxa_window_snapshot_wire_t;

static inline int pxa_window_snapshot_wire_valid(
    const pxa_window_snapshot_wire_t *value) {
    return value != NULL && value->revision != 0 &&
           value->logical_width != 0 && value->logical_height != 0 &&
           value->pixel_width != 0 && value->pixel_height != 0 &&
           value->density_numerator != 0 &&
           value->density_denominator != 0 &&
           value->orientation <= 2 && value->focused <= 1;
}

static inline int pxa_window_snapshot_records_encode(
    uint8_t *out, size_t capacity,
    const pxa_window_snapshot_wire_t *value, size_t *written) {
    if (written != NULL) *written = 0;
    if (out == NULL || written == NULL ||
        capacity < PXA_WINDOW_SNAPSHOT_RECORD_BYTES ||
        !pxa_window_snapshot_wire_valid(value)) return 0;
    pxa_wire_generated_store_u16(out + 0, 1);
    pxa_wire_generated_store_u16(out + 2, 8);
    pxa_wire_generated_store_u64(out + 4, value->revision);
    pxa_wire_generated_store_u16(out + 12, 2);
    pxa_wire_generated_store_u16(out + 14, 8);
    pxa_wire_generated_store_u32(out + 16, value->logical_width);
    pxa_wire_generated_store_u32(out + 20, value->logical_height);
    pxa_wire_generated_store_u16(out + 24, 3);
    pxa_wire_generated_store_u16(out + 26, 8);
    pxa_wire_generated_store_u32(out + 28, value->pixel_width);
    pxa_wire_generated_store_u32(out + 32, value->pixel_height);
    pxa_wire_generated_store_u16(out + 36, 4);
    pxa_wire_generated_store_u16(out + 38, 8);
    pxa_wire_generated_store_u32(out + 40, value->density_numerator);
    pxa_wire_generated_store_u32(out + 44, value->density_denominator);
    pxa_wire_generated_store_u16(out + 48, 5);
    pxa_wire_generated_store_u16(out + 50, 16);
    pxa_wire_generated_store_u32(out + 52, value->safe_insets.left);
    pxa_wire_generated_store_u32(out + 56, value->safe_insets.top);
    pxa_wire_generated_store_u32(out + 60, value->safe_insets.right);
    pxa_wire_generated_store_u32(out + 64, value->safe_insets.bottom);
    pxa_wire_generated_store_u16(out + 68, 6);
    pxa_wire_generated_store_u16(out + 70, 16);
    pxa_wire_generated_store_u32(out + 72, value->system_bar_insets.left);
    pxa_wire_generated_store_u32(out + 76, value->system_bar_insets.top);
    pxa_wire_generated_store_u32(out + 80, value->system_bar_insets.right);
    pxa_wire_generated_store_u32(out + 84, value->system_bar_insets.bottom);
    pxa_wire_generated_store_u16(out + 88, 7);
    pxa_wire_generated_store_u16(out + 90, 1);
    out[92] = value->orientation;
    pxa_wire_generated_store_u16(out + 93, 8);
    pxa_wire_generated_store_u16(out + 95, 1);
    out[97] = value->focused;
    *written = PXA_WINDOW_SNAPSHOT_RECORD_BYTES;
    return 1;
}

static inline int pxa_window_snapshot_records_decode(
    const uint8_t *data, size_t size,
    pxa_window_snapshot_wire_t *output) {
    if (output == NULL) return 0;
    for (size_t i = 0; i < sizeof(*output); ++i)
        ((uint8_t *)output)[i] = 0;
    if (data == NULL || size != PXA_WINDOW_SNAPSHOT_RECORD_BYTES)
        return 0;
    if (pxa_wire_generated_load_u16(data + 0) != 1 ||
        pxa_wire_generated_load_u16(data + 2) != 8) return 0;
    if (pxa_wire_generated_load_u16(data + 12) != 2 ||
        pxa_wire_generated_load_u16(data + 14) != 8) return 0;
    if (pxa_wire_generated_load_u16(data + 24) != 3 ||
        pxa_wire_generated_load_u16(data + 26) != 8) return 0;
    if (pxa_wire_generated_load_u16(data + 36) != 4 ||
        pxa_wire_generated_load_u16(data + 38) != 8) return 0;
    if (pxa_wire_generated_load_u16(data + 48) != 5 ||
        pxa_wire_generated_load_u16(data + 50) != 16) return 0;
    if (pxa_wire_generated_load_u16(data + 68) != 6 ||
        pxa_wire_generated_load_u16(data + 70) != 16) return 0;
    if (pxa_wire_generated_load_u16(data + 88) != 7 ||
        pxa_wire_generated_load_u16(data + 90) != 1) return 0;
    if (pxa_wire_generated_load_u16(data + 93) != 8 ||
        pxa_wire_generated_load_u16(data + 95) != 1) return 0;
    output->revision = pxa_wire_generated_load_u64(data + 4);
    output->logical_width = pxa_wire_generated_load_u32(data + 16);
    output->logical_height = pxa_wire_generated_load_u32(data + 20);
    output->pixel_width = pxa_wire_generated_load_u32(data + 28);
    output->pixel_height = pxa_wire_generated_load_u32(data + 32);
    output->density_numerator = pxa_wire_generated_load_u32(data + 40);
    output->density_denominator = pxa_wire_generated_load_u32(data + 44);
    output->safe_insets.left = pxa_wire_generated_load_u32(data + 52);
    output->safe_insets.top = pxa_wire_generated_load_u32(data + 56);
    output->safe_insets.right = pxa_wire_generated_load_u32(data + 60);
    output->safe_insets.bottom = pxa_wire_generated_load_u32(data + 64);
    output->system_bar_insets.left = pxa_wire_generated_load_u32(data + 72);
    output->system_bar_insets.top = pxa_wire_generated_load_u32(data + 76);
    output->system_bar_insets.right = pxa_wire_generated_load_u32(data + 80);
    output->system_bar_insets.bottom = pxa_wire_generated_load_u32(data + 84);
    output->orientation = data[92];
    output->focused = data[97];
    return pxa_window_snapshot_wire_valid(output);
}

static inline int pxa_window_snapshot_result_decode(
    const uint8_t *data, size_t size,
    pxa_window_snapshot_wire_t *output) {
    if (output == NULL) return 0;
    for (size_t i = 0; i < sizeof(*output); ++i)
        ((uint8_t *)output)[i] = 0;
    if (data == NULL || size < 4) return 0;
    output->status = (int32_t)pxa_wire_generated_load_u32(data);
    if (output->status != 0) return size == 4;
    return pxa_window_snapshot_records_decode(data + 4, size - 4,
                                              output);
}

#endif

#ifndef PXA_I18N_H
#define PXA_I18N_H

#include <stddef.h>
#include <stdint.h>

#include "pxa_system.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t pxa_i18n_message_id_t;

typedef struct {
    pxa_i18n_message_id_t id;
    const char *value;
    uint16_t value_size;
} pxa_i18n_entry_t;

typedef struct {
    const char *locale;
    uint8_t locale_size;
    const pxa_i18n_entry_t *entries;
    uint16_t entry_count;
} pxa_i18n_catalog_t;

typedef struct {
    const pxa_i18n_catalog_t *catalogs;
    uint8_t catalog_count;
    uint8_t default_catalog;
} pxa_i18n_bundle_t;

typedef struct {
    const char *data;
    size_t size;
} pxa_i18n_string_t;

typedef struct {
    const pxa_i18n_bundle_t *bundle;
    char locale[PXA_SYSTEM_LOCALE_MAX_BYTES + 1u];
    uint8_t locale_size;
    uint8_t text_direction;
} pxa_i18n_t;

typedef enum {
    PXA_I18N_ARGUMENT_STRING = 1,
    PXA_I18N_ARGUMENT_U32 = 2,
    PXA_I18N_ARGUMENT_I32 = 3,
} pxa_i18n_argument_type_t;

typedef struct {
    const char *name;
    uint8_t name_size;
    uint8_t type;
    union {
        pxa_i18n_string_t string;
        uint32_t u32;
        int32_t i32;
    } value;
} pxa_i18n_argument_t;

static inline int pxa_i18n_locale_match(const char *requested,
                                        size_t requested_size,
                                        const char *available,
                                        size_t available_size) {
    size_t index;
    if (available_size > requested_size) return 0;
    for (index = 0; index < available_size; ++index)
        if (requested[index] != available[index]) return 0;
    return available_size == requested_size ||
           requested[available_size] == '-';
}

static inline const pxa_i18n_entry_t *pxa_i18n_find(
    const pxa_i18n_catalog_t *catalog, pxa_i18n_message_id_t id) {
    size_t low = 0;
    size_t high = catalog != NULL ? catalog->entry_count : 0;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        pxa_i18n_message_id_t candidate = catalog->entries[middle].id;
        if (candidate < id)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < (catalog != NULL ? catalog->entry_count : 0) &&
                   catalog->entries[low].id == id
               ? &catalog->entries[low]
               : NULL;
}

static inline void pxa_i18n_init(pxa_i18n_t *i18n,
                                 const pxa_i18n_bundle_t *bundle) {
    const pxa_i18n_catalog_t *fallback = NULL;
    size_t index;
    if (i18n == NULL) return;
    i18n->bundle = bundle;
    i18n->locale_size = 0;
    i18n->locale[0] = '\0';
    i18n->text_direction = PXA_SYSTEM_TEXT_DIRECTION_LTR;
    if (bundle != NULL && bundle->catalogs != NULL &&
        bundle->default_catalog < bundle->catalog_count)
        fallback = &bundle->catalogs[bundle->default_catalog];
    if (fallback == NULL) return;
    for (index = 0; index < fallback->locale_size &&
                    index < PXA_SYSTEM_LOCALE_MAX_BYTES; ++index)
        i18n->locale[index] = fallback->locale[index];
    i18n->locale[index] = '\0';
    i18n->locale_size = (uint8_t)index;
}

static inline int pxa_i18n_apply_configuration(
    pxa_i18n_t *i18n, const pxa_system_configuration_event_t *configuration) {
    size_t index;
    int changed;
    if (i18n == NULL || configuration == NULL ||
        configuration->locale == NULL ||
        configuration->locale_size < 2u ||
        configuration->locale_size > PXA_SYSTEM_LOCALE_MAX_BYTES ||
        configuration->text_direction > PXA_SYSTEM_TEXT_DIRECTION_RTL) {
        return 0;
    }
    changed = i18n->locale_size != configuration->locale_size ||
              i18n->text_direction != configuration->text_direction;
    for (index = 0; index < configuration->locale_size; ++index) {
        if (i18n->locale[index] != (char)configuration->locale[index])
            changed = 1;
        i18n->locale[index] = (char)configuration->locale[index];
    }
    i18n->locale[index] = '\0';
    i18n->locale_size = (uint8_t)configuration->locale_size;
    i18n->text_direction = configuration->text_direction;
    return changed ? 1 : 2;
}

static inline int pxa_i18n_init_from_start_config(
    pxa_i18n_t *i18n, const pxa_i18n_bundle_t *bundle,
    const uint8_t *config, size_t config_size) {
    pxa_system_configuration_event_t configuration;
    pxa_i18n_init(i18n, bundle);
    if (!pxa_system_parse_start_configuration(config, config_size,
                                              &configuration)) {
        return 0;
    }
    return pxa_i18n_apply_configuration(i18n, &configuration);
}

static inline int pxa_i18n_handle_event(pxa_i18n_t *i18n,
                                        const pxa_event_t *event) {
    pxa_system_configuration_event_t configuration;
    if (i18n == NULL ||
        !pxa_system_parse_configuration_event(event, &configuration))
        return 0;
    return pxa_i18n_apply_configuration(i18n, &configuration);
}

static inline pxa_i18n_string_t pxa_i18n_get(
    const pxa_i18n_t *i18n, pxa_i18n_message_id_t id) {
    pxa_i18n_string_t result = {NULL, 0};
    const pxa_i18n_entry_t *entry = NULL;
    int best_score = -1;
    size_t index;
    if (i18n == NULL || i18n->bundle == NULL ||
        i18n->bundle->catalogs == NULL)
        return result;
    for (index = 0; index < i18n->bundle->catalog_count; ++index) {
        const pxa_i18n_catalog_t *catalog = &i18n->bundle->catalogs[index];
        int score;
        if (!pxa_i18n_locale_match(i18n->locale, i18n->locale_size,
                                   catalog->locale, catalog->locale_size))
            continue;
        score = (int)catalog->locale_size;
        if (score <= best_score) continue;
        entry = pxa_i18n_find(catalog, id);
        if (entry != NULL) best_score = score;
    }
    if (entry == NULL &&
        i18n->bundle->default_catalog < i18n->bundle->catalog_count)
        entry = pxa_i18n_find(
            &i18n->bundle->catalogs[i18n->bundle->default_catalog], id);
    if (entry != NULL) {
        result.data = entry->value;
        result.size = entry->value_size;
    }
    return result;
}

static inline const char *pxa_i18n_cstr(const pxa_i18n_t *i18n,
                                        pxa_i18n_message_id_t id) {
    pxa_i18n_string_t value = pxa_i18n_get(i18n, id);
    return value.data != NULL ? value.data : "";
}

static inline size_t pxa_i18n_size(const pxa_i18n_t *i18n,
                                   pxa_i18n_message_id_t id) {
    return pxa_i18n_get(i18n, id).size;
}

static inline size_t pxa_i18n_write_u32(char *output, size_t capacity,
                                        size_t offset, uint32_t value) {
    char reverse[10];
    size_t count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0 && count < sizeof(reverse));
    while (count != 0 && offset + 1u < capacity)
        output[offset++] = reverse[--count];
    return offset;
}

static inline size_t pxa_i18n_format(const pxa_i18n_t *i18n,
                                     pxa_i18n_message_id_t id,
                                     const pxa_i18n_argument_t *arguments,
                                     size_t argument_count, char *output,
                                     size_t capacity) {
    pxa_i18n_string_t message = pxa_i18n_get(i18n, id);
    size_t input = 0;
    size_t written = 0;
    if (output == NULL || capacity == 0) return 0;
    while (input < message.size && written + 1u < capacity) {
        size_t end;
        size_t argument_index;
        if (message.data[input] != '{') {
            output[written++] = message.data[input++];
            continue;
        }
        end = input + 1u;
        while (end < message.size && message.data[end] != '}') ++end;
        if (end == message.size) {
            output[written++] = message.data[input++];
            continue;
        }
        for (argument_index = 0; argument_index < argument_count;
             ++argument_index) {
            const pxa_i18n_argument_t *argument = &arguments[argument_index];
            size_t name_size = end - input - 1u;
            size_t name_index;
            if (argument->name_size != name_size) continue;
            for (name_index = 0; name_index < name_size; ++name_index)
                if (argument->name[name_index] !=
                    message.data[input + 1u + name_index]) break;
            if (name_index == name_size) break;
        }
        if (argument_index == argument_count) {
            while (input <= end && written + 1u < capacity)
                output[written++] = message.data[input++];
            continue;
        }
        {
            const pxa_i18n_argument_t *argument = &arguments[argument_index];
            if (argument->type == PXA_I18N_ARGUMENT_STRING) {
                size_t value_index;
                for (value_index = 0;
                     value_index < argument->value.string.size &&
                     written + 1u < capacity; ++value_index)
                    output[written++] = argument->value.string.data[value_index];
            } else if (argument->type == PXA_I18N_ARGUMENT_U32) {
                written = pxa_i18n_write_u32(
                    output, capacity, written, argument->value.u32);
            } else if (argument->type == PXA_I18N_ARGUMENT_I32) {
                int32_t value = argument->value.i32;
                uint32_t magnitude;
                if (value < 0 && written + 1u < capacity) output[written++] = '-';
                magnitude = value < 0 ? (uint32_t)(-(value + 1)) + 1u
                                      : (uint32_t)value;
                written = pxa_i18n_write_u32(output, capacity, written,
                                             magnitude);
            }
        }
        input = end + 1u;
    }
    output[written] = '\0';
    return written;
}

#ifdef __cplusplus
}
#endif

#endif

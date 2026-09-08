#ifndef PXA_PERMISSION_INTERNAL_H
#define PXA_PERMISSION_INTERNAL_H

#include "pxa/permission.h"

#include <stdint.h>
#include <string.h>

#define PXA_PERMISSION_MAGIC UINT32_C(0x5058504d)
#define PXA_PERMISSION_MAX_NAME ((size_t)96)
#define PXA_PERMISSION_MAX_SCOPE ((size_t)1024)
#define PXA_PERMISSION_SLOT_NONE UINT16_MAX

typedef struct {
    pxa_bytes_t name;
    pxa_bytes_t scope;
    pxa_permission_decision_t decision;
    uint8_t required;
} pxa_permission_entry_t;

typedef struct {
    struct pxa_permission_service *service;
    pxa_component_t component;
    pxa_authority_t authority;
    pxa_handle_t permission_handle;
    union {
        uint16_t declaration_index;
        uint16_t next_free;
    } slot;
    uint8_t active;
} pxa_permission_authority_t;

typedef struct {
    pxa_component_t component;
    uint32_t request_id;
    union {
        uint16_t declaration_index;
        uint16_t next_free;
    } slot;
    uint8_t active;
} pxa_permission_pending_prompt_t;

struct pxa_permission_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_permission_store_t store;
    pxa_bytes_t identity;
    pxa_permission_entry_t *declarations;
    pxa_permission_authority_t *authorities;
    pxa_permission_pending_prompt_t *pending_prompts;
    uint16_t declaration_count;
    uint16_t max_authorities;
    uint16_t max_pending_prompts;
    pxa_authority_t next_authority;
    void *prompt_context;
    pxa_permission_prompt_fn prompt;
    uint8_t initialized;
    uint8_t registered;
    uint16_t free_authority_head;
    uint16_t free_prompt_head;
};

static inline int pxa_permission_bytes_valid_internal(pxa_bytes_t bytes) {
    return bytes.data != NULL || bytes.size == 0;
}

static inline int pxa_permission_bytes_equal_internal(pxa_bytes_t left,
                                                       pxa_bytes_t right) {
    return left.size == right.size &&
           pxa_permission_bytes_valid_internal(left) &&
           pxa_permission_bytes_valid_internal(right) &&
           (left.size == 0 || memcmp(left.data, right.data, left.size) == 0);
}

static inline int pxa_permission_service_valid_internal(
    const pxa_permission_service_t *service) {
    return service != NULL && service->magic == PXA_PERMISSION_MAGIC;
}

static inline int pxa_permission_find_declaration_internal(
    const pxa_permission_service_t *service, pxa_bytes_t name,
    pxa_bytes_t scope, uint16_t *index_out) {
    uint16_t index;
    if (!pxa_permission_service_valid_internal(service) ||
        !pxa_permission_bytes_valid_internal(name) ||
        !pxa_permission_bytes_valid_internal(scope)) {
        return 0;
    }
    for (index = 0; index < service->declaration_count; ++index) {
        if (pxa_permission_bytes_equal_internal(
                service->declarations[index].name, name) &&
            pxa_permission_bytes_equal_internal(
                service->declarations[index].scope, scope)) {
            if (index_out != NULL) *index_out = index;
            return 1;
        }
    }
    return 0;
}

#endif

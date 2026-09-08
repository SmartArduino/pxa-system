#ifndef PXA_UI_BUILDER_H
#define PXA_UI_BUILDER_H

#include "pxa_ui.h"

/* A streaming tree builder. It retains only the current ancestor path; the
 * Host remains the sole owner of committed UI state. */
typedef struct {
    pxa_ui_transaction_t transaction;
    uint32_t* generation;
    uint32_t next_generation;
    uint32_t* ancestors;
    size_t ancestor_capacity;
    size_t depth;
    uint32_t current;
    uint8_t active;
    uint8_t failed;
} pxa_ui_builder_t;

static inline int pxa_ui_builder_begin(
    pxa_ui_builder_t* builder, uint32_t* generation, uint32_t surface,
    uint32_t target, uint8_t kind, uint8_t* scratch, size_t scratch_capacity,
    uint32_t* ancestors, size_t ancestor_capacity) {
    uint32_t next;
    if (builder == NULL || generation == NULL || builder->active ||
        (ancestors == NULL && ancestor_capacity != 0))
        return 0;
    next = *generation + 1u;
    if (next == 0 || !pxa_ui_transaction_begin_target(
                         &builder->transaction, next, next, surface, target,
                         kind, scratch, scratch_capacity))
        return 0;
    builder->generation = generation;
    builder->next_generation = next;
    builder->ancestors = ancestors;
    builder->ancestor_capacity = ancestor_capacity;
    builder->depth = 0;
    builder->current = 0;
    builder->active = 1;
    builder->failed = 0;
    return 1;
}

static inline uint32_t pxa_ui_builder_parent(
    const pxa_ui_builder_t* builder) {
    return builder == NULL || builder->depth == 0
               ? 0
               : builder->ancestors[builder->depth - 1u];
}

static inline int pxa_ui_builder_node_typed(
    pxa_ui_builder_t* builder, uint32_t node, uint32_t before, uint8_t type,
    uint8_t subtype) {
    if (builder == NULL || !builder->active || builder->failed || node == 0 ||
        !pxa_ui_create_typed(&builder->transaction, node,
                             pxa_ui_builder_parent(builder), before, type,
                             subtype)) {
        if (builder != NULL) builder->failed = 1;
        return 0;
    }
    builder->current = node;
    return 1;
}

static inline int pxa_ui_builder_node(pxa_ui_builder_t* builder,
                                      uint32_t node, uint32_t before,
                                      uint8_t type) {
    return pxa_ui_builder_node_typed(builder, node, before, type,
                                     PXA_UI_CONTROL_NONE);
}

static inline int pxa_ui_builder_enter_typed(
    pxa_ui_builder_t* builder, uint32_t node, uint32_t before, uint8_t type,
    uint8_t subtype) {
    if (builder == NULL || builder->depth >= builder->ancestor_capacity ||
        !pxa_ui_builder_node_typed(builder, node, before, type, subtype)) {
        if (builder != NULL) builder->failed = 1;
        return 0;
    }
    builder->ancestors[builder->depth++] = node;
    return 1;
}

static inline int pxa_ui_builder_enter(pxa_ui_builder_t* builder,
                                       uint32_t node, uint32_t before,
                                       uint8_t type) {
    return pxa_ui_builder_enter_typed(builder, node, before, type,
                                      PXA_UI_CONTROL_NONE);
}

static inline int pxa_ui_builder_leave(pxa_ui_builder_t* builder) {
    if (builder == NULL || !builder->active || builder->depth == 0) return 0;
    --builder->depth;
    builder->current = pxa_ui_builder_parent(builder);
    return 1;
}

static inline pxa_ui_transaction_t* pxa_ui_builder_transaction(
    pxa_ui_builder_t* builder) {
    return builder == NULL || !builder->active ? NULL : &builder->transaction;
}

static inline uint32_t pxa_ui_builder_current(
    const pxa_ui_builder_t* builder) {
    return builder == NULL ? 0 : builder->current;
}

static inline int pxa_ui_builder_abort(pxa_ui_builder_t* builder) {
    int result;
    if (builder == NULL || !builder->active) return 0;
    result = pxa_ui_transaction_cancel(&builder->transaction);
    builder->active = 0;
    builder->failed = 1;
    return result;
}

static inline int pxa_ui_builder_end(pxa_ui_builder_t* builder) {
    int result;
    if (builder == NULL || !builder->active || builder->failed) {
        if (builder != NULL && builder->active)
            (void)pxa_ui_builder_abort(builder);
        return 0;
    }
    result = pxa_ui_transaction_commit(&builder->transaction);
    builder->active = 0;
    builder->failed = (uint8_t)!result;
    if (!result) return 0;
    *builder->generation = builder->next_generation;
    return 1;
}

typedef struct {
    uint32_t* routes;
    size_t capacity;
    size_t count;
} pxa_ui_nav_stack_t;

static inline int pxa_ui_nav_init(pxa_ui_nav_stack_t* stack,
                                  uint32_t* routes, size_t capacity,
                                  uint32_t root) {
    if (stack == NULL || routes == NULL || capacity == 0 || root == 0)
        return 0;
    stack->routes = routes;
    stack->capacity = capacity;
    stack->count = 1;
    routes[0] = root;
    return 1;
}

static inline uint32_t pxa_ui_nav_current(const pxa_ui_nav_stack_t* stack) {
    return stack == NULL || stack->count == 0
               ? 0
               : stack->routes[stack->count - 1u];
}

static inline int pxa_ui_nav_push(pxa_ui_nav_stack_t* stack, uint32_t route) {
    if (stack == NULL || route == 0 || stack->count >= stack->capacity)
        return 0;
    stack->routes[stack->count++] = route;
    return 1;
}

static inline int pxa_ui_nav_pop(pxa_ui_nav_stack_t* stack) {
    if (stack == NULL || stack->count <= 1) return 0;
    --stack->count;
    return 1;
}

#endif

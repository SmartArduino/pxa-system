#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxa/event_port.h"
#include "pxa/package.h"
#include "pxa/service.h"
#include "pxa/wamr/pxa_wamr_engine.h"

static const char *guest_name = "event_port_guest.wasm";
typedef struct {
    pxa_runtime_t *runtime;
    pxa_wamr_engine_t *engine;
    pxa_component_t component;
    uint64_t now;
    unsigned phase, checkpoints, reentries_rejected, done, unexpected, overdue_decisions;
} context_t;

static uint64_t now_us(void *opaque) { return ((context_t *)opaque)->now; }

static pxa_status_t cooperate(void *opaque, pxa_component_t component, uint64_t until) {
    context_t *ctx = opaque;
    pxa_wamr_event_result_t nested;
    assert(component == ctx->component);
    ++ctx->checkpoints;
    /* The active Guest call retains exclusive ownership, even in a Host pump. */
    assert(pxa_wamr_engine_deliver_event_result(ctx->engine, ctx->runtime, component, &nested)
        == PXA_STATUS_WOULD_BLOCK && !nested.event_consumed);
    ++ctx->reentries_rejected;
    if (ctx->phase == 3) return PXA_STATUS_CANCELLED;
    if (ctx->phase == 5) {
        assert(pxa_wamr_engine_terminate_active_call(ctx->engine));
        return PXA_STATUS_OK;
    }
    if (ctx->phase == 4) {
        assert(pxa_wamr_engine_take_expired_deadline(ctx->engine, ctx->now));
        assert(pxa_wamr_engine_extend_active_deadline(ctx->engine, ctx->now));
        ++ctx->overdue_decisions; ctx->phase = 0;
    }
    if (ctx->phase == 1 || ctx->phase == 2) {
        uint8_t payload[64];
        for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)(i ^ 0xa5);
        assert(pxa_event_post_message(ctx->runtime, component, 0x7ffd, 1, 0,
            (pxa_bytes_t){payload, sizeof(payload)}, 1, 0) == PXA_STATUS_OK);
        ctx->phase = 0;
    }
    if (until) {
        ctx->now = until;
        assert(!pxa_wamr_engine_take_expired_deadline(ctx->engine, ctx->now));
        return PXA_STATUS_WOULD_BLOCK;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t control(void *opaque, pxa_runtime_t *runtime,
    pxa_component_t component, const pxa_message_view_t *message) {
    context_t *ctx = opaque;
    assert(runtime == ctx->runtime && component == ctx->component && !message->request_token);
    if (message->opcode >= 1000) fprintf(stderr, "Guest requirement failed at line %u\n", message->opcode - 1000);
    else if (message->opcode == 100) ++ctx->done;
    else if (message->opcode == 101) ++ctx->unexpected;
    else {
        ctx->phase = message->opcode;
        if (ctx->phase == 4) ctx->now += 2000;
    }
    return PXA_STATUS_OK;
}

static void run(unsigned mode, unsigned minor, unsigned flags, int has_backend,
                 pxa_status_t expected_instantiate) {
    pxa_runtime_limits_t limits;
    pxa_runtime_t *runtime;
    pxa_component_t component, other;
    pxa_handle64_t foreign;
    pxa_resource_t resource = {0};
    context_t context = {0};
    pxa_wamr_engine_t *engine;
    pxa_component_engine_t ops;
    pxa_runtime_limits_init(&limits);
    limits.max_components = 3;
    size_t size = pxa_runtime_workspace_size(&limits);
    void *memory = malloc(size);
    assert(memory && pxa_runtime_init(memory, size, &limits, &runtime) == PXA_STATUS_OK);
    assert(pxa_component_create(runtime, 1, &component) == PXA_STATUS_OK);
    assert(pxa_component_create(runtime, 2, &other) == PXA_STATUS_OK);
    assert(pxa_component_begin_start(runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_begin_start(runtime, other) == PXA_STATUS_OK);
    assert(pxa_component_finish_start(runtime, other, PXA_STATUS_OK) == PXA_STATUS_OK);
    assert(pxa_handle_open64(runtime, other, PXA_RESOURCE_EVENT_PORT, 0, &resource, &foreign) == PXA_STATUS_OK);
    context.runtime = runtime; context.component = component; context.now = 100;
    pxa_service_ops_t service = {0};
    service.struct_size = sizeof(service); service.service_id = 0x7ffe;
    service.context = &context; service.control = control;
    assert(pxa_service_register(runtime, &service) == PXA_STATUS_OK);
    pxa_wamr_engine_config_t config = {0};
    config.struct_size = sizeof(config); config.host_context = &context;
    config.open_artifact = pxa_wamr_open_artifact_file; config.now_us = now_us;
    config.cooperate = has_backend ? cooperate : NULL;
    config.call_timeout_us = 1000; config.guest_stack_size = 65536;
    config.max_components = 1; config.pool_bytes = 2 * 1024 * 1024;
    size_t engine_size = pxa_wamr_engine_workspace_size(&config);
    void *engine_memory = malloc(engine_size);
    assert(engine_memory && pxa_wamr_engine_init(engine_memory, engine_size, &config,
        &engine, &ops) == PXA_STATUS_OK);
    assert(pxa_wamr_engine_execution_models(engine) ==
        (has_backend ? PXA_EXECUTION_MODEL_MAIN_LOOP : 0));
    pxa_wamr_engine_set_runtime(engine, runtime); context.engine = engine;
    pxa_package_artifact_t artifact = {0};
    const char *name = guest_name;
    artifact.kind = strstr(name, ".aot") ? PXA_ARTIFACT_AOT : PXA_ARTIFACT_WASM;
    artifact.path = (pxa_bytes_t){(const uint8_t *)name, strlen(name)};
    pxa_package_component_t description = {0};
    description.id = (pxa_bytes_t){(const uint8_t *)"main", 4};
    description.kind = PXA_COMPONENT_KIND_UI; description.flags = (uint8_t)flags;
    description.artifacts = &artifact; description.artifact_count = 1;
    pxa_activation_entry_t entry = {0};
    entry.component = &description; entry.artifact = &artifact; entry.core_major = 2; entry.core_minor = (uint16_t)minor;
    const char *directory = PXA_EVENT_PORT_GUEST_DIR;
    pxa_status_t status = ops.instantiate(ops.context,
        (pxa_bytes_t){(const uint8_t *)directory, strlen(directory)}, &entry, component, 1);
    assert(status == expected_instantiate);
    if (status == PXA_STATUS_OK) {
        uint8_t data[9]; data[0] = (uint8_t)mode; pxa_write_u64(data + 1, foreign);
        assert(pxa_wamr_engine_set_config(engine, 1,
            (pxa_bytes_t){data, sizeof(data)}) == PXA_STATUS_OK);
        assert(ops.start(ops.context, component) == PXA_STATUS_OK);
        assert(pxa_component_finish_start(runtime, component, PXA_STATUS_OK) == PXA_STATUS_OK);
        if (mode != 3) {
            pxa_wamr_event_result_t result;
            status = pxa_wamr_engine_deliver_event_result(engine, runtime, component, &result);
            if (mode == 2) assert(status == PXA_STATUS_INTERNAL && !result.cooperative_cancelled);
            else {
                assert(status == PXA_STATUS_OK);
                assert(result.cooperative_cancelled == (mode == 1 || mode == 4));
                assert(context.checkpoints && context.checkpoints == context.reentries_rejected);
                if (!mode) {
                    assert(!pxa_wamr_engine_take_expired_deadline(engine, context.now));
                    assert(pxa_wamr_engine_deliver_event_result(engine, runtime, component, &result) == PXA_STATUS_OK);
                    assert(context.done == 1);
                    assert(context.overdue_decisions == 1);
                }
            }
            assert(!context.unexpected);
        }
        ops.destroy(ops.context, component); /* Closes a still-open cancelled port. */
    }
    assert(pxa_component_abort(runtime, component, PXA_STOP_NORMAL) == PXA_STATUS_OK);
    assert(pxa_component_abort(runtime, other, PXA_STOP_NORMAL) == PXA_STATUS_OK);
    pxa_runtime_usage_t usage;
    assert(pxa_runtime_usage_snapshot(runtime, &usage) == PXA_STATUS_OK);
    assert(!usage.current_requests && !usage.current_handles && !usage.current_events);
    pxa_wamr_engine_deinit(engine);
    free(engine_memory); pxa_runtime_deinit(runtime); free(memory);
}

int main(int argc, char **argv) {
    if (argc > 1) guest_name = argv[1];
    run(0, 1, PXA_PACKAGE_COMPONENT_FLAG_MAIN_LOOP, 1, PXA_STATUS_OK);
    run(1, 1, PXA_PACKAGE_COMPONENT_FLAG_MAIN_LOOP, 1, PXA_STATUS_OK);
    run(2, 1, PXA_PACKAGE_COMPONENT_FLAG_MAIN_LOOP, 1, PXA_STATUS_OK);
    run(4, 1, PXA_PACKAGE_COMPONENT_FLAG_MAIN_LOOP, 1, PXA_STATUS_OK);
    run(3, 0, 0, 1, PXA_STATUS_OK);
    run(3, 1, 0, 1, PXA_STATUS_OK);
    run(0, 0, PXA_PACKAGE_COMPONENT_FLAG_MAIN_LOOP, 1, PXA_STATUS_UNSUPPORTED);
    run(0, 1, PXA_PACKAGE_COMPONENT_FLAG_MAIN_LOOP, 0, PXA_STATUS_UNSUPPORTED);
    puts("PASS: event-port waits, timeout/deadline suspension, queue retention, memory.grow, owner/stale handles, cancellation, trap and opt-in gates");
    return 0;
}

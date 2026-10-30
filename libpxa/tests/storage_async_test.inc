/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
typedef struct {
    pxa_storage_work_fn work[8];
    void *contexts[8];
    unsigned count;
    pxa_status_t reject;
} test_storage_executor_t;

static pxa_status_t storage_submit(void *context, pxa_storage_work_fn work, void *job) {
    test_storage_executor_t *executor = context;
    if (executor->reject) return executor->reject;
    assert(executor->count < 8);
    executor->work[executor->count] = work;
    executor->contexts[executor->count++] = job;
    return PXA_STATUS_OK;
}

static void storage_execute_one(test_storage_executor_t *executor) {
    pxa_storage_work_fn work;
    void *context;
    unsigned i;
    assert(executor->count);
    work = executor->work[0]; context = executor->contexts[0];
    for (i = 1; i < executor->count; ++i) {
        executor->work[i - 1] = executor->work[i];
        executor->contexts[i - 1] = executor->contexts[i];
    }
    --executor->count;
    work(context);
}

static void storage_dispatch(test_runtime_t *test, pxa_component_t component,
    uint16_t opcode, pxa_request_token_t token, const char *key, const char *value) {
    uint8_t command[256];
    size_t size = make_storage_request(command, sizeof(command), opcode, token, key, value);
    assert(pxa_component_begin_event(test->runtime, component) == PXA_STATUS_OK);
    assert(pxa_runtime_control(test->runtime, component, command, size) == PXA_STATUS_OK);
    assert(pxa_component_finish_event(test->runtime, component, 1) == PXA_STATUS_OK);
    /* The original message is borrowed by control, and may be reused before
     * any queued disk call starts. The executor must own all key/value bytes. */
    memset(command, 0xa5, sizeof(command));
}

static void storage_completion(test_runtime_t *test, pxa_component_t component,
    pxa_request_token_t token, pxa_status_t result, const char *value) {
    uint8_t bytes[1200];
    pxa_event_view_t view;
    pxa_message_view_t message;
    size_t size = read_head_event(test->runtime, component, bytes, sizeof(bytes), &view);
    assert(pxa_message_decode(bytes, size, PXA_MAX_CONTROL_MESSAGE, &message) == PXA_STATUS_OK);
    assert(message.request_token == token && (int32_t)pxa_read_u32(message.payload.data) == result);
    if (value) {
        pxa_record_iterator_t iterator;
        pxa_record_view_t record;
        pxa_record_iterator_init(&iterator, (pxa_bytes_t){message.payload.data + 4, message.payload.size - 4});
        assert(pxa_record_next(&iterator, &record) == PXA_STATUS_OK);
        assert(record.tag == 2 && record.payload.size == strlen(value));
        assert(!memcmp(record.payload.data, value, strlen(value)));
    }
    assert(pxa_event_consume(test->runtime, component, view.token) == PXA_STATUS_OK);
}

static void test_storage_async(void) {
    test_runtime_t test = make_runtime(1, 8, 8, 2, 12, 8, 2);
    pxa_component_t component = create_started(test.runtime, 10);
    test_storage_backend_t backend;
    test_storage_executor_t executor;
    pxa_storage_config_t config;
    pxa_storage_service_t *storage = NULL;
    pxa_event_view_t view;
    void *workspace, *jobs;
    size_t size, jobs_size;
    memset(&backend, 0, sizeof(backend)); memset(&executor, 0, sizeof(executor));
    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config); config.max_value_bytes = TEST_STORAGE_VALUE_CAPACITY;
    config.backend.struct_size = sizeof(config.backend); config.backend.context = &backend;
    config.backend.get = storage_get; config.backend.set = storage_set;
    config.backend.remove = storage_remove; config.backend.list = storage_list;
    size = pxa_storage_service_workspace_size(&config); workspace = malloc(size);
    assert(pxa_storage_service_init(workspace, size, test.runtime, &config, &storage) == PXA_STATUS_OK);
    assert(pxa_storage_service_register(storage) == PXA_STATUS_OK);
    assert(!pxa_storage_service_async_workspace_size(0));
    assert(!pxa_storage_service_async_workspace_size(17));
    jobs_size = pxa_storage_service_async_workspace_size(2); jobs = malloc(jobs_size);
    assert(pxa_storage_service_enable_async(storage, jobs, jobs_size - 1, 2, storage_submit, &executor) == PXA_STATUS_INVALID_ARGUMENT);
    assert(pxa_storage_service_enable_async(storage, jobs, jobs_size, 2, storage_submit, &executor) == PXA_STATUS_OK);

    storage_dispatch(&test, component, PXA_STORAGE_SET, 101, "checkpoint", "star1");
    storage_dispatch(&test, component, PXA_STORAGE_GET, 102, "checkpoint", NULL);
    assert(backend.count == 0 && executor.count == 2);
    assert(pxa_event_peek(test.runtime, component, &view) == PXA_STATUS_WOULD_BLOCK);
    assert(!pxa_storage_service_poll(storage));
    /* Other Guest event turns remain runnable while I/O has made no progress. */
    assert(pxa_component_begin_event(test.runtime, component) == PXA_STATUS_OK);
    assert(pxa_component_finish_event(test.runtime, component, 1) == PXA_STATUS_OK);
    storage_dispatch(&test, component, PXA_STORAGE_SET, 103, "overflow", "bad");
    storage_completion(&test, component, 103, PXA_STATUS_WOULD_BLOCK, NULL);
    storage_execute_one(&executor);
    assert(backend.count == 1);
    assert(pxa_event_peek(test.runtime, component, &view) == PXA_STATUS_WOULD_BLOCK);
    assert(pxa_storage_service_poll(storage));
    storage_completion(&test, component, 101, PXA_STATUS_OK, NULL);
    storage_execute_one(&executor); assert(pxa_storage_service_poll(storage));
    storage_completion(&test, component, 102, PXA_STATUS_OK, "star1");

    /* A host-cancelled completion cannot be delivered into a reused token.
     * The already committed old write still finishes before the later GET. */
    storage_dispatch(&test, component, PXA_STORAGE_SET, 104, "checkpoint", "star2");
    assert(pxa_request_complete(test.runtime, component, 104, PXA_STATUS_CANCELLED, NULL, 0) == PXA_STATUS_OK);
    storage_completion(&test, component, 104, PXA_STATUS_CANCELLED, NULL);
    storage_dispatch(&test, component, PXA_STORAGE_GET, 104, "checkpoint", NULL);
    storage_execute_one(&executor); assert(!pxa_storage_service_poll(storage));
    assert(pxa_event_peek(test.runtime, component, &view) == PXA_STATUS_WOULD_BLOCK);
    storage_execute_one(&executor); assert(pxa_storage_service_poll(storage));
    storage_completion(&test, component, 104, PXA_STATUS_OK, "star2");

    executor.reject = PXA_STATUS_RESOURCE_LIMIT;
    storage_dispatch(&test, component, PXA_STORAGE_SET, 105, "checkpoint", "unsaved");
    storage_completion(&test, component, 105, PXA_STATUS_RESOURCE_LIMIT, NULL);
    executor.reject = PXA_STATUS_OK;
    storage_dispatch(&test, component, PXA_STORAGE_GET, 106, "checkpoint", NULL);
    storage_execute_one(&executor); assert(pxa_storage_service_poll(storage));
    storage_completion(&test, component, 106, PXA_STATUS_OK, "star2");
    storage_dispatch(&test, component, PXA_STORAGE_REMOVE, 107, "checkpoint", NULL);
    storage_execute_one(&executor); assert(pxa_storage_service_poll(storage));
    storage_completion(&test, component, 107, PXA_STATUS_OK, NULL);
    assert(backend.count == 0);
    /* Teardown drains committed writes even when the Guest/runtime has gone.
     * No poll is allowed after runtime destruction. */
    storage_dispatch(&test, component, PXA_STORAGE_SET, 108, "checkpoint", "star3");
    destroy_runtime(&test);
    storage_execute_one(&executor);
    assert(backend.count == 1 && !executor.count);
    free(jobs); free(workspace);
    puts("Storage async: nonblocking owner, owned payloads, FIFO saves/reads, bounded admission, reused tokens, executor failure and shutdown drain passed");
}

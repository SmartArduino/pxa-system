#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "pxa/runtime.h"
#include "pxa/wire.h"

#define SNAPSHOT_ITERATIONS UINT32_C(1000000)
#define QUEUE_ITERATIONS UINT32_C(200000)

static uint64_t monotonic_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static double ns_per_operation(uint64_t start, uint64_t end,
                               uint32_t iterations) {
    return (double)(end - start) / (double)iterations;
}

int main(void) {
    pxa_runtime_limits_t limits;
    pxa_runtime_t *runtime = NULL;
    pxa_component_t component = PXA_COMPONENT_INVALID;
    pxa_component_snapshot_t snapshot;
    pxa_writer_t writer;
    uint8_t message[32];
    uint8_t event_bytes[32];
    uint8_t marker = 7;
    uint32_t index;
    volatile uint64_t checksum = 0;
    uint64_t start;
    uint64_t end;
    size_t workspace_size;
    void *workspace;

    pxa_runtime_limits_init(&limits);
    limits.max_components = 1;
    limits.max_requests = 8;
    limits.max_requests_per_component = 8;
    limits.max_handles = 8;
    limits.max_events = 8;
    limits.mailbox_capacity = 8;
    limits.reliable_event_reserve = 2;
    limits.max_revoked_authorities_per_component = 16;
    limits.max_services = 1;
    limits.event_block_size = 64;
    limits.event_block_count = 64;
    workspace_size = pxa_runtime_workspace_size(&limits);
    workspace = malloc(workspace_size);
    if (workspace == NULL ||
        pxa_runtime_init(workspace, workspace_size, &limits, &runtime) !=
            PXA_STATUS_OK ||
        pxa_component_create(runtime, 1, &component) != PXA_STATUS_OK ||
        pxa_component_begin_start(runtime, component) != PXA_STATUS_OK ||
        pxa_component_finish_start(runtime, component, PXA_STATUS_OK) !=
            PXA_STATUS_OK) {
        free(workspace);
        return 1;
    }
    pxa_writer_init(&writer, message, sizeof(message));
    if (pxa_writer_message(&writer, 3, UINT16_C(0x8001), 0, &marker, 1) !=
        PXA_STATUS_OK) {
        pxa_runtime_deinit(runtime);
        free(workspace);
        return 1;
    }

    start = monotonic_ns();
    for (index = 0; index < SNAPSHOT_ITERATIONS; ++index) {
        checksum += (uint64_t)pxa_component_snapshot(runtime, component,
                                                     &snapshot);
        checksum += snapshot.state;
    }
    end = monotonic_ns();
    printf("native_c snapshot_ns %.2f\n",
           ns_per_operation(start, end, SNAPSHOT_ITERATIONS));

    start = monotonic_ns();
    for (index = 0; index < QUEUE_ITERATIONS; ++index) {
        checksum += (uint64_t)pxa_event_post(runtime, component, message,
                                             writer.size, 1, 0);
        {
            size_t read_size = 0;
            checksum += (uint64_t)pxa_event_pop(
                runtime, component, event_bytes, sizeof(event_bytes), &read_size);
            checksum += read_size;
        }
    }
    end = monotonic_ns();
    printf("native_c event_roundtrip_ns %.2f\n",
           ns_per_operation(start, end, QUEUE_ITERATIONS));

    start = monotonic_ns();
    for (index = 0; index < QUEUE_ITERATIONS; ++index) {
        const uint32_t request_id = index + 1u;
        checksum += (uint64_t)pxa_request_begin(runtime, component, request_id,
                                                5, 1, 0);
        checksum += (uint64_t)pxa_request_complete(
            runtime, component, request_id, PXA_STATUS_OK, NULL, 0);
        {
            size_t read_size = 0;
            checksum += (uint64_t)pxa_event_pop(
                runtime, component, event_bytes, sizeof(event_bytes), &read_size);
            checksum += read_size;
        }
    }
    end = monotonic_ns();
    printf("native_c request_roundtrip_ns %.2f\n",
           ns_per_operation(start, end, QUEUE_ITERATIONS));
    printf("native_c workspace_bytes %zu\n", workspace_size);

    pxa_runtime_deinit(runtime);
    free(workspace);
    return checksum == 0 ? 1 : 0;
}

#include "pxa_log.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[512];
static uint32_t captured_length;

int32_t pxa_submit(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

int main(void) {
    static const char message[] = "frame complete";
    char too_long[PXA_LOG_MAX_MESSAGE_BYTES + 2u];
    pxa_event_t event;

    memset(too_long, 'x', sizeof(too_long));
    too_long[sizeof(too_long) - 1u] = '\0';

    assert(pxa_log_write(PXA_LOG_LEVEL_WARN, message) == PXA_STATUS_OK);
    assert(captured_length > 0);
    assert(pxa_parse_event(captured, captured_length, &event));
    assert(event.service == PXA_LOG_SERVICE);
    assert(event.opcode == PXA_LOG_WRITE);
    assert(event.token == 0);
    assert(event.payload_size == 1u + sizeof(message) - 1u);
    assert(event.payload[0] == PXA_LOG_LEVEL_WARN);
    assert(memcmp(event.payload + 1u, message, sizeof(message) - 1u) == 0);

    assert(pxa_log_write(PXA_LOG_LEVEL_INFO, "") != PXA_STATUS_OK);
    assert(pxa_log_write(PXA_LOG_LEVEL_INFO, too_long) != PXA_STATUS_OK);
    assert(pxa_log_write(9u, "bad level") != PXA_STATUS_OK);
    assert(pxa_log_write(PXA_LOG_LEVEL_ERROR, NULL) != PXA_STATUS_OK);
    return 0;
}

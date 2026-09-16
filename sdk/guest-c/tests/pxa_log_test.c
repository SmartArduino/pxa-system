#include "pxa_log.h"

#include <assert.h>
#include <string.h>

static uint8_t captured[320];
static uint32_t captured_length;

int32_t pxa_control(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(captured));
    memcpy(captured, data, length);
    captured_length = length;
    return PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t *data,
               uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

int main(void) {
    static const uint8_t bytes[] = "frame complete";
    char too_long[PXA_LOG_MAX_MESSAGE_BYTES + 2u];
    memset(too_long, 'x', sizeof(too_long));
    too_long[sizeof(too_long) - 1u] = '\0';
    assert(pxa_log_write(PXA_LOG_LEVEL_WARN, bytes, sizeof(bytes) - 1u));
    assert(captured_length == 12u + 1u + sizeof(bytes) - 1u);
    assert(pxa_read_u16(captured) == PXA_SERVICE_LOG);
    assert(pxa_read_u16(captured + 2) == PXA_LOG_WRITE);
    assert(pxa_read_u32(captured + 4) == 0);
    assert(pxa_read_u32(captured + 8) == sizeof(bytes));
    assert(captured[12] == PXA_LOG_LEVEL_WARN);
    assert(memcmp(captured + 13, bytes, sizeof(bytes) - 1u) == 0);
    assert(pxa_log_info("ready"));
    assert(!pxa_log_message(PXA_LOG_LEVEL_INFO, ""));
    assert(!pxa_log_message(PXA_LOG_LEVEL_INFO, too_long));
    assert(!pxa_log_message(9, "bad level"));
    return 0;
}

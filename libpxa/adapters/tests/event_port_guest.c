#include "pxa.h"
#include "pxa_event_port.h"

static uint8_t mode;
static uint64_t foreign_port;
static uint64_t old_port;
static uint8_t bytes[128], saved[128], large[4097];

static int control(unsigned opcode) {
    return pxa_control_call(0x7ffe, (uint16_t)opcode, 0, NULL, 0);
}
#define REQUIRE(condition) do { if (!(condition)) { (void)control(1000 + __LINE__); return PXA_STATUS_INTERNAL; } } while (0)

int32_t pxa_app_start(const uint8_t *config, uint32_t size) {
    if (size != 9) return PXA_STATUS_INVALID_ARGUMENT;
    mode = config[0]; foreign_port = pxa_load_u64(config + 1);
    int status = pxa_event_port_open(1);
    if (mode == 3) return status == PXA_STATUS_UNSUPPORTED ? 0 : PXA_STATUS_INTERNAL;
    return status;
}

int32_t pxa_app_on_event(const uint8_t *data, uint32_t size) {
    pxa_event_t event;
    pxa_event_port_result_t opened;
    REQUIRE(pxa_parse_event(data, size, &event));
    REQUIRE(pxa_event_port_parse_open(&event, event.token, &opened) && !opened.status);
    uint64_t port = opened.handle;
    if (event.token == 2) {
        REQUIRE(port != old_port);
        REQUIRE(pxa_close_handle(port) == 0);
        REQUIRE(control(100) == 0);
        return PXA_EVENT_HANDLED;
    }
    REQUIRE(event.token == 1);
    if (mode == 1 || mode == 4) {
        REQUIRE(control(mode == 1 ? 3 : 5) == 0);
        (void)pxa_event_port_wait(port, bytes, sizeof(bytes), 1000000);
        (void)control(101); /* A cancelled import must unwind before this. */
        return PXA_STATUS_INTERNAL;
    }
    if (mode == 2) {
        (void)pxa_io(port, PXA_EVENT_PORT_IO_WAIT, (uint8_t *)(uintptr_t)0xfffffff0, 128);
        (void)control(101);
        return PXA_STATUS_INTERNAL;
    }
    REQUIRE(pxa_event_port_open(99) == PXA_STATUS_BAD_STATE);
    REQUIRE(pxa_event_port_wait(port, bytes, sizeof(bytes), 0) == PXA_STATUS_WOULD_BLOCK);
    REQUIRE(pxa_event_port_wait(port, bytes, sizeof(bytes), 1000000) == PXA_STATUS_TIMED_OUT);
    REQUIRE(control(4) == 0);
    REQUIRE(pxa_event_port_wait(port, bytes, sizeof(bytes), 1000000) == PXA_STATUS_TIMED_OUT);
    REQUIRE(pxa_io(foreign_port, PXA_EVENT_PORT_IO_WAIT, bytes, sizeof(bytes)) == PXA_STATUS_NOT_FOUND);
    pxa_store_u64(bytes, PXA_EVENT_PORT_MAX_WAIT_US + 1);
    REQUIRE(pxa_io(port, PXA_EVENT_PORT_IO_WAIT, bytes, sizeof(bytes)) == PXA_STATUS_LIMIT_EXCEEDED);
    pxa_store_u64(bytes, 0);
    REQUIRE(pxa_io(port, PXA_EVENT_PORT_IO_WAIT, bytes, PXA_HEADER_BYTES - 1) == PXA_STATUS_INVALID_ARGUMENT);
    pxa_store_u64(large, 0);
    REQUIRE(pxa_io(port, PXA_EVENT_PORT_IO_WAIT, large, sizeof(large)) == PXA_STATUS_INVALID_ARGUMENT);
    REQUIRE(control(1) == 0);
    for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = (uint8_t)(i * 7);
    pxa_store_u64(bytes, 0);
    for (unsigned i = 0; i < sizeof(bytes); ++i) saved[i] = bytes[i];
    REQUIRE(pxa_io(port, PXA_EVENT_PORT_IO_WAIT, bytes, PXA_HEADER_BYTES) == PXA_STATUS_RESOURCE_LIMIT);
    for (unsigned i = 0; i < sizeof(bytes); ++i) REQUIRE(bytes[i] == saved[i]);
    int count = pxa_event_port_wait(port, bytes, sizeof(bytes), 0);
    REQUIRE(count == PXA_HEADER_BYTES + 64);
    REQUIRE(pxa_parse_event(bytes, (unsigned)count, &event));
    REQUIRE(event.service == 0x7ffd && event.opcode == 1 && event.payload_size == 64);
    for (unsigned i = 0; i < 64; ++i) REQUIRE(event.payload[i] == (uint8_t)(i ^ 0xa5));
    size_t previous_pages = __builtin_wasm_memory_grow(0, 1);
    REQUIRE(previous_pages != (size_t)-1);
    uint8_t *grown = (uint8_t *)(uintptr_t)(previous_pages * 65536u + 16u);
    REQUIRE(control(2) == 0);
    count = pxa_event_port_wait(port, grown, 128, 1000000);
    REQUIRE(count == PXA_HEADER_BYTES + 64 && pxa_parse_event(grown, (unsigned)count, &event));
    for (unsigned i = 0; i < 64; ++i) REQUIRE(event.payload[i] == (uint8_t)(i ^ 0xa5));
    old_port = port;
    REQUIRE(pxa_close_handle(port) == 0);
    REQUIRE(pxa_event_port_wait(port, bytes, sizeof(bytes), 0) == PXA_STATUS_NOT_FOUND);
    REQUIRE(pxa_event_port_open(2) == 0);
    return PXA_EVENT_HANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }

#include "../../testing/pxa_control_capture.h"
#include "pxa_game_render_spans.h"
#include <assert.h>
#include <string.h>

static uint8_t captured[120];
static int32_t failure;
static int short_reply, reserved;
int32_t pxa_test_control(const uint8_t *p, uint32_t n) {
    (void)p; (void)n; return PXA_STATUS_OK;
}
int32_t pxa_io(uint64_t handle, uint32_t op, uint8_t *p, uint32_t n) {
    assert(handle == 77);
    if (failure) return failure;
    if (op == PXA_GAME_RENDER_IO_EXECUTE_SPANS) {
        assert(n <= sizeof(captured)); memcpy(captured, p, n);
    } else {
        assert(op == PXA_GAME_RENDER_IO_SPAN_TELEMETRY && n == 64);
        memset(p, 0, n); pxa_store_u64(p, UINT64_C(0x123456789));
        pxa_store_u64(p+24, 64000); pxa_store_u32(p+56, 256);
        pxa_store_u32(p+60, reserved);
    }
    return (int32_t)n-short_reply;
}
int main(void) {
    uint8_t bytes[120];
    pxa_game_render_span_batch_t batch;
    pxa_game_render_spans_header_wire_t header = {0};
    pxa_game_render_span_wire_t span = {0, 8, -65536, 0, 65536, 0}, decoded;
    header.target_offset = 4096; header.target_bytes = 32;
    pxa_game_render_span_batch_begin(&batch, bytes, sizeof(bytes), &header);
    assert(pxa_game_render_execute_spans(77, &batch) == 0);
    assert(pxa_game_render_span_append(&batch, &span));
    assert(pxa_game_render_execute_spans(77, &batch) == 96);
    assert(pxa_load_u32(captured+8) == 1);
    assert(pxa_game_render_span_decode(captured+72, 24, &decoded));
    assert(decoded.s == span.s && decoded.ds == span.ds);
    assert(pxa_game_render_span_append(&batch, &span));
    failure = PXA_STATUS_WOULD_BLOCK;
    assert(pxa_game_render_execute_spans(77, &batch) == failure);
    failure = 0;
    assert(!pxa_game_render_span_append(&batch, &span));
    assert(pxa_game_render_execute_spans(77, &batch) == PXA_STATUS_LIMIT_EXCEEDED);
    pxa_game_render_span_telemetry_t stats;
    assert(pxa_game_render_query_span_telemetry(77, &stats) == 0);
    assert(stats.batches == UINT64_C(0x123456789) && stats.pixels == 64000 && stats.max_spans == 256);
    short_reply = 1;
    assert(pxa_game_render_query_span_telemetry(77, &stats) == PXA_STATUS_PROTOCOL_ERROR);
    assert(!stats.batches && !stats.max_spans);
    short_reply = 0; reserved = 1;
    assert(pxa_game_render_query_span_telemetry(77, &stats) == PXA_STATUS_PROTOCOL_ERROR);
    return 0;
}

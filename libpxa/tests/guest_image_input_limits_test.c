#include "../../sdk/testing/pxa_control_capture.h"
#include "pxa_image_decode.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
static unsigned calls;
int32_t pxa_test_control(const uint8_t *data,uint32_t size) {
    pxa_event_t e;assert(pxa_parse_event(data,size,&e));
    assert(e.service==21 && e.opcode==8 && e.payload_size==4);
    assert(e.token==UINT64_C(0x123456789abcdef0) && pxa_load_u32(e.payload)==262144);
    ++calls;return 0;
}
int main(void) {
    const uint64_t token=UINT64_C(0x123456789abcdef0);
    assert(pxa_image_input_limits(token,0)==PXA_STATUS_INVALID_ARGUMENT && !calls);
    assert(!pxa_image_input_limits(token,262144) && calls==1);
    uint8_t payload[16]={0};
    pxa_store_u32(payload+4,16777216);pxa_store_u32(payload+8,2097152);pxa_store_u32(payload+12,524288);
    pxa_event_t e={21,8,token,payload,16};pxa_image_input_limits_t limits;
    assert(pxa_image_input_limits_parse(&e,token,&limits) && limits.available_encoded_bytes==2097152);
    pxa_store_u32(payload+8,0);assert(pxa_image_input_limits_parse(&e,token,&limits));
    pxa_store_u32(payload+8,16777217);assert(!pxa_image_input_limits_parse(&e,token,&limits));
    pxa_store_u32(payload+8,0);pxa_store_u32(payload+4,0);assert(!pxa_image_input_limits_parse(&e,token,&limits));
    pxa_store_u32(payload,(uint32_t)PXA_STATUS_UNSUPPORTED);e.payload_size=4;
    assert(pxa_image_input_limits_parse(&e,token,&limits) && limits.status==PXA_STATUS_UNSUPPORTED);
    e.payload_size=5;assert(!pxa_image_input_limits_parse(&e,token,&limits));
    pxa_image_decode_options_t options=pxa_image_decode_defaults();
    assert(options.max_encoded_bytes==131072);
    options.max_encoded_bytes=16777216;assert(pxa_image_decode_options_valid(options));
    options.max_encoded_bytes=16777217;assert(!pxa_image_decode_options_valid(options));
    puts("Guest C image-input limits PASS: wire, pressure, unsupported, malformed, bounds");
}

#ifndef PXA_TEST_CONTROL_CAPTURE_H
#define PXA_TEST_CONTROL_CAPTURE_H
#include <stdint.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
int32_t pxa_test_control(const uint8_t *, uint32_t);
static const uint8_t *pxa_test_payload;
int32_t pxa_call(uint32_t route, uint64_t token, const uint8_t *payload, uint32_t size) {
    uint8_t packet[4096];
    if (size > sizeof(packet) - 20 || (!payload && size)) return -1;
    packet[0]=(uint8_t)route; packet[1]=(uint8_t)(route>>8);
    packet[2]=(uint8_t)(route>>16); packet[3]=(uint8_t)(route>>24);
    for (unsigned i=0;i<8;++i) packet[4+i]=(uint8_t)(token>>(8*i));
    for (unsigned i=0;i<4;++i) packet[12+i]=(uint8_t)(size>>(8*i));
    memset(packet+16,0,4);
    if (size) memcpy(packet+20,payload,size);
    pxa_test_payload = payload;
    return pxa_test_control(packet,size+20);
}
#ifdef __cplusplus
}
#endif
#endif

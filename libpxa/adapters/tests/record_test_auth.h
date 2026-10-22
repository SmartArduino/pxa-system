#ifndef PXA_RECORD_TEST_AUTH_H
#define PXA_RECORD_TEST_AUTH_H
#include "pxa/posix/pxa_posix_install_record.h"
#include <openssl/hmac.h>
#include <string.h>
typedef struct {uint8_t key[32];int available;} record_test_auth_context_t;
static pxa_status_t record_test_tag(void *context,pxa_bytes_t purpose,pxa_bytes_t binding,
    pxa_bytes_t body,uint8_t output[32]) {
    record_test_auth_context_t *auth=context;
    uint8_t framed[2048];size_t size=12;unsigned bytes=0;
    if(!auth->available)return PXA_STATUS_UNAVAILABLE;
    if(purpose.size>64 || binding.size>512 || body.size>sizeof(framed)-12-purpose.size-binding.size)
        return PXA_STATUS_INVALID_ARGUMENT;
    pxa_write_u16(framed,(uint16_t)purpose.size);pxa_write_u16(framed+2,(uint16_t)binding.size);
    pxa_write_u64(framed+4,(uint64_t)body.size);
    memcpy(framed+size,purpose.data,purpose.size);size+=purpose.size;
    memcpy(framed+size,binding.data,binding.size);size+=binding.size;
    memcpy(framed+size,body.data,body.size);size+=body.size;
    return HMAC(EVP_sha256(),auth->key,sizeof(auth->key),framed,size,output,&bytes) && bytes==32?
        PXA_STATUS_OK:PXA_STATUS_INTERNAL;
}
#endif

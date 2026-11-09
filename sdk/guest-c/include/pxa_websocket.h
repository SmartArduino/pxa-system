/* SPDX-License-Identifier: MIT */
#ifndef PXA_GUEST_WEBSOCKET_H
#define PXA_GUEST_WEBSOCKET_H
#include "pxa_core.h"
#define PXA_WEBSOCKET_SERVICE 9u
#define PXA_WEBSOCKET_OPEN 9u
#define PXA_NET_FEATURE_WEBSOCKET 512u
/* Native message transport, distinct from raw TCP. IO READ/WRITE contain
 * opcode:u8 (1=text, 2=binary) then one complete message. A short READ leaves
 * the message queued. WRITE is atomic; WOULD_BLOCK consumes no bytes.
 * net.socket/outbound or the exact ws[s] URL grant is required. */
static inline int32_t pxa_websocket_open(uint64_t token,uint64_t permission,
    const char *url,uint16_t url_size,uint32_t buffer_bytes,
    uint32_t max_message_bytes,uint32_t timeout_ms,uint8_t *scratch,uint32_t capacity){
  if(!token||!(permission>>32)||!url||!url_size||url_size>512||
     !scratch||capacity<24u+url_size)return -1;
  pxa_store_u64(scratch,permission);pxa_store_u16(scratch+8,url_size);
  pxa_store_u16(scratch+10,0);pxa_store_u32(scratch+12,buffer_bytes);
  pxa_store_u32(scratch+16,max_message_bytes);pxa_store_u32(scratch+20,timeout_ms);
  for(unsigned i=0;i<url_size;++i)scratch[24+i]=(uint8_t)url[i];
  return pxa_control_call(9,9,token,scratch,24u+url_size);
}
#endif

#include "../desktop_net.c"
#include <assert.h>

int main(void) {
    desktop_net_slot_t slot={0};
    strcpy(slot.wanted_names[0],"content-range");
    strcpy(slot.wanted_names[1],"etag");
    slot.wanted_count=2;
    char range[]="Content-Range: bytes 0-32767/258777\r\n";
    char etag[]="ETag: \"fixture-v1\"\r\n";
    char type[]="Content-Type: text/plain; charset=utf-8\r\n";
    assert(desktop_net_header_cb(range,1,strlen(range),&slot)==strlen(range));
    assert(desktop_net_header_cb(etag,1,strlen(etag),&slot)==strlen(etag));
    assert(desktop_net_header_cb(type,1,strlen(type),&slot)==strlen(type));
    assert(slot.response_header_count==2);
    assert(memcmp(slot.response_headers[0].name.data,"content-range",13)==0);
    assert(memcmp(slot.response_headers[1].name.data,"etag",4)==0);
    assert(strcmp(slot.content_type,"text/plain; charset=utf-8")==0);
    puts("Desktop HTTP: mixed-case response names normalize to PXA wire names OK");
}

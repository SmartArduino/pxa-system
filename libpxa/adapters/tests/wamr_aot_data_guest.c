#include <stdint.h>

/* Both data segments and export names must survive source-buffer release. */
static volatile uint32_t initialized_data[3] = {
    UINT32_C(0x11223344), UINT32_C(0x55667788), UINT32_C(0x99aabbcc)};
static volatile char initialized_text[] = "mainland sans";

int32_t pxa_app_start(const uint8_t *config, uint32_t size) {
    (void)config; (void)size;
    return initialized_data[0] == UINT32_C(0x11223344) &&
           initialized_data[1] == UINT32_C(0x55667788) &&
           initialized_data[2] == UINT32_C(0x99aabbcc) &&
           initialized_text[0] == 'm' && initialized_text[12] == 's' ? 0 : -1;
}
int32_t pxa_app_on_event(const uint8_t *event, uint32_t size) {
    (void)event; (void)size; return 0;
}
void pxa_app_stop(uint32_t reason) { (void)reason; }

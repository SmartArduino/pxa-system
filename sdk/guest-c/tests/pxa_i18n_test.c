#include <assert.h>
#include <string.h>

#include "pxa_i18n.h"

int main(void) {
    const pxa_i18n_catalog_t catalogs[] = {
        {"en", 2, NULL, 0}, {"fr", 2, NULL, 0},
    };
    const pxa_i18n_bundle_t bundle = {catalogs, 2, 0};
    const uint8_t payload[] = {1, 0, 2, 0, 'f', 'r'};
    pxa_i18n_t i18n;
    pxa_event_t event = {
        PXA_SERVICE_SYSTEM, PXA_SYSTEM_CONFIGURATION_EVENT, 0,
        payload, sizeof(payload),
    };
    pxa_i18n_init(&i18n, &bundle);
    assert(strcmp(i18n.locale, "en") == 0);
    assert(pxa_i18n_handle_event(&i18n, &event) == 1);
    assert(strcmp(i18n.locale, "fr") == 0);
    assert(pxa_i18n_handle_event(&i18n, &event) == 2);
    event.token = UINT64_C(0x100000000);
    assert(pxa_i18n_handle_event(&i18n, &event) == 0);
    event.token = 0;
    event.payload_size--;
    assert(pxa_i18n_handle_event(&i18n, &event) == 0);
    return 0;
}

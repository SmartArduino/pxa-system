#include "pxa/profile.h"
#include <assert.h>
#include <string.h>

int main(void) {
    static const uint8_t target[] = "linux-x86_64";
    static const uint8_t engine[] = "wamr";
    static const uint8_t abi[] = "wamr-pxa-aot-v6-core-1";
    pxa_package_service_capability_t services[] = {{7, {0, 1}, UINT64_C(0xffffffffffffffff)}};
    pxa_package_activation_profile_t activation = {{1, 0}, services, 1};
    pxa_package_host_profile_t host = {0};
    char output[512];
    char guarded[4] = {'A', 'B', 'C', 'D'};
    host.target = (pxa_bytes_t){target, sizeof(target) - 1};
    host.engine = (pxa_bytes_t){engine, sizeof(engine) - 1};
    host.engine_abi = (pxa_bytes_t){abi, sizeof(abi) - 1};
    host.memory_model = PXA_MEMORY_WASM32;
    assert(pxa_package_profile_record(&host, &activation, 0, 0, output, sizeof(output)) == PXA_STATUS_OK);
    assert(strstr(output, "\"core\":[1,0]") != NULL && strstr(output, "\"wasm\":false") != NULL);
    /* Firmware PXADB uses its existing 320-byte frame buffer. */
    assert(pxa_package_profile_record(&host, &activation, 1, 0, output, 320) == PXA_STATUS_OK);
    assert(pxa_package_profile_record(&host, &activation, 1, 1, output, sizeof(output)) == PXA_STATUS_OK);
    assert(strstr(output, "18446744073709551615") != NULL);
    assert(pxa_package_profile_record(&host, &activation, 1, 2, output, sizeof(output)) == PXA_STATUS_NOT_FOUND);
    assert(pxa_package_profile_record(&host, &activation, 1, 1, guarded + 1, 2) == PXA_STATUS_LIMIT_EXCEEDED);
    assert(guarded[0] == 'A' && guarded[1] == '\0' && guarded[3] == 'D');
    host.target = (pxa_bytes_t){(const uint8_t *)"a\"b", 3};
    assert(pxa_package_profile_record(&host, &activation, 1, 0, output, sizeof(output)) == PXA_STATUS_INVALID_ARGUMENT);
    return 0;
}

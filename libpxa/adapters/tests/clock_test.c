#define _POSIX_C_SOURCE 200809L
#include "pxa/posix/pxa_posix_clock.h"
#include "pxa/wire.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void check(const char *zone, int64_t seconds, int offset, int dst) {
    uint8_t bytes[PXA_CLOCK_WALL_TIME_BYTES];
    assert(setenv("TZ", zone, 1) == 0);
    tzset();
    assert(pxa_posix_clock_wall_time(seconds * INT64_C(1000000) + 123456,
                                    987654321, bytes) == PXA_STATUS_OK);
    assert(pxa_read_u64(bytes) == (uint64_t)(seconds * INT64_C(1000000) + 123456));
    assert(pxa_read_u64(bytes + 8) == 987654321);
    assert(pxa_read_u32(bytes + 16) == (uint32_t)offset);
    assert(pxa_read_u32(bytes + 20) == (uint32_t)dst);
}

int main(void) {
    const char *old = getenv("TZ");
    char *saved = old ? strdup(old) : NULL;
    const int64_t january = INT64_C(1704067200), july = INT64_C(1719792000);
    check("UTC0", january, 0, 0);
    check("CST-8", january, 28800, 0); // POSIX sign is opposite UTC offset.
    check("NST3:30", january, -12600, 0); // Previous year locally.
    check("NPT-5:45", january, 20700, 0);
    check("CHAST-12:45", july, 45900, 0);
    check("CST-8", january - 1, 28800, 0); // Next year locally.
    check("CST-8", -1, 28800, 0); // Pre-epoch fractional time.
    check("EST5EDT,M3.2.0/2,M11.1.0/2", january, -18000, 0);
    check("EST5EDT,M3.2.0/2,M11.1.0/2", july, -14400, 1);
    check("EST5EDT,M3.2.0/2,M11.1.0/2", INT64_C(1710053999), -18000, 0);
    check("EST5EDT,M3.2.0/2,M11.1.0/2", INT64_C(1710054000), -14400, 1);
    assert(pxa_posix_clock_wall_time(0, 0, NULL) == PXA_STATUS_INVALID_ARGUMENT);
    if (saved) { assert(setenv("TZ", saved, 1) == 0); free(saved); }
    else assert(unsetenv("TZ") == 0);
    tzset();return 0;
}

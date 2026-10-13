#define _POSIX_C_SOURCE 200809L
#include "pxa/posix/pxa_posix_clock.h"
#include "pxa/wire.h"
#include <limits.h>
#include <sys/time.h>
#include <time.h>

pxa_status_t pxa_posix_clock_wall_time(int64_t unix_us, uint64_t monotonic_us,
                                      uint8_t output[PXA_CLOCK_WALL_TIME_BYTES]) {
    int64_t seconds = unix_us / INT64_C(1000000);
    time_t instant;
    struct tm local, utc;
    int days, offset;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    if (unix_us % INT64_C(1000000) < 0) --seconds;
    instant = (time_t)seconds;
    if ((int64_t)instant != seconds || localtime_r(&instant, &local) == NULL ||
        gmtime_r(&instant, &utc) == NULL) return PXA_STATUS_UNAVAILABLE;
    days = local.tm_yday - utc.tm_yday;
    if (local.tm_year > utc.tm_year) days = 1;
    else if (local.tm_year < utc.tm_year) days = -1;
    offset = days * 86400 + (local.tm_hour - utc.tm_hour) * 3600 +
        (local.tm_min - utc.tm_min) * 60 + local.tm_sec - utc.tm_sec;
    if (offset < -86400 || offset > 86400 ||
        (offset > 0 && unix_us > INT64_MAX - (int64_t)offset * 1000000) ||
        (offset < 0 && unix_us < INT64_MIN - (int64_t)offset * 1000000))
        return PXA_STATUS_UNAVAILABLE;
    pxa_write_u64(output, (uint64_t)unix_us);
    pxa_write_u64(output + 8, monotonic_us);
    pxa_write_u32(output + 16, (uint32_t)offset);
    pxa_write_u32(output + 20, local.tm_isdst > 0 ? PXA_CLOCK_DAYLIGHT_SAVING : 0);
    return PXA_STATUS_OK;
}

pxa_status_t pxa_posix_clock_control(pxa_runtime_t *runtime,
                                    pxa_component_t component,
                                    const pxa_message_view_t *message,
                                    uint64_t monotonic_us) {
    uint8_t payload[PXA_CLOCK_WALL_TIME_BYTES];
    struct timeval now;
    uint16_t profile;
    pxa_status_t status;
    if (message == NULL || message->opcode != PXA_CLOCK_WALL_TIME ||
        message->request_token == 0 || message->payload.size != 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_component_wire_profile(runtime, component, &profile);
    if (status != PXA_STATUS_OK) return status;
    if (profile != 1) return PXA_STATUS_UNSUPPORTED;
    status = pxa_request_begin_reserved(runtime, component, message->request_token,
        PXA_CLOCK_SERVICE_ID, PXA_CLOCK_WALL_TIME, 0, sizeof(payload));
    if (status != PXA_STATUS_OK) return status;
    status = pxa_request_commit(runtime, component, message->request_token);
    if (status != PXA_STATUS_OK) return status;
    if (gettimeofday(&now, NULL) != 0 ||
        now.tv_sec > INT64_MAX / 1000000 || now.tv_sec < INT64_MIN / 1000000) {
        status = PXA_STATUS_UNAVAILABLE;
    } else {
        status = pxa_posix_clock_wall_time((int64_t)now.tv_sec * 1000000 + now.tv_usec,
                                          monotonic_us, payload);
    }
    return pxa_request_complete(runtime, component, message->request_token, status,
        status == PXA_STATUS_OK ? payload : NULL,
        status == PXA_STATUS_OK ? sizeof(payload) : 0);
}

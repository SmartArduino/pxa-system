#ifndef PXA_LOG_GUEST_H
#define PXA_LOG_GUEST_H

#include "pxa.h"

#define PXA_LOG_WRITE 1u
#define PXA_LOG_MAX_MESSAGE_BYTES 256u

#define PXA_LOG_LEVEL_TRACE 0u
#define PXA_LOG_LEVEL_DEBUG 1u
#define PXA_LOG_LEVEL_INFO 2u
#define PXA_LOG_LEVEL_WARN 3u
#define PXA_LOG_LEVEL_ERROR 4u

static inline int pxa_log_write(uint8_t level, const uint8_t *message,
                                size_t length) {
    uint8_t packet[12u + 1u + PXA_LOG_MAX_MESSAGE_BYTES];
    pxa_writer_t writer;
    if (level > PXA_LOG_LEVEL_ERROR || message == NULL || length == 0 ||
        length > PXA_LOG_MAX_MESSAGE_BYTES) {
        return 0;
    }
    pxa_writer_init(&writer, packet, sizeof(packet));
    return pxa_put_u16(&writer, PXA_SERVICE_LOG) &&
           pxa_put_u16(&writer, PXA_LOG_WRITE) && pxa_put_u32(&writer, 0) &&
           pxa_put_u32(&writer, (uint32_t)length + 1u) &&
           pxa_put_u8(&writer, level) && pxa_put_bytes(&writer, message, length) &&
           pxa_control(writer.data, (uint32_t)writer.length) == PXA_STATUS_OK;
}

static inline int pxa_log_message(uint8_t level, const char *message) {
    size_t length = 0;
    if (message == NULL) return 0;
    while (length <= PXA_LOG_MAX_MESSAGE_BYTES && message[length] != '\0')
        ++length;
    return length != 0 && length <= PXA_LOG_MAX_MESSAGE_BYTES &&
           pxa_log_write(level, (const uint8_t *)message, length);
}

static inline int pxa_log_trace(const char *message) {
    return pxa_log_message(PXA_LOG_LEVEL_TRACE, message);
}

static inline int pxa_log_debug(const char *message) {
    return pxa_log_message(PXA_LOG_LEVEL_DEBUG, message);
}

static inline int pxa_log_info(const char *message) {
    return pxa_log_message(PXA_LOG_LEVEL_INFO, message);
}

static inline int pxa_log_warn(const char *message) {
    return pxa_log_message(PXA_LOG_LEVEL_WARN, message);
}

static inline int pxa_log_error(const char *message) {
    return pxa_log_message(PXA_LOG_LEVEL_ERROR, message);
}

#endif

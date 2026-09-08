/* Freestanding PXA guest used by the WAMR engine adapter test. Exercises the
 * two pxa.core.v0 imports end to end: FS open + pxa_io write + close, and a
 * Storage SET/GET round trip. No libc dependency; compiles with
 * clang --target=wasm32 -nostdlib. */

#include "pxa.h"

#define PXA_FS_SERVICE 5u
#define PXA_FS_OPEN 1u
#define PXA_FS_PATH 1u
#define PXA_FS_OPEN_FLAGS 3u
#define PXA_FS_OPEN_WRITE (1u << 1)

#define PXA_STORAGE_GET 1u
#define PXA_STORAGE_SET 2u

static uint32_t s_request_id = 1;
static uint8_t s_done;

static uint32_t pxa_copy(uint8_t *destination, const uint8_t *source,
                         uint32_t length) {
    uint32_t index;
    for (index = 0; index < length; ++index) {
        destination[index] = source[index];
    }
    return length;
}

static uint32_t pxa_length(const uint8_t *value) {
    uint32_t length = 0;
    while (value[length] != 0) ++length;
    return length;
}

static int32_t storage_set(const char *key, const char *value) {
    uint8_t payload[80];
    uint8_t packet[96];
    pxa_writer_t writer;
    pxa_writer_t message;
    pxa_writer_init(&writer, payload, sizeof(payload));
    if (!pxa_record(&writer, 1, (const uint8_t *)key,
                    (uint32_t)pxa_length((const uint8_t *)key)) ||
        !pxa_record(&writer, 2, (const uint8_t *)value,
                    (uint32_t)pxa_length((const uint8_t *)value))) {
        return PXA_STATUS_INTERNAL;
    }
    pxa_writer_init(&message, packet, sizeof(packet));
    if (!pxa_message(&message, PXA_SERVICE_STORAGE, PXA_STORAGE_SET,
                     s_request_id++, writer.data,
                     (uint32_t)writer.length)) {
        return PXA_STATUS_INTERNAL;
    }
    return pxa_control(message.data, (uint32_t)message.length);
}

static int32_t storage_get(const char *key) {
    uint8_t payload[40];
    uint8_t packet[64];
    pxa_writer_t writer;
    pxa_writer_t message;
    pxa_writer_init(&writer, payload, sizeof(payload));
    if (!pxa_record(&writer, 1, (const uint8_t *)key,
                    (uint32_t)pxa_length((const uint8_t *)key))) {
        return PXA_STATUS_INTERNAL;
    }
    pxa_writer_init(&message, packet, sizeof(packet));
    if (!pxa_message(&message, PXA_SERVICE_STORAGE, PXA_STORAGE_GET,
                     s_request_id++, writer.data,
                     (uint32_t)writer.length)) {
        return PXA_STATUS_INTERNAL;
    }
    return pxa_control(message.data, (uint32_t)message.length);
}

static int32_t fs_open_log(void) {
    static const uint8_t path[] = "log.txt";
    uint8_t payload[48];
    uint8_t packet[64];
    pxa_writer_t writer;
    pxa_writer_t message;
    const uint32_t flags =
        PXA_FS_OPEN_WRITE | (1u << 2) | (1u << 4); /* WRITE|CREATE|TRUNCATE */    pxa_writer_init(&writer, payload, sizeof(payload));
    if (!pxa_record(&writer, PXA_FS_PATH, path, sizeof(path) - 1) ||
        !pxa_record(&writer, PXA_FS_OPEN_FLAGS,
                    (const uint8_t *)&flags, 4)) {
        return PXA_STATUS_INTERNAL;
    }
    pxa_writer_init(&message, packet, sizeof(packet));
    if (!pxa_message(&message, PXA_FS_SERVICE, PXA_FS_OPEN, s_request_id++,
                     writer.data, (uint32_t)writer.length)) {
        return PXA_STATUS_INTERNAL;
    }
    return pxa_control(message.data, (uint32_t)message.length);
}

static int32_t close_handle(uint32_t handle) {
    uint8_t payload[4] = {(uint8_t)handle, (uint8_t)(handle >> 8),
                          (uint8_t)(handle >> 16), (uint8_t)(handle >> 24)};
    return pxa_send(PXA_SERVICE_CORE, PXA_CORE_CLOSE_HANDLE, 0, payload,
                    sizeof(payload))
               ? PXA_STATUS_OK
               : PXA_STATUS_INTERNAL;
}

uint32_t pxa_app_api_version(void) { return PXA_CORE_VERSION; }

int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    (void)config;
    (void)config_length;
    return fs_open_log();
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    static const uint8_t *previous_event;
    pxa_event_t parsed;
    if (!pxa_parse_event(event, length, &parsed)) return PXA_EVENT_UNHANDLED;
    if (parsed.service == 0x7ffe) {
        if (parsed.opcode == 1) {
            previous_event = event;
            return __builtin_wasm_memory_grow(0, 1) == (size_t)-1
                       ? PXA_STATUS_RESOURCE_LIMIT : PXA_EVENT_HANDLED;
        }
        return event == previous_event && parsed.payload_length == 4 &&
               pxa_read_u32(parsed.payload) == 0x12345678u
                   ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    /* Responses carry a 4-byte completion status before the result. */
    if (parsed.service == PXA_FS_SERVICE && parsed.opcode == PXA_FS_OPEN &&
        parsed.request_id == 1) {
        if (parsed.payload_length != 8 ||
            (int32_t)pxa_read_u32(parsed.payload) != PXA_STATUS_OK) {
            return PXA_EVENT_UNHANDLED;
        }
        {
            const uint32_t handle = pxa_read_u32(parsed.payload + 4);
            static const uint8_t body[2] = {'h', 'i'};
            if (pxa_io(handle, PXA_IO_WRITE, (uint8_t *)body, 2) != 2) {
                return PXA_EVENT_UNHANDLED;
            }
            (void)close_handle(handle);
        }
        return storage_set("hits", "1") == PXA_STATUS_OK ? PXA_EVENT_HANDLED
                                                         : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_SERVICE_STORAGE && parsed.request_id == 2 &&
        (int32_t)pxa_read_u32(parsed.payload) == PXA_STATUS_OK) {
        return storage_get("hits") == PXA_STATUS_OK ? PXA_EVENT_HANDLED
                                                    : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_SERVICE_STORAGE && parsed.request_id == 3) {
        /* GET response: status header, then the value record. */
        if (parsed.payload_length >= 8 &&
            (int32_t)pxa_read_u32(parsed.payload) == PXA_STATUS_OK) {
            const uint8_t *record = parsed.payload + 4;
            const uint16_t tag = pxa_read_u16(record);
            const uint16_t record_length = pxa_read_u16(record + 2);
            if (tag == 2 && record_length + 8 <= parsed.payload_length) {
                uint8_t value[16];
                uint32_t value_length =
                    record_length < sizeof(value) ? record_length
                                                  : (uint32_t)sizeof(value);
                value_length = pxa_copy(value, record + 4, value_length);
                value[value_length] = 0;
                if (storage_set("result", (const char *)value) ==
                    PXA_STATUS_OK) {
                    return PXA_EVENT_HANDLED;
                }
            }
        }
        return PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_SERVICE_STORAGE && parsed.request_id == 4 &&
        (int32_t)pxa_read_u32(parsed.payload) == PXA_STATUS_OK) {
        return storage_set("done", "1") == PXA_STATUS_OK ? PXA_EVENT_HANDLED
                                                         : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_SERVICE_STORAGE && parsed.request_id == 5) {
        s_done = 1;
        return PXA_EVENT_HANDLED;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) {
    (void)reason;
    s_done = 0;
}

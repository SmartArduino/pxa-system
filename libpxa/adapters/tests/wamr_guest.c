/* Current Core guest for the WAMR adapter test: FS, Storage and memory.grow. */
#include "pxa.h"
#include "pxa_fs.h"
#include "pxa_storage.h"

#define OPEN_TOKEN UINT64_C(1)
#define HITS_SET_TOKEN UINT64_C(2)
#define HITS_GET_TOKEN UINT64_C(3)
#define RESULT_SET_TOKEN UINT64_C(4)
#define DONE_SET_TOKEN UINT64_C(5)

static int32_t storage_set(uint64_t token, const char *key, size_t key_size,
                           const uint8_t *value, size_t value_size) {
    uint8_t packet[96];
    return pxa_storage_request_set(packet, sizeof(packet), token,
                                   key, key_size, value, value_size);
}

int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    (void)config;
    (void)config_length;
    return pxa_fs_request_open(OPEN_TOKEN, "log.txt", 7,
                               PXA_FS_OPEN_WRITE | PXA_FS_OPEN_CREATE |
                                   PXA_FS_OPEN_TRUNCATE);
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    static const uint8_t *previous_event;
    static const uint8_t one = '1';
    pxa_event_t parsed;
    if (!pxa_parse_event(event, length, &parsed)) return PXA_EVENT_UNHANDLED;
    if (parsed.service == 0x7ffe) {
        if (parsed.opcode == 1) {
            previous_event = event;
            return __builtin_wasm_memory_grow(0, 1) == (size_t)-1
                       ? PXA_STATUS_RESOURCE_LIMIT : PXA_EVENT_HANDLED;
        }
        return event == previous_event && parsed.payload_size == 4 &&
                       pxa_load_u32(parsed.payload) == UINT32_C(0x12345678)
                   ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    if (parsed.service == PXA_FS_SERVICE && parsed.opcode == PXA_FS_OPEN) {
        pxa_fs_open_result_t opened;
        uint8_t body[2] = {'h', 'i'};
        if (!pxa_fs_parse_open(&parsed, OPEN_TOKEN, &opened) ||
            opened.status != PXA_STATUS_OK ||
            pxa_fs_write(opened.handle, body, sizeof(body)) != 2 ||
            pxa_fs_close(opened.handle) != PXA_STATUS_OK)
            return PXA_EVENT_UNHANDLED;
        return storage_set(HITS_SET_TOKEN, "hits", 4, &one, 1) == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_STORAGE_SERVICE &&
        parsed.token == HITS_SET_TOKEN) {
        int32_t status = 0;
        return pxa_storage_parse_status(&parsed, HITS_SET_TOKEN,
                                         PXA_STORAGE_SET, &status) &&
                       status == PXA_STATUS_OK &&
                       pxa_storage_request_get(HITS_GET_TOKEN, "hits", 4) ==
                           PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_STORAGE_SERVICE &&
        parsed.token == HITS_GET_TOKEN) {
        pxa_storage_get_result_t result;
        if (!pxa_storage_parse_get(&parsed, HITS_GET_TOKEN, &result) ||
            result.status != PXA_STATUS_OK || result.value.size != 1)
            return PXA_EVENT_UNHANDLED;
        return storage_set(RESULT_SET_TOKEN, "result", 6,
                           result.value.data, result.value.size) == PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_STORAGE_SERVICE &&
        parsed.token == RESULT_SET_TOKEN) {
        int32_t status = 0;
        return pxa_storage_parse_status(&parsed, RESULT_SET_TOKEN,
                                         PXA_STORAGE_SET, &status) &&
                       status == PXA_STATUS_OK &&
                       storage_set(DONE_SET_TOKEN, "done", 4, &one, 1) ==
                           PXA_STATUS_OK
                   ? PXA_EVENT_HANDLED : PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_STORAGE_SERVICE &&
        parsed.token == DONE_SET_TOKEN)
        return PXA_EVENT_HANDLED;
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) { (void)reason; }

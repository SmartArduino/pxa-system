#include "pxsys/intent_wire.h"

#include <limits.h>
#include <string.h>

#define PXSYS_INTENT_WIRE_HEADER_SIZE ((size_t)40)
#define PXSYS_INTENT_WIRE_TARGET_SIZE PXSYS_PUBLISHER_ROOT_BYTES
#define PXSYS_INTENT_WIRE_MAGIC UINT32_C(0x5058494e)

static void write_u16(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)(value >> 8u);
    output[1] = (uint8_t)value;
}

static void write_u32(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24u);
    output[1] = (uint8_t)(value >> 16u);
    output[2] = (uint8_t)(value >> 8u);
    output[3] = (uint8_t)value;
}

static void write_u64(uint8_t* output, uint64_t value) {
    write_u32(output, (uint32_t)(value >> 32u));
    write_u32(output + 4, (uint32_t)value);
}

static uint16_t read_u16(const uint8_t* input) {
    return (uint16_t)(((uint16_t)input[0] << 8u) | input[1]);
}

static uint32_t read_u32(const uint8_t* input) {
    return ((uint32_t)input[0] << 24u) | ((uint32_t)input[1] << 16u) | ((uint32_t)input[2] << 8u) |
           input[3];
}

static uint64_t read_u64(const uint8_t* input) {
    return ((uint64_t)read_u32(input) << 32u) | read_u32(input + 4);
}

static int field_valid(pxsys_string_t value) {
    return value.size <= UINT32_MAX && (value.size == 0 || value.data != NULL);
}

static int add_size(size_t* total, size_t value) {
    if (*total > SIZE_MAX - value)
        return 0;
    *total += value;
    return 1;
}

pxsys_status_t pxsys_intent_wire_size(const pxsys_intent_t* intent, size_t* size) {
    size_t total;
    if (size == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *size = 0;
    if (intent == NULL || intent->struct_size < sizeof(*intent) || !field_valid(intent->action) ||
        !field_valid(intent->uri) || !field_valid(intent->mime_type) ||
        intent->arguments.size > UINT32_MAX ||
        (intent->arguments.size != 0 && intent->arguments.data == NULL) ||
        (intent->target != NULL &&
         (intent->target->app_id.size == 0 || intent->target->app_id.size > UINT32_MAX ||
          intent->target->app_id.data == NULL))) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    total = PXSYS_INTENT_WIRE_HEADER_SIZE;
    if (intent->target != NULL) {
        if (!add_size(&total, PXSYS_INTENT_WIRE_TARGET_SIZE) ||
            !add_size(&total, intent->target->app_id.size))
            return PXSYS_STATUS_RESOURCE_LIMIT;
    }
    if (!add_size(&total, intent->action.size) || !add_size(&total, intent->uri.size) ||
        !add_size(&total, intent->mime_type.size) || !add_size(&total, intent->arguments.size)) {
        return PXSYS_STATUS_RESOURCE_LIMIT;
    }
    *size = total;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_intent_wire_encode(const pxsys_intent_t* intent, void* buffer, size_t capacity,
                                        size_t* written) {
    uint8_t* output = (uint8_t*)buffer;
    uint8_t* cursor;
    size_t required;
    pxsys_status_t status;
    if (written == NULL)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    *written = 0;
    status = pxsys_intent_wire_size(intent, &required);
    if (status != PXSYS_STATUS_OK)
        return status;
    if (output == NULL || capacity < required)
        return PXSYS_STATUS_RESOURCE_LIMIT;
    write_u32(output, PXSYS_INTENT_WIRE_MAGIC);
    write_u16(output + 4, PXSYS_INTENT_WIRE_VERSION);
    write_u16(output + 6, (uint16_t)PXSYS_INTENT_WIRE_HEADER_SIZE);
    write_u32(output + 8, intent->flags);
    write_u64(output + 12, intent->correlation_id);
    write_u32(output + 20, intent->target == NULL ? 0u : (uint32_t)intent->target->app_id.size);
    write_u32(output + 24, (uint32_t)intent->action.size);
    write_u32(output + 28, (uint32_t)intent->uri.size);
    write_u32(output + 32, (uint32_t)intent->mime_type.size);
    write_u32(output + 36, (uint32_t)intent->arguments.size);
    cursor = output + PXSYS_INTENT_WIRE_HEADER_SIZE;
    if (intent->target != NULL) {
        memcpy(cursor, intent->target->publisher_root, PXSYS_PUBLISHER_ROOT_BYTES);
        cursor += PXSYS_PUBLISHER_ROOT_BYTES;
        memcpy(cursor, intent->target->app_id.data, intent->target->app_id.size);
        cursor += intent->target->app_id.size;
    }
    if (intent->action.size != 0)
        memcpy(cursor, intent->action.data, intent->action.size);
    cursor += intent->action.size;
    if (intent->uri.size != 0)
        memcpy(cursor, intent->uri.data, intent->uri.size);
    cursor += intent->uri.size;
    if (intent->mime_type.size != 0)
        memcpy(cursor, intent->mime_type.data, intent->mime_type.size);
    cursor += intent->mime_type.size;
    if (intent->arguments.size != 0)
        memcpy(cursor, intent->arguments.data, intent->arguments.size);
    *written = required;
    return PXSYS_STATUS_OK;
}

pxsys_status_t pxsys_intent_wire_decode(const void* buffer, size_t size, pxsys_intent_t* intent,
                                        pxsys_app_identity_t* target_storage) {
    const uint8_t* input = (const uint8_t*)buffer;
    const uint8_t* cursor;
    size_t action_size;
    size_t uri_size;
    size_t mime_size;
    size_t arguments_size;
    size_t target_app_id_size = 0;
    size_t required;
    if (intent == NULL || target_storage == NULL || input == NULL ||
        size < PXSYS_INTENT_WIRE_HEADER_SIZE || read_u32(input) != PXSYS_INTENT_WIRE_MAGIC ||
        read_u16(input + 4) != PXSYS_INTENT_WIRE_VERSION) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    if (read_u16(input + 6) != PXSYS_INTENT_WIRE_HEADER_SIZE)
        return PXSYS_STATUS_UNSUPPORTED;
    target_app_id_size = read_u32(input + 20);
    action_size = read_u32(input + 24);
    uri_size = read_u32(input + 28);
    mime_size = read_u32(input + 32);
    arguments_size = read_u32(input + 36);
    required = PXSYS_INTENT_WIRE_HEADER_SIZE;
    if (target_app_id_size != 0) {
        if (size < required + PXSYS_INTENT_WIRE_TARGET_SIZE)
            return PXSYS_STATUS_INVALID_ARGUMENT;
        required += PXSYS_INTENT_WIRE_TARGET_SIZE;
    }
    if (action_size > size - required || uri_size > size - required - action_size ||
        mime_size > size - required - action_size - uri_size ||
        arguments_size > size - required - action_size - uri_size - mime_size ||
        target_app_id_size >
            size - required - action_size - uri_size - mime_size - arguments_size) {
        return PXSYS_STATUS_INVALID_ARGUMENT;
    }
    required += target_app_id_size + action_size + uri_size + mime_size + arguments_size;
    if (required != size)
        return PXSYS_STATUS_INVALID_ARGUMENT;
    memset(intent, 0, sizeof(*intent));
    memset(target_storage, 0, sizeof(*target_storage));
    intent->struct_size = sizeof(*intent);
    intent->flags = read_u32(input + 8);
    intent->correlation_id = read_u64(input + 12);
    cursor = input + PXSYS_INTENT_WIRE_HEADER_SIZE;
    if (target_app_id_size != 0) {
        memcpy(target_storage->publisher_root, cursor, PXSYS_PUBLISHER_ROOT_BYTES);
        cursor += PXSYS_PUBLISHER_ROOT_BYTES;
        target_storage->app_id.data = (const char*)cursor;
        target_storage->app_id.size = target_app_id_size;
        cursor += target_app_id_size;
        intent->target = target_storage;
    }
    intent->action.data = (const char*)cursor;
    intent->action.size = action_size;
    cursor += action_size;
    intent->uri.data = (const char*)cursor;
    intent->uri.size = uri_size;
    cursor += uri_size;
    intent->mime_type.data = (const char*)cursor;
    intent->mime_type.size = mime_size;
    cursor += mime_size;
    intent->arguments.data = cursor;
    intent->arguments.size = arguments_size;
    return PXSYS_STATUS_OK;
}

#include "pxa_ui_components.h"

#include <assert.h>

static uint32_t opcode_counts[10];
static uint32_t command_counts[6];
static size_t command_payload_remaining;
static int fail_commit;

int32_t pxa_control(const uint8_t* data, uint32_t length) {
    uint16_t opcode;
    assert(data != NULL && length >= 12);
    opcode = pxa_read_u16(data + 2);
    assert(pxa_read_u16(data) == PXA_SERVICE_UI);
    assert(pxa_read_u32(data + 8) == length - 12u);
    if (opcode < sizeof(opcode_counts) / sizeof(opcode_counts[0]))
        ++opcode_counts[opcode];
    if (opcode == PXA_UI_TX_WRITE) {
        const uint8_t* stream = data + 16;
        size_t stream_size = length - 16u;
        if (command_payload_remaining == 0) {
            uint8_t command;
            assert(stream_size == 4);
            command = stream[0];
            assert(command < sizeof(command_counts) /
                                 sizeof(command_counts[0]));
            ++command_counts[command];
            command_payload_remaining = pxa_read_u16(stream + 2);
        } else {
            assert(stream_size <= command_payload_remaining);
            command_payload_remaining -= stream_size;
        }
    }
    return fail_commit && opcode == PXA_UI_TX_COMMIT ? PXA_STATUS_INTERNAL
                                                      : PXA_STATUS_OK;
}

int32_t pxa_io(uint32_t handle, uint32_t operation, uint8_t* data,
               uint32_t length) {
    (void)handle;
    (void)operation;
    (void)data;
    (void)length;
    return PXA_STATUS_UNSUPPORTED;
}

int main(void) {
    pxa_ui_builder_t builder = {0};
    uint8_t scratch[64];
    uint8_t environment_records[128];
    uint8_t config[160];
    uint8_t encoded[16] = {0};
    uint32_t ancestors[4];
    uint32_t generation = 0;
    uint32_t routes[3];
    pxa_ui_nav_stack_t navigation;
    pxa_ui_environment_t environment;
    pxa_writer_t environment_writer;
    pxa_writer_t config_writer;
    uint32_t index;

    pxa_writer_init(&environment_writer, environment_records,
                    sizeof(environment_records));
    pxa_ui_write_u32(encoded, PXA_UI_PRIMARY_SURFACE);
    assert(pxa_record(&environment_writer, 1, encoded, 4));
    pxa_ui_write_u32(encoded, 800);
    assert(pxa_record(&environment_writer, 2, encoded, 4));
    pxa_ui_write_u32(encoded, 480);
    assert(pxa_record(&environment_writer, 3, encoded, 4));
    pxa_ui_write_u32(encoded, UINT32_C(2) << 16);
    assert(pxa_record(&environment_writer, 4, encoded, 4));
    pxa_ui_write_u32(encoded, UINT32_C(1) << 16);
    assert(pxa_record(&environment_writer, 5, encoded, 4));
    pxa_ui_write_u32(encoded, 1);
    pxa_ui_write_u32(encoded + 4, 2);
    pxa_ui_write_u32(encoded + 8, 3);
    pxa_ui_write_u32(encoded + 12, 4);
    assert(pxa_record(&environment_writer, 6, encoded, 16));
    encoded[0] = 1;
    assert(pxa_record(&environment_writer, 7, encoded, 1));
    encoded[0] = 0;
    assert(pxa_record(&environment_writer, 8, encoded, 1));
    pxa_ui_write_u64(encoded, UINT64_C(3));
    assert(pxa_record(&environment_writer, 9, encoded, 8));
    pxa_ui_write_u64(encoded,
                     PXA_UI_FEATURE_CANVAS | (UINT64_C(1) << 40));
    assert(pxa_record(&environment_writer, 10, encoded, 8));
    pxa_ui_write_u32(encoded, 1024);
    assert(pxa_record(&environment_writer, 11, encoded, 4));
    pxa_writer_init(&config_writer, config, sizeof(config));
    assert(pxa_record(&config_writer, PXA_UI_CONFIG_ENVIRONMENT,
                      environment_writer.data, environment_writer.length));
    assert(pxa_ui_parse_start_environment(config_writer.data,
                                          config_writer.length,
                                          &environment));
    assert(environment.width == 800 && environment.height == 480);
    assert(environment.safe_insets[3] == 4);
    assert(environment.features ==
           (PXA_UI_FEATURE_CANVAS | (UINT64_C(1) << 40)));

    {
        pxa_ui_transaction_t transaction = {0};
        transaction.active = 1;
        assert(!pxa_ui_set_dp(&transaction, 1, PXA_UI_PROPERTY_X, 7));
        assert(!pxa_ui_set_dp(&transaction, 1, PXA_UI_PROPERTY_GAP, -1));
    }

    assert(pxa_ui_builder_begin(
        &builder, &generation, PXA_UI_PRIMARY_SURFACE, 0,
        PXA_UI_TRANSACTION_REPLACE_SURFACE, scratch, sizeof(scratch),
        ancestors, 4));
    assert(pxa_ui_builder_enter(&builder, 1, 0, PXA_UI_NODE_ROOT));
    assert(pxa_ui_builder_enter(&builder, 2, 0, PXA_UI_NODE_BOX));
    assert(pxa_ui_component_text(&builder, 3, "hello", 2,
                                 PXA_UI_THEME_TEXT));
    assert(pxa_ui_builder_leave(&builder));
    assert(pxa_ui_component_button(&builder, 4, 5, "OK",
                                   PXA_UI_THEME_PRIMARY,
                                   PXA_UI_THEME_ON_PRIMARY));
    assert(pxa_ui_builder_leave(&builder));
    assert(pxa_ui_builder_end(&builder));
    assert(generation == 1);
    assert(command_counts[PXA_UI_COMMAND_CREATE] == 5);
    assert(command_counts[PXA_UI_COMMAND_SET_PROPERTY] == 11);

    /* Node count is streamed and unrelated to the ancestor stack capacity. */
    assert(pxa_ui_builder_begin(
        &builder, &generation, PXA_UI_PRIMARY_SURFACE, 0,
        PXA_UI_TRANSACTION_REPLACE_SURFACE, scratch, sizeof(scratch),
        ancestors, 4));
    assert(pxa_ui_builder_enter(&builder, 1, 0, PXA_UI_NODE_ROOT));
    for (index = 2; index <= 5001; ++index)
        assert(pxa_ui_builder_node(&builder, index, 0, PXA_UI_NODE_TEXT));
    assert(pxa_ui_builder_leave(&builder));
    assert(pxa_ui_builder_end(&builder));
    assert(generation == 2);

    fail_commit = 1;
    assert(pxa_ui_builder_begin(
        &builder, &generation, PXA_UI_PRIMARY_SURFACE, 0,
        PXA_UI_TRANSACTION_PATCH, scratch, sizeof(scratch), ancestors, 4));
    assert(pxa_ui_builder_node(&builder, 6000, 0, PXA_UI_NODE_TEXT));
    assert(!pxa_ui_builder_end(&builder));
    assert(generation == 2);
    assert(opcode_counts[PXA_UI_TX_CANCEL] == 0);

    assert(pxa_ui_nav_init(&navigation, routes, 3, 10));
    assert(pxa_ui_nav_current(&navigation) == 10);
    assert(pxa_ui_nav_push(&navigation, 20));
    assert(pxa_ui_nav_current(&navigation) == 20);
    assert(pxa_ui_nav_pop(&navigation));
    assert(!pxa_ui_nav_pop(&navigation));
    return 0;
}

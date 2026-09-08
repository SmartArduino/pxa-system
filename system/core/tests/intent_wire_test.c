#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pxsys/intent_wire.h"

static void test_round_trip(void) {
    static const uint8_t arguments[] = {1, 2, 3, 4};
    uint8_t wire[256];
    pxsys_app_identity_t target;
    pxsys_app_identity_t decoded_target;
    pxsys_intent_t source = {0};
    pxsys_intent_t decoded;
    size_t required = 0;
    size_t written = 0;

    memset(&target, 0x5a, sizeof(target.publisher_root));
    target.app_id = pxsys_string_from_cstr("gallery");
    source.struct_size = sizeof(source);
    source.target = &target;
    source.action = pxsys_string_from_cstr("system.intent.view");
    source.uri = pxsys_string_from_cstr("file:/photo.png");
    source.mime_type = pxsys_string_from_cstr("image/png");
    source.flags = PXSYS_INTENT_FLAG_EXPECT_RESULT;
    source.correlation_id = UINT64_C(0x0102030405060708);
    source.arguments = pxsys_bytes(arguments, sizeof(arguments));

    assert(pxsys_intent_wire_size(&source, &required) == PXSYS_STATUS_OK);
    assert(pxsys_intent_wire_encode(&source, wire, required - 1, &written) ==
           PXSYS_STATUS_RESOURCE_LIMIT);
    assert(pxsys_intent_wire_encode(&source, wire, sizeof(wire), &written) == PXSYS_STATUS_OK);
    assert(written == required);
    assert(pxsys_intent_wire_decode(wire, written, &decoded, &decoded_target) == PXSYS_STATUS_OK);
    assert(decoded.target != NULL && pxsys_app_identity_equal(decoded.target, &target));
    assert(decoded.flags == source.flags && decoded.correlation_id == source.correlation_id);
    assert(decoded.action.size == source.action.size &&
           memcmp(decoded.action.data, source.action.data, source.action.size) == 0);
    assert(decoded.uri.size == source.uri.size &&
           memcmp(decoded.uri.data, source.uri.data, source.uri.size) == 0);
    assert(decoded.mime_type.size == source.mime_type.size &&
           memcmp(decoded.mime_type.data, source.mime_type.data, source.mime_type.size) == 0);
    assert(decoded.arguments.size == sizeof(arguments) &&
           memcmp(decoded.arguments.data, arguments, sizeof(arguments)) == 0);
    assert(pxsys_intent_wire_decode(wire, written - 1, &decoded, &decoded_target) ==
           PXSYS_STATUS_INVALID_ARGUMENT);
}

int main(void) {
    test_round_trip();
    puts("intent wire tests passed");
    return 0;
}

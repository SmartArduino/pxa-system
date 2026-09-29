#include <assert.h>
#include <string.h>

#include "pxa_canvas.h"

static unsigned stream_opens;
static unsigned begins;
static unsigned presents;
static unsigned writes;
static unsigned stream_writes;
static uint8_t copied[128];
static size_t copied_size;

int32_t pxa_submit(const uint8_t *packet, uint32_t size) {
    pxa_event_t message;
    assert(pxa_parse_event(packet, size, &message));
    assert(message.service == PXA_UI_SERVICE && message.token == 0);
    if (message.opcode == PXA_UI_CANVAS_STREAM_OPEN) {
        assert(message.payload_size == 12);
        ++stream_opens;
    } else if (message.opcode == PXA_UI_CANVAS_BEGIN) {
        assert(message.payload_size == 16);
        ++begins;
    } else if (message.opcode == PXA_UI_CANVAS_WRITE) {
        assert(message.payload_size > 12);
        assert(copied_size + message.payload_size - 12 <= sizeof(copied));
        memcpy(copied + copied_size, message.payload + 12,
               message.payload_size - 12);
        copied_size += message.payload_size - 12;
        ++writes;
    } else if (message.opcode == PXA_UI_CANVAS_PRESENT) {
        assert(message.payload_size == 13);
        ++presents;
    } else {
        assert(0);
    }
    return 0;
}

int32_t pxa_io(uint64_t handle, uint32_t operation, uint8_t *data,
                  uint32_t size) {
    assert(handle == UINT64_C(0x100000001) && operation == 2);
    assert(data != NULL && size != 0);
    ++stream_writes;
    return (int32_t)size;
}

int main(void) {
    {
        uint8_t bytes[30]; pxa_canvas_frame_t image;
        const uint64_t handle = UINT64_C(0x1234567800000042);
        pxa_canvas_begin(&image, bytes, sizeof(bytes));
        assert(pxa_canvas_image_handle(&image, -2, 3, 40, 50, 128, 2, handle));
        assert(image.length == 30 && image.records == 1);
        assert(bytes[0] == PXA_CANVAS_DRAW_IMAGE_HANDLE && pxa_read_u16(bytes+2) == 26);
        assert((int32_t)pxa_read_u32(bytes+4) == -2 && pxa_read_u64(bytes+22) == handle);
        pxa_canvas_begin(&image, bytes, sizeof(bytes));
        assert(!pxa_canvas_image_handle(&image,0,0,1,1,255,0,0));
        assert(!pxa_canvas_image_handle(&image,0,0,0,1,255,0,handle));
        assert(!pxa_canvas_image_handle(&image,0,0,1,1,255,3,handle));
        assert(image.length == 0);
        pxa_canvas_begin(&image, bytes, sizeof(bytes)-1);
        assert(!pxa_canvas_image_handle(&image,0,0,1,1,255,0,handle) && image.failed);
    }
    uint8_t frame_bytes[128];
    uint8_t packet[48];
    uint8_t ready_bytes[16] = {0};
    uint8_t pointer_bytes[36] = {0};
    pxa_canvas_frame_t frame;
    pxa_canvas_event_t event;
    pxa_ui_pointer_data_t pointer;
    pxa_canvas_handle_t handle = 0;
    uint32_t request = 0;
    uint32_t generation = 0;
    uint8_t initialized = 1;
    int32_t status = -1;

    assert(pxa_canvas_stream_open(7, 2));
    assert(stream_opens == 1);
    pxa_store_u32(ready_bytes, 7);
    pxa_store_u64(ready_bytes + 4, UINT64_C(0x100000001));
    event = (pxa_canvas_event_t){PXA_UI_SERVICE,
                                 PXA_UI_CANVAS_STREAM_READY, 0,
                                 ready_bytes, sizeof(ready_bytes)};
    assert(pxa_canvas_parse_stream_ready(&event, &request, &handle, &status));
    assert(request == 7 && handle == UINT64_C(0x100000001) && status == 0);

    pxa_canvas_begin(&frame, frame_bytes, sizeof(frame_bytes));
    assert(pxa_canvas_rect(&frame, 2, 3, 4, 5, 0xff0000, 0));
    assert(pxa_canvas_present_stream(handle, 2, &frame, &generation,
                                     &initialized, NULL, 0,
                                     packet, sizeof(packet)));
    assert(stream_writes == 1 && begins == 1 && presents == 1 &&
           generation == 1);

    assert(pxa_canvas_present_regions(2, &frame, &generation, &initialized,
                                      0, NULL, 0, packet, sizeof(packet),
                                      NULL, 0));
    assert(writes > 1 && begins == 2 && presents == 2);
    assert(copied_size == frame.length &&
           memcmp(copied, frame.data, frame.length) == 0);

    pxa_store_u32(pointer_bytes, 1);
    pxa_store_u32(pointer_bytes + 4, 2);
    pxa_store_u32(pointer_bytes + 8, generation);
    pxa_store_u16(pointer_bytes + 12, PXA_UI_EVENT_POINTER_KIND);
    pxa_store_u64(pointer_bytes + 16, 123);
    pointer_bytes[24] = 3;
    pointer_bytes[25] = 1;
    pxa_store_u32(pointer_bytes + 28, 41);
    pxa_store_u32(pointer_bytes + 32, 42);
    event = (pxa_canvas_event_t){PXA_UI_SERVICE, PXA_UI_EVENT, 0,
                                 pointer_bytes, sizeof(pointer_bytes)};
    assert(pxa_canvas_parse_pointer(&event, 2, &pointer));
    assert(pointer.pointer_id == 3 && pointer.x == 41 && pointer.y == 42 &&
           pointer.timestamp_us == 123);
    event.token = UINT64_C(0x100000000);
    assert(!pxa_canvas_parse_pointer(&event, 2, &pointer));
    return 0;
}

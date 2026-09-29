#undef NDEBUG
#include "pxa_asset_scene.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t submitted[128], cancelled[128], closed[128];
static unsigned submits, cancels, closes, binds, reject_at;
static int32_t bind_error, close_error;
int32_t pxa_submit(const uint8_t *packet, uint32_t bytes) {
    pxa_event_t message; assert(pxa_parse_event(packet, bytes, &message));
    if (message.service == PXA_ASSETS_SERVICE) {
        assert(message.opcode == PXA_ASSETS_LOAD && submits < 128);
        if (reject_at && submits + 1 == reject_at) return -7;
        submitted[submits++] = message.token;
    } else {
        assert(message.service == PXA_CORE_SERVICE);
        uint64_t id = pxa_load_u64(message.payload);
        if (message.opcode == PXA_CORE_CANCEL_REQUEST) {
            assert(cancels < 128); cancelled[cancels++] = id;
            return -5; // cancel may lose to a success already in the mailbox
        }
        assert(message.opcode == PXA_CORE_CLOSE_HANDLE);
        if (close_error) return close_error;
        assert(closes < 128); closed[closes++] = id;
    }
    return 0;
}
int32_t pxa_io(uint64_t context, uint32_t op, uint8_t *packet, uint32_t bytes) {
    assert(context == 77 && op == PXA_GAME_RENDER_IO_BIND_ASSETS);
    assert(bytes == 4u + pxa_load_u16(packet) * 12u);
    if (bind_error) return bind_error;
    ++binds; return (int32_t)bytes;
}
static void result(pxa_asset_scene_t *scene, uint64_t token, int32_t status, uint64_t handle, uint8_t kind) {
    uint8_t bytes[32] = {0};
    pxa_store_u32(bytes, (uint32_t)status);
    pxa_store_u64(bytes + 4, handle); bytes[12] = kind;
    pxa_event_t event = {PXA_ASSETS_SERVICE, PXA_ASSETS_LOAD, token, bytes, status ? 4 : 32};
    assert(pxa_asset_scene_on_event(scene, &event));
    assert(!pxa_asset_scene_on_event(scene, &event)); // no duplicate close/advance
}
int main(void) {
    pxa_asset_scene_t scene = {0};
    const pxa_asset_scene_item_t items[] = {{"assets/a.pxr",1,0}, {"assets/b.pxr",1,1}, {"assets/p.pxr",2,0}};
    pxa_game_render_binding_t storage[3] = {0};
    const uint64_t base = UINT64_C(0xabcdef0100000000);
    assert(pxa_asset_scene_begin(&scene,items,storage,3,UINT64_MAX,2) == -1 && !submits);
    pxa_asset_scene_item_t invalid[3]; memcpy(invalid,items,sizeof(items)); invalid[1].slot = 0;
    assert(pxa_asset_scene_begin(&scene,invalid,storage,3,base,2) == -1 && !submits);
    assert(!pxa_asset_scene_begin(&scene,items,storage,3,base,2));
    assert(submits == 2 && submitted[0] == base && submitted[1] == base+1);
    assert(pxa_asset_scene_bind(&scene,77) == -2 && !binds);
    result(&scene,base+1,0,101,1); assert(submits == 3 && submitted[2] == base+2);
    result(&scene,base,0,100,1);
    result(&scene,base+2,0,102,2);
    assert(scene.state == PXA_SCENE_READY && !scene.status);
    bind_error = -9; assert(pxa_asset_scene_bind(&scene,77) == -9);
    assert(!closes && storage[0].handle == 100 && storage[1].handle == 101);
    bind_error = 0; assert(pxa_asset_scene_bind(&scene,77) == 40 && binds == 1);
    assert(!pxa_asset_scene_release(&scene) && closes == 3 && !cancels);
    assert(!storage[0].handle && !storage[1].handle && !storage[2].handle);

    // Cancellation loses to queued success; restart is forbidden until drained.
    assert(!pxa_asset_scene_begin(&scene,items,storage,3,base+10,1));
    assert(!pxa_asset_scene_release(&scene) && cancels == 1 && cancelled[0] == base+10);
    assert(pxa_asset_scene_begin(&scene,items,storage,3,base+20,1) == -6);
    result(&scene,base+10,0,110,1);
    assert(scene.state == PXA_SCENE_CANCELLED && closed[closes-1] == 110 && submits == 4);

    // An immediate rejection has no completion; earlier accepted requests drain.
    reject_at = submits + 2;
    assert(pxa_asset_scene_begin(&scene,items,storage,3,base+20,3) == -7);
    assert(scene.state == PXA_SCENE_FAILED && pxa_asset_scene_pending(&scene) == 1);
    result(&scene,base+20,0,120,1); assert(scene.status == -7 && closed[closes-1] == 120);
    reject_at = 0;
    assert(!pxa_asset_scene_begin(&scene,items,storage,3,base+30,3));
    result(&scene,base+30,0,130,1);
    result(&scene,base+31,-14,0,0); // failure closes earlier handles and cancels the third
    assert(scene.status == -14 && closed[closes-1] == 130);
    result(&scene,base+32,-10,0,0); assert(!scene.pending && scene.status == -14);

    // Close failure cannot silently forget ownership or permit buffer reuse.
    assert(!pxa_asset_scene_begin(&scene,items,storage,1,base+40,1));
    result(&scene,base+40,0,140,1);
    close_error = -2; assert(pxa_asset_scene_release(&scene) == -2 && storage[0].handle == 140);
    assert(pxa_asset_scene_begin(&scene,items,storage,1,base+50,1) == -2);
    close_error = 0; assert(!pxa_asset_scene_release(&scene) && !storage[0].handle);

    // No hidden retry after asynchronous quota failure.
    assert(!pxa_asset_scene_begin(&scene,items,storage,1,base+50,1));
    unsigned before = submits; result(&scene,base+50,-9,0,0);
    assert(scene.state == PXA_SCENE_FAILED && scene.status == -9 && submits == before);
    puts("scene SDK: bounded window, full tokens, atomic bind, cancellation race, rejection and ownership cleanup passed");
}

#ifndef PXA_GUEST_ASSET_SCENE_H
#define PXA_GUEST_ASSET_SCENE_H

#include "pxa_assets.h"
#include "pxa_game_render.h"

#define PXA_SCENE_IDLE 0u
#define PXA_SCENE_LOADING 1u
#define PXA_SCENE_READY 2u
#define PXA_SCENE_FAILED 3u
#define PXA_SCENE_CANCELLING 4u
#define PXA_SCENE_CANCELLED 5u

typedef struct {
    const char *path;
    uint8_t kind;
    uint8_t slot;
} pxa_asset_scene_item_t;

/* Zero-initialize once. No heap, event loop, transport queue or pixel cache.
 * items/paths are borrowed until pending results drain. Binding storage is
 * zero-initialized by the caller and then exclusively managed by this helper;
 * keep it alive while attached to this scene (including after release). To
 * detach storage, first release successfully and drain, then zero the scene.
 * Reserve a unique
 * contiguous token range [first_token, first_token + count) in the app's token
 * namespace; do not reuse it for a replacement scene or another request.
 * Ordinary LOAD/close/cancel and the existing binding builder do all Host I/O.
 * max_in_flight bounds accepted requests; errors are terminal, never retried
 * invisibly. The caller decides when to retry after resources have retired. */
typedef struct {
    const pxa_asset_scene_item_t *items;
    pxa_game_render_binding_t *bindings;
    uint64_t first_token;
    uint64_t pending;
    int32_t status;
    uint8_t count;
    uint8_t submitted;
    uint8_t completed;
    uint8_t max_in_flight;
    uint8_t state;
} pxa_asset_scene_t;

static inline unsigned pxa_asset_scene_pending(const pxa_asset_scene_t *scene) {
    unsigned n = 0;
    uint64_t mask = scene ? scene->pending : 0;
    while (mask) { mask &= mask - 1; ++n; }
    return n;
}

static inline int32_t pxa_asset_scene_close_handles(pxa_asset_scene_t *scene) {
    int32_t first_error = 0;
    for (unsigned i = 0; i < scene->count; ++i) {
        if (!scene->bindings[i].handle) continue;
        int32_t status = pxa_close_handle(scene->bindings[i].handle);
        if (!status || status == -5 /* NOT_FOUND: already closed */) scene->bindings[i].handle = 0;
        else if (!first_error) first_error = status;
    }
    return first_error;
}

/* Cancels only this group's accepted requests and closes its Guest handles.
 * Render bindings/frames retain independent references: explicitly unbind or
 * replace them in the app. Keep forwarding events while pending != 0: cancel
 * can lose to an already queued success, whose handle we still have to close.
 * At app shutdown Core additionally closes all remaining component resources. */
static inline int32_t pxa_asset_scene_release(pxa_asset_scene_t *scene) {
    if (!scene) return -1;
    for (unsigned i = 0; i < scene->count; ++i)
        if (scene->pending & (UINT64_C(1) << i))
            (void)pxa_cancel(scene->first_token + i);
    int32_t status = pxa_asset_scene_close_handles(scene);
    if (status && !scene->status) scene->status = status;
    if (scene->state != PXA_SCENE_FAILED)
        scene->state = scene->pending ? PXA_SCENE_CANCELLING : PXA_SCENE_CANCELLED;
    return status;
}

static inline void pxa_asset_scene_fail(pxa_asset_scene_t *scene, int32_t status) {
    if (!scene->status) scene->status = status;
    scene->state = PXA_SCENE_FAILED;
    (void)pxa_asset_scene_release(scene);
}

static inline void pxa_asset_scene_submit_more(pxa_asset_scene_t *scene) {
    unsigned pending = pxa_asset_scene_pending(scene);
    while (scene->state == PXA_SCENE_LOADING && scene->submitted < scene->count &&
           pending < scene->max_in_flight) {
        unsigned i = scene->submitted;
        int32_t status = pxa_assets_load(scene->first_token + i,
            scene->items[i].path, scene->items[i].kind);
        if (status) { pxa_asset_scene_fail(scene, status); break; }
        scene->pending |= UINT64_C(1) << i;
        ++scene->submitted; ++pending;
    }
}

/* Atomically validates the whole description before submitting requests.
 * Binding storage has count entries; sizes are bounded by the renderer's 49
 * bindings. Host handle/byte quotas still apply even with a small window.
 * A failed begin may have accepted earlier requests: release/drain the group
 * rather than forgetting it. No new begin while accepted results are pending. */
static inline int32_t pxa_asset_scene_begin(pxa_asset_scene_t *scene,
    const pxa_asset_scene_item_t *items, pxa_game_render_binding_t *storage,
    size_t count, uint64_t first_token, unsigned max_in_flight) {
    uint8_t packet[4 + 49 * 12]; uint32_t bytes;
    if (!scene || !items || !storage || !count || count > 49 || !first_token ||
        first_token > UINT64_MAX - (count - 1) || !max_in_flight || max_in_flight > count) return -1;
    if (scene->pending || scene->state == PXA_SCENE_LOADING) return -6;
    for (unsigned i = 0; i < scene->count; ++i)
        if (scene->bindings[i].handle) return -2;
    for (size_t i = 0; i < count; ++i) {
        if (storage[i].handle) return -2;
        if (!pxa_assets_build(packet, sizeof(packet), PXA_ASSETS_LOAD,
            first_token + i, items[i].path, items[i].kind, &bytes)) return -1;
        storage[i].kind = items[i].kind; storage[i].slot = items[i].slot;
    }
    if (!pxa_game_render_build_bindings(packet, sizeof(packet), storage, count, &bytes)) return -1;
    pxa_zero(scene, sizeof(*scene));
    scene->items = items; scene->bindings = storage; scene->count = (uint8_t)count;
    scene->first_token = first_token; scene->max_in_flight = (uint8_t)max_in_flight;
    scene->state = PXA_SCENE_LOADING;
    pxa_asset_scene_submit_more(scene);
    return scene->status;
}

/* Pass events from the app's existing dispatcher. Returns 1 when owned by
 * this group (including failed/malformed results), 0 when unrelated. Inspect
 * state/status after handling: READY permits atomic binding, FAILED retains
 * the original error. Drain pending results even after failure/release. */
static inline int pxa_asset_scene_on_event(pxa_asset_scene_t *scene,
    const pxa_event_t *event) {
    pxa_asset_result_t result;
    if (!scene || !event || event->service != PXA_ASSETS_SERVICE ||
        event->opcode != PXA_ASSETS_LOAD || event->token < scene->first_token ||
        event->token - scene->first_token >= scene->count) return 0;
    unsigned i = (unsigned)(event->token - scene->first_token);
    uint64_t bit = UINT64_C(1) << i;
    if (!(scene->pending & bit)) return 0;
    scene->pending &= ~bit;
    if (!pxa_assets_parse_result(event, event->token, PXA_ASSETS_LOAD, &result)) {
        pxa_asset_scene_fail(scene, -15);
        return 1;
    }
    if (scene->state != PXA_SCENE_LOADING) {
        if (!result.status) {
            scene->bindings[i].handle = result.handle;
            int32_t status = pxa_asset_scene_close_handles(scene);
            if (status) pxa_asset_scene_fail(scene, status);
        }
        if (!scene->pending && scene->state == PXA_SCENE_CANCELLING)
            scene->state = PXA_SCENE_CANCELLED;
        return 1;
    }
    if (result.status) { pxa_asset_scene_fail(scene, result.status); return 1; }
    scene->bindings[i].handle = result.handle;
    if (result.info.kind != scene->bindings[i].kind) {
        pxa_asset_scene_fail(scene, -15); return 1;
    }
    if (++scene->completed == scene->count) scene->state = PXA_SCENE_READY;
    else pxa_asset_scene_submit_more(scene);
    return 1;
}

/* Host atomic binding: failure preserves the previous context bindings AND
 * the group's handles, so the app can retry or release. Does not close them;
 * call release after a successful bind to transfer residency to the context. */
static inline int32_t pxa_asset_scene_bind(const pxa_asset_scene_t *scene, uint64_t context) {
    if (!scene || scene->state != PXA_SCENE_READY || scene->pending) return -2;
    return pxa_game_render_bind_assets(context, scene->bindings, scene->count);
}

#endif

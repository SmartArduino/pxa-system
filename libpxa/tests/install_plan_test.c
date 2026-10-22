#include "pxa/install_plan.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pxa_volume_registry_t *registry(void **workspace) {
    pxa_volume_registry_t *r = NULL;
    size_t bytes = pxa_volume_registry_workspace_size(8);
    *workspace = malloc(bytes);
    assert(*workspace);
    assert(pxa_volume_registry_init(*workspace, bytes, 8, 100, &r) == PXA_STATUS_OK);
    return r;
}

static uint64_t mount_volume(pxa_volume_registry_t *r, unsigned id,
    unsigned device, uint64_t free_bytes, int removable, int installable) {
    pxa_volume_info_t v = {0};
    uint64_t epoch = 0;
    v.id.bytes[0] = (uint8_t)id;
    v.device_id.bytes[0] = (uint8_t)device;
    v.state = PXA_VOLUME_READY;
    v.capabilities = PXA_VOLUME_READ | PXA_VOLUME_WRITE | PXA_VOLUME_RANDOM_READ;
    if (removable) v.capabilities |= PXA_VOLUME_REMOVABLE;
    if (installable) v.capabilities |= PXA_VOLUME_INSTALLABLE;
    v.total_bytes = UINT64_C(1024) * 1024;
    v.free_bytes = free_bytes;
    v.space_known = PXA_VOLUME_FREE_KNOWN | PXA_VOLUME_TOTAL_KNOWN;
    v.allocation_unit = 4096;
    v.max_file_bytes = UINT32_MAX;
    v.reserved_bytes = 400; /* A mount cannot inject old admission leases. */
    snprintf(v.name, sizeof(v.name), "volume-%u", id);
    assert(pxa_volume_register(r, &v, &epoch) == PXA_STATUS_OK);
    return epoch;
}

static uint64_t reserved(pxa_volume_registry_t *r, uint64_t epoch) {
    pxa_volume_info_t v;
    assert(pxa_volume_get(r, epoch, &v) == PXA_STATUS_OK);
    return v.reserved_bytes;
}

static void topologies(void) {
    void *memory;
    pxa_volume_registry_t *r;
    pxa_install_plan_t plan;
    const pxa_install_requirements_t req = {100, 20, 600, 10, 30};
    uint64_t first, second;
    unsigned scenario;
    for (scenario = 0; scenario < 5; ++scenario) {
        r = registry(&memory);
        first = second = 0;
        switch (scenario) {
        case 0: /* One writable filesystem on one Flash. */
            first = mount_volume(r, 1, 1, 1000, 0, 1);
            break;
        case 1: /* Two partitions of the same physical Flash. */
            first = mount_volume(r, 1, 1, 0, 0, 1);
            second = mount_volume(r, 2, 1, 1000, 0, 1);
            break;
        case 2: /* Multiple Flash devices. */
            first = mount_volume(r, 1, 1, 0, 0, 1);
            second = mount_volume(r, 2, 2, 1000, 0, 1);
            break;
        case 3: /* Full internal storage plus an installable SD card. */
            first = mount_volume(r, 1, 1, 0, 0, 1);
            second = mount_volume(r, 2, 2, 1000, 1, 1);
            break;
        default: /* No internal storage filesystem at all. */
            second = mount_volume(r, 2, 2, 1000, 1, 1);
            break;
        }
        assert(pxa_install_plan_select(r, &req, first, &plan) == PXA_STATUS_OK);
        assert(plan.app_mount_epoch == (second ? second : first));
        assert(plan.media_mount_epoch == plan.app_mount_epoch);
        assert(plan.volume_count == 1 && plan.volumes[0].bytes == 760);
        assert(pxa_install_plan_reserve(r, &plan) == PXA_STATUS_OK);
        assert(pxa_install_plan_validate(r, &plan) == PXA_STATUS_OK);
        assert(reserved(r, plan.app_mount_epoch) == 760);
        assert(pxa_install_plan_release(r, &plan) == PXA_STATUS_OK);
        assert(pxa_install_plan_release(r, &plan) == PXA_STATUS_OK);
        free(memory);
    }
}

static void split_and_concurrent(void) {
    void *memory;
    pxa_volume_registry_t *r = registry(&memory);
    pxa_install_plan_t a, b;
    const pxa_install_requirements_t req = {100, 20, 600, 10, 30};
    uint64_t internal = mount_volume(r, 1, 1, 200, 0, 1);
    uint64_t media = mount_volume(r, 2, 2, 1000, 1, 0);
    assert(pxa_install_plan_evaluate(r, &req, internal, media, &a) == PXA_STATUS_OK);
    assert(a.volume_count == 2 && a.volumes[0].bytes == 150 && a.volumes[1].bytes == 640);
    assert(pxa_install_plan_evaluate(r, &req, internal, media, &b) == PXA_STATUS_OK);
    /* Capacity changed while the confirmation dialog was open. */
    assert(pxa_volume_update(r, media, PXA_VOLUME_READY, 1000, 300, 3) == PXA_STATUS_OK);
    assert(pxa_install_plan_reserve(r, &a) == PXA_STATUS_QUOTA_EXCEEDED);
    assert(reserved(r, internal) == 0 && reserved(r, media) == 0);
    assert(pxa_volume_update(r, media, PXA_VOLUME_READY, 1000, 1000, 3) == PXA_STATUS_OK);
    assert(pxa_install_plan_reserve(r, &a) == PXA_STATUS_OK);
    assert(pxa_install_plan_reserve(r, &a) == PXA_STATUS_BAD_STATE);
    assert(pxa_install_plan_reserve(r, &b) == PXA_STATUS_QUOTA_EXCEEDED);
    /* In-flight writes may reduce observed free bytes below the original
     * admission amount; that does not invalidate the already granted lease. */
    assert(pxa_volume_update(r, media, PXA_VOLUME_READY, 1000, 40, 3) == PXA_STATUS_OK);
    assert(pxa_install_plan_validate(r, &a) == PXA_STATUS_OK);
    assert(pxa_install_plan_release(r, &a) == PXA_STATUS_OK);
    assert(reserved(r, internal) == 0 && reserved(r, media) == 0);
    free(memory);
}

static void unplug_and_clones(void) {
    void *memory;
    pxa_volume_registry_t *r = registry(&memory);
    pxa_install_plan_t plan;
    const pxa_install_requirements_t req = {100, 0, 200, 0, 10};
    uint64_t app = mount_volume(r, 1, 1, 1000, 0, 1);
    uint64_t media = mount_volume(r, 2, 2, 1000, 1, 1), replacement, clone;
    assert(pxa_install_plan_evaluate(r, &req, app, media, &plan) == PXA_STATUS_OK);
    assert(pxa_install_plan_reserve(r, &plan) == PXA_STATUS_OK);
    assert(pxa_volume_unregister(r, media) == PXA_STATUS_OK);
    replacement = mount_volume(r, 2, 2, 1000, 1, 1);
    assert(replacement != media);
    assert(pxa_install_plan_validate(r, &plan) == PXA_STATUS_UNAVAILABLE);
    assert(pxa_install_plan_release(r, &plan) == PXA_STATUS_UNAVAILABLE);
    assert(reserved(r, app) == 0 && reserved(r, replacement) == 0);
    assert(pxa_install_plan_evaluate(r, &req, app, replacement, &plan) == PXA_STATUS_OK);
    assert(pxa_install_plan_reserve(r, &plan) == PXA_STATUS_OK);
    clone = mount_volume(r, 2, 3, 1000, 1, 1);
    assert(pxa_install_plan_validate(r, &plan) == PXA_STATUS_UNAVAILABLE);
    assert(pxa_install_plan_release(r, &plan) == PXA_STATUS_OK);
    assert(reserved(r, clone) == 0);
    free(memory);
}

static void limits_and_preferences(void) {
    void *memory;
    pxa_volume_registry_t *r = registry(&memory);
    pxa_install_plan_t plan;
    pxa_install_requirements_t req = {100, 0, 20, 0, 10};
    pxa_volume_info_t v;
    uint64_t bytes = 0;
    uint64_t sd = mount_volume(r, 2, 2, 1000, 1, 1);
    uint64_t app = mount_volume(r, 1, 1, 1000, 0, 1);
    assert(pxa_install_plan_select(r, &req, 0, &plan) == PXA_STATUS_OK);
    assert(plan.app_mount_epoch == app);
    assert(pxa_install_plan_select(r, &req, sd, &plan) == PXA_STATUS_OK);
    assert(plan.app_mount_epoch == sd);
    req.app_temporary_bytes = UINT64_MAX;
    assert(pxa_install_plan_evaluate(r, &req, app, sd, &plan) == PXA_STATUS_LIMIT_EXCEEDED);
    assert(plan.volume_count == 0 && plan.reserved == 0);
    req.app_temporary_bytes = 0;
    assert(pxa_volume_get(r, sd, &v) == PXA_STATUS_OK);
    assert(pxa_install_file_allocation(&v, 4097, &bytes) == PXA_STATUS_OK && bytes == 8192);
    assert(pxa_install_file_allocation(&v, UINT64_C(1) << 32, &bytes) == PXA_STATUS_LIMIT_EXCEEDED);
    assert(bytes == 0);
    v.max_file_bytes = 0;
    assert(pxa_install_file_allocation(&v, UINT64_MAX, &bytes) == PXA_STATUS_LIMIT_EXCEEDED);
    assert(pxa_volume_update(r, sd, PXA_VOLUME_READY, 1000, 1000,
                            PXA_VOLUME_TOTAL_KNOWN) == PXA_STATUS_OK);
    assert(pxa_install_plan_evaluate(r, &req, sd, 0, &plan) == PXA_STATUS_UNAVAILABLE);
    assert(pxa_volume_update(r, app, PXA_VOLUME_OFFLINE, 1000, 1000, 3) == PXA_STATUS_OK);
    assert(pxa_install_plan_select(r, &req, 0, &plan) == PXA_STATUS_UNAVAILABLE);
    free(memory);
}

int main(void) {
    topologies();
    split_and_concurrent();
    unplug_and_clones();
    limits_and_preferences();
    puts("Install planning: five storage topologies, split resources, admission, unplug, clone and overflow checks passed");
    return 0;
}

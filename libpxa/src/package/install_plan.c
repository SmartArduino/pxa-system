#include "pxa/install_plan.h"
#include <string.h>

static int add(uint64_t *value, uint64_t bytes) {
    if (bytes > UINT64_MAX - *value) return 0;
    *value += bytes;
    return 1;
}

pxa_status_t pxa_install_file_allocation(const pxa_volume_info_t *volume,
    uint64_t logical, uint64_t *allocated) {
    uint64_t extra;
    if (!volume || !allocated) return PXA_STATUS_INVALID_ARGUMENT;
    *allocated = 0;
    if (volume->max_file_bytes && logical > volume->max_file_bytes)
        return PXA_STATUS_LIMIT_EXCEEDED;
    if (volume->allocation_unit) {
        extra = logical % volume->allocation_unit;
        if (extra && !add(&logical, volume->allocation_unit - extra))
            return PXA_STATUS_LIMIT_EXCEEDED;
    }
    *allocated = logical;
    return PXA_STATUS_OK;
}

static pxa_status_t destination(const pxa_volume_registry_t *registry,
    uint64_t epoch, int application, pxa_volume_info_t *volume) {
    const uint64_t common = PXA_VOLUME_READ | PXA_VOLUME_WRITE |
        PXA_VOLUME_RANDOM_READ;
    pxa_status_t status = pxa_volume_get(registry, epoch, volume);
    if (status != PXA_STATUS_OK) return status;
    if (volume->ambiguous || volume->state != PXA_VOLUME_READY)
        return PXA_STATUS_UNAVAILABLE;
    if ((volume->capabilities & common) != common ||
        (application && !(volume->capabilities & PXA_VOLUME_INSTALLABLE)))
        return PXA_STATUS_UNSUPPORTED;
    if (!(volume->space_known & PXA_VOLUME_FREE_KNOWN))
        return PXA_STATUS_UNAVAILABLE;
    return PXA_STATUS_OK;
}

static pxa_status_t budget(const pxa_volume_info_t *volume, uint64_t bytes) {
    if (volume->reserved_bytes > volume->free_bytes ||
        bytes > volume->free_bytes - volume->reserved_bytes)
        return PXA_STATUS_QUOTA_EXCEEDED;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_install_plan_evaluate(const pxa_volume_registry_t *registry,
    const pxa_install_requirements_t *required, uint64_t app_epoch,
    uint64_t media_epoch, pxa_install_plan_t *output) {
    pxa_install_plan_t plan = {0};
    pxa_volume_info_t app, media;
    uint64_t app_bytes, media_bytes;
    pxa_status_t status;
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (!registry || !required || !app_epoch || !required->app_bytes)
        return PXA_STATUS_INVALID_ARGUMENT;
    status = destination(registry, app_epoch, 1, &app);
    if (status != PXA_STATUS_OK) return status;
    app_bytes = required->app_bytes;
    media_bytes = required->media_bytes;
    if (!add(&app_bytes, required->app_temporary_bytes) ||
        !add(&media_bytes, required->media_temporary_bytes) ||
        !add(&app_bytes, required->volume_headroom_bytes))
        return PXA_STATUS_LIMIT_EXCEEDED;
    if (!media_epoch || !media_bytes) media_epoch = app_epoch;
    if (media_epoch == app_epoch) {
        if (!add(&app_bytes, media_bytes)) return PXA_STATUS_LIMIT_EXCEEDED;
    } else {
        status = destination(registry, media_epoch, 0, &media);
        if (status != PXA_STATUS_OK) return status;
        if (!add(&media_bytes, required->volume_headroom_bytes))
            return PXA_STATUS_LIMIT_EXCEEDED;
        status = budget(&media, media_bytes);
        if (status != PXA_STATUS_OK) return status;
        plan.volumes[1] = (pxa_install_reservation_t){media.id, media_epoch, media_bytes};
        plan.volume_count = 1;
    }
    status = budget(&app, app_bytes);
    if (status != PXA_STATUS_OK) return status;
    plan.volumes[0] = (pxa_install_reservation_t){app.id, app_epoch, app_bytes};
    ++plan.volume_count;
    plan.app_mount_epoch = app_epoch;
    plan.media_mount_epoch = media_epoch;
    *output = plan;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_install_plan_select(const pxa_volume_registry_t *registry,
    const pxa_install_requirements_t *required, uint64_t preferred,
    pxa_install_plan_t *output) {
    pxa_volume_info_t volume;
    pxa_install_plan_t candidate;
    uint64_t revision;
    uint16_t cursor = 0;
    pxa_status_t status, result = PXA_STATUS_UNAVAILABLE;
    int selected = 0, removable = 1;
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (!registry || !required || !required->app_bytes)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (preferred && pxa_install_plan_evaluate(registry, required,
            preferred, 0, output) == PXA_STATUS_OK) return PXA_STATUS_OK;
    revision = pxa_volume_revision(registry);
    while ((status = pxa_volume_next(registry, revision, &cursor, &volume)) == PXA_STATUS_OK) {
        int next_removable = !!(volume.capabilities & PXA_VOLUME_REMOVABLE);
        status = pxa_install_plan_evaluate(registry, required,
            volume.mount_epoch, 0, &candidate);
        if (status == PXA_STATUS_OK) {
            if (!selected || (removable && !next_removable)) {
                *output = candidate;
                removable = next_removable;
                selected = 1;
            }
        } else if (status == PXA_STATUS_LIMIT_EXCEEDED) {
            return status;
        } else if (status == PXA_STATUS_QUOTA_EXCEEDED) result = status;
    }
    if (status != PXA_STATUS_NOT_FOUND) return status;
    return selected ? PXA_STATUS_OK : result;
}

static pxa_status_t check_plan(const pxa_volume_registry_t *registry,
    const pxa_install_plan_t *plan, int admission) {
    uint8_t i;
    pxa_volume_info_t volume;
    pxa_status_t status;
    if (!registry || !plan || !plan->volume_count || plan->volume_count > 2 ||
        plan->volumes[0].mount_epoch != plan->app_mount_epoch ||
        !plan->app_mount_epoch ||
        (plan->volume_count == 2 &&
         (plan->volumes[1].mount_epoch != plan->media_mount_epoch ||
          plan->app_mount_epoch == plan->media_mount_epoch)) ||
        (plan->volume_count == 1 && plan->app_mount_epoch != plan->media_mount_epoch))
        return PXA_STATUS_INVALID_ARGUMENT;
    for (i = 0; i < plan->volume_count; ++i) {
        const pxa_install_reservation_t *lease = &plan->volumes[i];
        if (!lease->bytes) return PXA_STATUS_INVALID_ARGUMENT;
        status = destination(registry, lease->mount_epoch, i == 0, &volume);
        if (status != PXA_STATUS_OK) return status;
        if (memcmp(&volume.id, &lease->id, sizeof(volume.id)))
            return PXA_STATUS_UNAVAILABLE;
        if (admission) {
            status = budget(&volume, lease->bytes);
            if (status != PXA_STATUS_OK) return status;
        } else if (volume.reserved_bytes < lease->bytes) return PXA_STATUS_BAD_STATE;
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_install_plan_reserve(pxa_volume_registry_t *registry,
    pxa_install_plan_t *plan) {
    uint8_t i;
    pxa_status_t status;
    if (!plan) return PXA_STATUS_INVALID_ARGUMENT;
    if (plan->reserved) return PXA_STATUS_BAD_STATE;
    status = check_plan(registry, plan, 1);
    if (status != PXA_STATUS_OK) return status;
    for (i = 0; i < plan->volume_count; ++i) {
        status = pxa_volume_reserve(registry, plan->volumes[i].mount_epoch,
            plan->volumes[i].bytes);
        if (status != PXA_STATUS_OK) {
            while (i) {
                --i;
                (void)pxa_volume_release(registry, plan->volumes[i].mount_epoch,
                    plan->volumes[i].bytes);
            }
            return status;
        }
    }
    plan->reserved = 1;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_install_plan_validate(const pxa_volume_registry_t *registry,
    const pxa_install_plan_t *plan) {
    if (!plan || !plan->reserved) return PXA_STATUS_BAD_STATE;
    return check_plan(registry, plan, 0);
}

pxa_status_t pxa_install_plan_release(pxa_volume_registry_t *registry,
    pxa_install_plan_t *plan) {
    uint8_t i;
    pxa_status_t status, result = PXA_STATUS_OK;
    if (!registry || !plan || plan->volume_count > 2) return PXA_STATUS_INVALID_ARGUMENT;
    if (!plan->reserved) return PXA_STATUS_OK;
    for (i = 0; i < plan->volume_count; ++i) {
        status = pxa_volume_release(registry, plan->volumes[i].mount_epoch,
            plan->volumes[i].bytes);
        if (status != PXA_STATUS_OK) result = status;
    }
    plan->reserved = 0;
    return result;
}

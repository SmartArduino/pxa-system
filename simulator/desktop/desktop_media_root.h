#ifndef PXSYS_DESKTOP_MEDIA_ROOT_H
#define PXSYS_DESKTOP_MEDIA_ROOT_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "pxa/posix/pxa_scoped_fs.h"
#include "pxa/openssl/pxa_openssl.h"

/* A read-only, app-scoped removable-media stand-in. Keep private files on
 * their existing backend. The POSIX scoped adapter rejects links/traversal. */
typedef struct {
    pxa_fs_roots_backend_t api, base;
    pxa_scoped_fs_t *fs;
    pxa_scoped_fs_mount_t mount;
    pxa_scoped_fs_grant_t grant;
    void *volume_memory, *fs_memory;
    char *path;
} desktop_media_root_t;

static pxa_status_t desktop_media_open(void *context, uint8_t kind,
    const uint8_t id[16], void **directory, uint32_t *rights) {
    desktop_media_root_t *r = context;
    (void)id;
    if (kind != PXA_FS_ROOT_APP_MEDIA) return PXA_STATUS_UNSUPPORTED;
    return r->base.open_root(r->base.context, PXA_FS_ROOT_GRANT,
                            r->grant.id, directory, rights);
}
static pxa_status_t desktop_media_open_at(void *context, void *directory,
    pxa_bytes_t path, uint32_t flags, void **file, pxa_fs_entry_t *info,
    uint64_t *features) {
    desktop_media_root_t *r = context;
    return r->base.open_at(r->base.context, directory, path, flags,
                           file, info, features);
}
static pxa_status_t desktop_media_stat_at(void *context, void *directory,
    pxa_bytes_t path, pxa_fs_entry_t *info) {
    desktop_media_root_t *r = context;
    return r->base.stat_at(r->base.context, directory, path, info);
}
static pxa_status_t desktop_media_query(void *context, void *directory,
    pxa_fs_root_info_t *info) {
    desktop_media_root_t *r = context;
    return r->base.query_root(r->base.context, directory, info);
}
static pxa_status_t desktop_media_path(void *context,char *out,size_t capacity,size_t *size) {
    desktop_media_root_t *r=context;
    if(!r || !r->path || !out || !size)return PXA_STATUS_INVALID_ARGUMENT;
    size_t n=strlen(r->path);if(n>capacity || n>PXA_FS_DISPLAY_PATH_MAX)return PXA_STATUS_RESOURCE_LIMIT;
    memcpy(out,r->path,n);*size=n;return PXA_STATUS_OK;
}
static void desktop_media_deinit(desktop_media_root_t *r) {
    if (r->fs) pxa_scoped_fs_deinit(r->fs);
    free(r->fs_memory); free(r->volume_memory); free(r->path);
    memset(r, 0, sizeof(*r));
}
static int desktop_media_directory(const char *path) {
    struct stat s;
    return (mkdir(path, 0700) == 0 || errno == EEXIST) &&
           lstat(path, &s) == 0 && S_ISDIR(s.st_mode);
}
static int desktop_media_init(desktop_media_root_t *r, const char *state,
    const pxa_package_manifest_t *manifest) {
    static const char hex[] = "0123456789abcdef";
    static const uint8_t domain[] = "PXA-APPKEY-1";
    uint8_t input[sizeof(domain)-1 + 32 + 2 + 64], digest[32];
    const uint8_t *publisher = manifest->publisher_key_id;
    pxa_package_publisher_lineage_t lineage;
    pxa_volume_registry_t *volumes;
    pxa_volume_info_t volume = {0};
    pxa_scoped_fs_config_t config = {0};
    size_t n = sizeof(domain)-1, prefix;
    if (!state || !publisher || manifest->app_id.size > 64) return 0;
    if (manifest->publisher_lineage.size) {
        if (pxa_package_publisher_lineage_parse(manifest,
                                               &lineage) != PXA_STATUS_OK) return 0;
        publisher = lineage.root_key_id;
    }
    memcpy(input, domain, n); memcpy(input+n, publisher, 32); n += 32;
    input[n++] = (uint8_t)manifest->app_id.size;
    input[n++] = (uint8_t)(manifest->app_id.size >> 8);
    memcpy(input+n, manifest->app_id.data, manifest->app_id.size);
    if (pxa_openssl_sha256(input, n+manifest->app_id.size, digest)) return 0;
    r->path = malloc(strlen(state)+sizeof("/media/")+64);
    if (!r->path) return 0;
    sprintf(r->path, "%s/media", state);
    if (!desktop_media_directory(r->path)) return 0;
    prefix = strlen(r->path); r->path[prefix++] = '/';
    for (size_t i=0; i<32; ++i) {
        r->path[prefix+2*i] = hex[digest[i] >> 4];
        r->path[prefix+2*i+1] = hex[digest[i] & 15];
    }
    r->path[prefix+64] = 0;
    if (!desktop_media_directory(r->path)) return 0;
    r->volume_memory = malloc(pxa_volume_registry_workspace_size(1));
    if (!r->volume_memory || pxa_volume_registry_init(r->volume_memory,
        pxa_volume_registry_workspace_size(1), 1, 1, &volumes)) return 0;
    memcpy(volume.id.bytes, digest, sizeof(volume.id.bytes));
    volume.device_id = volume.id; volume.state = PXA_VOLUME_READY;
    volume.identity_strength = PXA_VOLUME_ID_MEDIA_BOUND;
    volume.capabilities = PXA_VOLUME_READ | PXA_VOLUME_RANDOM_READ;
    strcpy(volume.name, "Simulator app media");
    if (pxa_volume_register(volumes, &volume, &r->mount.mount_epoch)) return 0;
    r->mount.host_path = r->path;
    r->grant.id[0] = 1; r->grant.volume_id = volume.id;
    r->grant.revision = 1; r->grant.rights = PXA_FS_RIGHT_READ | PXA_FS_RIGHT_ENUMERATE;
    r->grant.active = 1;
    config.struct_size = sizeof(config); config.volumes = volumes;
    config.mounts = &r->mount; config.mount_count = 1;
    config.grants = &r->grant; config.grant_count = 1; config.max_open_resources = 16;
    r->fs_memory = malloc(pxa_scoped_fs_workspace_size(&config));
    if (!r->fs_memory || pxa_scoped_fs_init(r->fs_memory,
        pxa_scoped_fs_workspace_size(&config), &config, &r->fs, &r->base)) return 0;
    r->api = r->base; r->api.context = r;
    r->api.open_root = desktop_media_open;
    r->api.open_at = desktop_media_open_at;
    r->api.stat_at = desktop_media_stat_at;
    r->api.query_root = desktop_media_query;
    r->api.app_media_path=desktop_media_path;
    r->api.features = PXA_FS_FEATURE_APP_MEDIA_ROOT|PXA_FS_FEATURE_APP_MEDIA_PATH;
    fprintf(stderr, "PXA SIM app-media-root=%s\n", r->path);
    return 1;
}
#endif

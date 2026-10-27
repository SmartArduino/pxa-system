#define _POSIX_C_SOURCE 200809L
#include "../desktop_media_root.h"
#include <assert.h>
#include <unistd.h>

static pxa_bytes_t bytes(const char *s) {
    return (pxa_bytes_t){(const uint8_t *)s, strlen(s)};
}
int main(void) {
    char state[] = "/tmp/pxa-desktop-media-XXXXXX", path[512], data[8];
    uint8_t publisher[32] = {1}, ignored[16] = {0};
    pxa_package_manifest_t manifest = {0};
    desktop_media_root_t a = {0}, b = {0};
    void *directory, *file; uint32_t rights; uint64_t features; size_t size;
    pxa_fs_entry_t entry;
    assert(mkdtemp(state));
    manifest.publisher_key_id = publisher; manifest.app_id = bytes("pxa-one");
    assert(desktop_media_init(&a, state, &manifest,NULL));
    manifest.app_id = bytes("pxa-two");
    assert(desktop_media_init(&b, state, &manifest,NULL));
    assert(!(a.api.features&PXA_FS_FEATURE_FILE_IMPORT));
    assert(!a.api.request_import && !a.api.cancel_import);
    assert(strcmp(a.path, b.path));
    char label[256]; size_t label_size = 0;
    assert(a.api.features & PXA_FS_FEATURE_APP_MEDIA_PATH);
    assert(!a.api.app_media_path(a.api.context, label, sizeof(label), &label_size));
    assert(label_size == strlen(a.path) && !memcmp(label, a.path, label_size));
    assert(a.api.app_media_path(a.api.context, label, 2, &label_size) == PXA_STATUS_RESOURCE_LIMIT);
    snprintf(path, sizeof(path), "%s/level.pkd", a.path);
    FILE *output = fopen(path, "wb"); assert(output);
    assert(fwrite("LEVEL", 1, 5, output) == 5); assert(!fclose(output));
    assert(!a.api.open_root(a.api.context, PXA_FS_ROOT_APP_MEDIA,
                           ignored, &directory, &rights));
    assert(rights == (PXA_FS_RIGHT_READ | PXA_FS_RIGHT_ENUMERATE));
    assert(!a.api.open_at(a.api.context, directory, bytes("level.pkd"),
                         PXA_FS_OPEN_READ, &file, &entry, &features));
    assert(entry.size == 5 && (features & PXA_FS_FILE_FEATURE_READ_AT));
    assert(!a.api.read_at(a.api.files.context, file, 1, (uint8_t *)data, 3, &size));
    assert(size == 3 && !memcmp(data, "EVE", 3));
    a.api.files.close(a.api.files.context, file, PXA_FS_KIND_REGULAR);
    assert(a.api.open_at(a.api.context, directory, bytes("../outside"),
        PXA_FS_OPEN_READ, &file, &entry, &features) == PXA_STATUS_INVALID_ARGUMENT);
    assert(a.api.open_at(a.api.context, directory, bytes("level.pkd"),
        PXA_FS_OPEN_WRITE, &file, &entry, &features) == PXA_STATUS_DENIED);
    a.api.files.close(a.api.files.context, directory, PXA_FS_KIND_DIRECTORY);
    assert(!b.api.open_root(b.api.context, PXA_FS_ROOT_APP_MEDIA,
                           ignored, &directory, &rights));
    assert(b.api.open_at(b.api.context, directory, bytes("level.pkd"),
        PXA_FS_OPEN_READ, &file, &entry, &features) == PXA_STATUS_NOT_FOUND);
    b.api.files.close(b.api.files.context, directory, PXA_FS_KIND_DIRECTORY);
    assert(a.api.open_root(a.api.context, PXA_FS_ROOT_GRANT,
                          ignored, &directory, &rights) == PXA_STATUS_UNSUPPORTED);
    assert(!unlink(path)); assert(!rmdir(a.path)); assert(!rmdir(b.path));
    desktop_media_deinit(&a); desktop_media_deinit(&b);
    snprintf(path, sizeof(path), "%s/media", state);
    assert(!rmdir(path)); assert(!rmdir(state));
    puts("desktop media: per-app isolation, read-at, readonly and traversal checks passed");
    return 0;
}

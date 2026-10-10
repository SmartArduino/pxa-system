#define _POSIX_C_SOURCE 200809L
#include "package_store.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static bool safe_id(const char *id) {
    size_t size;
    if (id == NULL || strncmp(id, "pxa-", 4) != 0 ||
        (size = strlen(id)) <= 4 || size > 64) return false;
    for (size_t i = 4; i < size; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') ||
              (id[i] >= '0' && id[i] <= '9') ||
              id[i] == '.' || id[i] == '-' || id[i] == '_')) return false;
    return true;
}
static int open_directory(const char *path) {
    return path == NULL ? -1 : open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
}
static int open_child(int parent, const char *name) {
    return openat(parent, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
}
static int package_directory(const char *packages, const char *id) {
    if (!safe_id(id)) return -1;
    int parent = open_directory(packages);
    if (parent < 0) return -1;
    int result = open_child(parent, id);
    close(parent);
    return result;
}
static bool manifest_exists(int package) {
    int current = open_child(package, "current");
    int selected = current >= 0 ? current : package;
    struct stat st;
    bool exists = fstatat(selected, "manifest.pxm", &st, AT_SYMLINK_NOFOLLOW) == 0 &&
                  S_ISREG(st.st_mode);
    if (current >= 0) close(current);
    return exists;
}
bool pxsys_desktop_package_installed(const char *packages, const char *id) {
    int package = package_directory(packages, id);
    if (package < 0) return false;
    bool result = manifest_exists(package);
    close(package);
    return result;
}
bool pxsys_desktop_package_enabled(const char *packages, const char *id) {
    int package = package_directory(packages, id);
    if (package < 0) return false;
    struct stat st;
    bool result = fstatat(package, ".disabled", &st, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
    close(package);
    return result;
}
static void data_name(char name[131], const char *id, const uint8_t publisher[32]) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        name[i * 2] = hex[publisher[i] >> 4];
        name[i * 2 + 1] = hex[publisher[i] & 15];
    }
    name[64] = '-';
    strcpy(name + 65, id);
}
bool pxsys_desktop_package_has_data(const char *state, const char *id,
                                   const uint8_t publisher[32]) {
    if (!safe_id(id) || publisher == NULL) return false;
    int root = open_directory(state);
    if (root < 0) return false;
    char name[131]; data_name(name, id, publisher);
    const char *parents[] = {"app-data", "private-files"};
    bool result = false;
    for (size_t i = 0; i < 2; ++i) {
        int parent = open_child(root, parents[i]);
        if (parent < 0) continue;
        struct stat st;
        if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) == 0) result = true;
        close(parent);
    }
    close(root);
    return result;
}
/* Walk through directory descriptors; symlinks are unlinked, never followed.
 * Bound nesting even if a test package contains a malicious directory tree. */
static int remove_at(int parent, const char *name, unsigned depth) {
    struct stat st;
    if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode)) return unlinkat(parent, name, 0);
    if (depth >= 64) return -1;
    int fd = open_child(parent, name);
    if (fd < 0) return -1;
    DIR *dir = fdopendir(fd);
    if (dir == NULL) { close(fd); return -1; }
    int result = 0;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (remove_at(fd, entry->d_name, depth + 1) != 0) { result = -1; break; }
        errno = 0;
    }
    if (errno != 0) result = -1;
    closedir(dir);
    if (result == 0) result = unlinkat(parent, name, AT_REMOVEDIR);
    return result;
}
/* Hide the complete root atomically before reclaiming files. A stopped app or
 * a restarted catalog cannot discover a half-removed package. Retry cleans a
 * previous interrupted reclamation without touching a new installation. */
static int detach_and_remove(int parent, const char *name) {
    char trash[160];
    if (snprintf(trash, sizeof(trash), ".removed-%s", name) >= (int)sizeof(trash)) return -1;
    if (remove_at(parent, trash, 0) != 0) return -1;
    if (renameat(parent, name, parent, trash) != 0)
        return errno == ENOENT ? 0 : -1;
    if (fsync(parent) != 0) return -1;
    if (remove_at(parent, trash, 0) != 0) return -1;
    return fsync(parent);
}
pxa_status_t pxsys_desktop_package_manage(const char *packages, const char *state,
    const char *id, const uint8_t publisher[32], pxsys_desktop_package_action_t action) {
    if (!safe_id(id) || publisher == NULL || action < PXSYS_DESKTOP_PACKAGE_ENABLE || action > PXSYS_DESKTOP_PACKAGE_UNINSTALL)
        return PXA_STATUS_INVALID_ARGUMENT;
    int parent = open_directory(packages), package = -1, lock = -1, root = -1;
    pxa_status_t result = PXA_STATUS_INTERNAL;
    if (parent < 0) return PXA_STATUS_NOT_FOUND;
    char name[131], lock_name[72];
    snprintf(lock_name, sizeof(lock_name), "%s.lock", id);
    lock = openat(parent, lock_name, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0) goto done;
    /* Same per-app lock as the installer. Never stall the LVGL owner. */
    if (flock(lock, LOCK_EX | LOCK_NB) != 0) { result = PXA_STATUS_BUSY; goto done; }
    package = open_child(parent, id);
    if (package < 0 || !manifest_exists(package)) { result = PXA_STATUS_NOT_FOUND; goto done; }
    if (action == PXSYS_DESKTOP_PACKAGE_ENABLE) {
        if (unlinkat(package, ".disabled", 0) != 0 && errno != ENOENT) goto done;
        result = fsync(package) == 0 ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
        goto done;
    }
    if (action == PXSYS_DESKTOP_PACKAGE_DISABLE) {
        int flag = openat(package, ".disabled", O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (flag < 0) goto done;
        int status = fsync(flag); close(flag);
        if (status != 0) goto done;
        result = fsync(package) == 0 ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
        goto done;
    }
    root = open_directory(state);
    if (root < 0) goto done;
    data_name(name, id, publisher);
    const char *parents[] = {"app-data", "private-files"};
    for (size_t i = 0; i < 2; ++i) {
        int data = open_child(root, parents[i]);
        if (data < 0) { if (errno == ENOENT) continue; goto done; }
        int status = detach_and_remove(data, name);
        close(data);
        if (status != 0) goto done;
    }
    if (action == PXSYS_DESKTOP_PACKAGE_UNINSTALL) {
        close(package); package = -1;
        if (detach_and_remove(parent, id) != 0) goto done;
        snprintf(name, sizeof(name), ".session-%s", id);
        if (detach_and_remove(parent, name) != 0) goto done;
    }
    result = PXA_STATUS_OK;
done:
    if (root >= 0) close(root);
    if (package >= 0) close(package);
    if (lock >= 0) close(lock);
    close(parent);
    return result;
}

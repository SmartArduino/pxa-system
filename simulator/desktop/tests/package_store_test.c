#define _POSIX_C_SOURCE 200809L
#include "../package_store.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static void directory(const char *path) { assert(mkdir(path, 0700) == 0); }
static void file(const char *path) { int fd = open(path, O_WRONLY | O_CREAT, 0600); assert(fd >= 0); assert(write(fd, "data", 4) == 4); close(fd); }
static bool exists(const char *path) { return access(path, F_OK) == 0; }
int main(void) {
    char state[] = "/tmp/pxsys-package-store-XXXXXX";
    assert(mkdtemp(state)); assert(chdir(state) == 0);
    directory("packages"); directory("packages/pxa-one"); directory("packages/pxa-one/current");
    file("packages/pxa-one/current/manifest.pxm");
    directory("packages/pxa-two"); file("packages/pxa-two/manifest.pxm");
    uint8_t publisher[32] = {0};
    const char *name = "0000000000000000000000000000000000000000000000000000000000000000-pxa-one";
    char data[256], private[256], nested[300];
    directory("app-data"); directory("private-files");
    snprintf(data, sizeof(data), "app-data/%s", name); directory(data);
    snprintf(private, sizeof(private), "private-files/%s", name); directory(private);
    snprintf(nested, sizeof(nested), "%s/book.txt", private); file(nested);
    file("outside.txt"); snprintf(nested, sizeof(nested), "%s/link", private);
    assert(symlink("../../outside.txt", nested) == 0);
    directory("private-files/another-app"); file("private-files/another-app/keep.txt");
    assert(pxsys_desktop_package_installed("packages", "pxa-one"));
    assert(pxsys_desktop_package_installed("packages", "pxa-two"));
    assert(pxsys_desktop_package_enabled("packages", "pxa-one"));
    assert(pxsys_desktop_package_has_data(state, "pxa-one", publisher));
    assert(!pxsys_desktop_package_installed("packages", "../pxa-one"));
    assert(pxsys_desktop_package_manage("packages", state, "../pxa-one", publisher, PXSYS_DESKTOP_PACKAGE_UNINSTALL) == PXA_STATUS_INVALID_ARGUMENT);
    int lock = open("packages/pxa-one.lock", O_RDWR | O_CREAT, 0600);
    assert(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0);
    assert(pxsys_desktop_package_manage("packages", state, "pxa-one", publisher, PXSYS_DESKTOP_PACKAGE_UNINSTALL) == PXA_STATUS_BUSY);
    assert(exists(private)); close(lock);
    assert(pxsys_desktop_package_manage("packages", state, "pxa-one", publisher, PXSYS_DESKTOP_PACKAGE_DISABLE) == PXA_STATUS_OK);
    assert(!pxsys_desktop_package_enabled("packages", "pxa-one"));
    assert(pxsys_desktop_package_manage("packages", state, "pxa-one", publisher, PXSYS_DESKTOP_PACKAGE_ENABLE) == PXA_STATUS_OK);
    assert(pxsys_desktop_package_enabled("packages", "pxa-one"));
    assert(pxsys_desktop_package_manage("packages", state, "pxa-one", publisher, PXSYS_DESKTOP_PACKAGE_CLEAR_DATA) == PXA_STATUS_OK);
    assert(!exists(data) && !exists(private));
    assert(exists("outside.txt") && exists("private-files/another-app/keep.txt"));
    assert(pxsys_desktop_package_installed("packages", "pxa-one"));
    assert(!pxsys_desktop_package_has_data(state, "pxa-one", publisher));
    directory("packages/.removed-pxa-one"); file("packages/.removed-pxa-one/old.txt");
    assert(pxsys_desktop_package_manage("packages", state, "pxa-one", publisher, PXSYS_DESKTOP_PACKAGE_UNINSTALL) == PXA_STATUS_OK);
    assert(!pxsys_desktop_package_installed("packages", "pxa-one"));
    assert(!exists("packages/pxa-one") && !exists("packages/.removed-pxa-one"));
    assert(pxsys_desktop_package_installed("packages", "pxa-two"));
    assert(pxsys_desktop_package_manage("packages", state, "pxa-one", publisher, PXSYS_DESKTOP_PACKAGE_UNINSTALL) == PXA_STATUS_NOT_FOUND);
    assert(symlink("pxa-two", "packages/pxa-symlink") == 0);
    assert(!pxsys_desktop_package_installed("packages", "pxa-symlink"));
    assert(pxsys_desktop_package_manage("packages", state, "pxa-symlink", publisher, PXSYS_DESKTOP_PACKAGE_UNINSTALL) == PXA_STATUS_NOT_FOUND);
    assert(exists("packages/pxa-two/manifest.pxm"));
    // Cleanup only this test's mkdtemp directory, with a known safe prefix.
    char command[256]; assert(chdir("/") == 0);
    snprintf(command, sizeof(command), "rm -rf -- '%s'", state); assert(system(command) == 0);
    puts("Desktop package store: exact data roots, uninstall, persistence, lock and symlink isolation OK");
}

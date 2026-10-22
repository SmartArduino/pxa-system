#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pxa/posix/pxa_posix_volume.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "pxa/esp/pxa_esp_posix_shim.h"
#define openat pxa_esp_openat
#define fstat pxa_esp_fstat
#define fsync pxa_esp_fsync
#endif

static const char *const names[] = {"volume.0.pxv", "volume.1.pxv"};

static pxa_status_t read_identity(int directory, unsigned slot, pxa_volume_id_t *id) {
    uint8_t bytes[PXA_VOLUME_IDENTITY_BYTES];
    struct stat metadata;
    size_t offset = 0;
    int file = openat(directory, names[slot], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (file < 0) return errno == ENOENT ? PXA_STATUS_NOT_FOUND : PXA_STATUS_IO_ERROR;
    if (fstat(file, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        metadata.st_nlink > 1 || metadata.st_size != sizeof(bytes)) {
        close(file); return PXA_STATUS_DENIED;
    }
    while (offset < sizeof(bytes)) {
        ssize_t count = read(file, bytes + offset, sizeof(bytes) - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(file); return PXA_STATUS_IO_ERROR; }
        offset += (size_t)count;
    }
    if (close(file) != 0) return PXA_STATUS_IO_ERROR;
    return pxa_volume_identity_decode((pxa_bytes_t){bytes, sizeof(bytes)}, id);
}

pxa_status_t pxa_posix_volume_identity_load(int directory, pxa_volume_id_t *output) {
    pxa_volume_id_t first, second;
    pxa_status_t a, b;
    if (directory < 0 || !output) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    a = read_identity(directory, 0, &first);
    b = read_identity(directory, 1, &second);
    if (a == PXA_STATUS_OK && b == PXA_STATUS_OK && memcmp(&first, &second, sizeof(first)))
        return PXA_STATUS_DENIED;
    if (a == PXA_STATUS_OK) { *output = first; return PXA_STATUS_OK; }
    if (b == PXA_STATUS_OK) { *output = second; return PXA_STATUS_OK; }
    return a == PXA_STATUS_NOT_FOUND && b == PXA_STATUS_NOT_FOUND
        ? PXA_STATUS_NOT_FOUND : PXA_STATUS_DENIED;
}

pxa_status_t pxa_posix_volume_identity_initialize(int directory,
    const pxa_volume_id_t *proposed, pxa_volume_id_t *actual) {
    uint8_t bytes[PXA_VOLUME_IDENTITY_BYTES];
    pxa_status_t status;
    unsigned slot;
    if (directory < 0 || !proposed || !actual) return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_posix_volume_identity_load(directory, actual);
    if (status != PXA_STATUS_NOT_FOUND) return status;
    status = pxa_volume_identity_encode(proposed, bytes);
    if (status != PXA_STATUS_OK) return status;
    for (slot = 0; slot < 2; ++slot) {
        size_t offset = 0;
        int file = openat(directory, names[slot],
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (file < 0) return PXA_STATUS_IO_ERROR;
        while (offset < sizeof(bytes)) {
            ssize_t count = write(file, bytes + offset, sizeof(bytes) - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { status = PXA_STATUS_IO_ERROR; break; }
            offset += (size_t)count;
        }
        if (status == PXA_STATUS_OK && fsync(file) != 0) status = PXA_STATUS_IO_ERROR;
        if (close(file) != 0) status = PXA_STATUS_IO_ERROR;
        if (status != PXA_STATUS_OK) return status;
    }
    if (fsync(directory) != 0) return PXA_STATUS_IO_ERROR;
    *actual = *proposed;
    return PXA_STATUS_OK;
}

#include "pxa/wamr/pxa_wamr_engine.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static pxa_status_t read_at(void *context, uint64_t offset, uint8_t *output,
                            size_t capacity, size_t *size) {
    int fd = (int)((intptr_t)context - 1);
    off_t position = (off_t)offset;
    ssize_t count;
    *size = 0;
    if (position < 0 || (uint64_t)position != offset)
        return PXA_STATUS_RESOURCE_LIMIT;
    if (lseek(fd, position, SEEK_SET) != position) return PXA_STATUS_IO_ERROR;
    do { count = read(fd, output, capacity); } while (count < 0 && errno == EINTR);
    if (count < 0) return PXA_STATUS_IO_ERROR;
    *size = (size_t)count;
    return PXA_STATUS_OK;
}

static void close_file(void *context) {
    close((int)((intptr_t)context - 1));
}

pxa_status_t pxa_wamr_open_artifact_file(
    void *context, pxa_bytes_t path, pxa_wamr_artifact_reader_t *output) {
    char filename[512];
    struct stat metadata;
    int fd;
    (void)context;
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    if (!path.data || !path.size || path.size >= sizeof(filename) ||
        memchr(path.data, 0, path.size)) return PXA_STATUS_INVALID_ARGUMENT;
    memcpy(filename, path.data, path.size);
    filename[path.size] = 0;
    fd = open(filename, O_RDONLY);
    if (fd < 0) return errno == ENOENT ? PXA_STATUS_NOT_FOUND : PXA_STATUS_IO_ERROR;
    if (fstat(fd, &metadata) || !S_ISREG(metadata.st_mode) || metadata.st_size <= 0) {
        close(fd);
        return PXA_STATUS_IO_ERROR;
    }
    output->context = (void *)((intptr_t)fd + 1);
    output->size = (uint64_t)metadata.st_size;
    output->read_at = read_at;
    output->close = close_file;
    return PXA_STATUS_OK;
}

#define _POSIX_C_SOURCE 200809L
#include "pxa/posix/pxa_posix_asset.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static pxa_status_t input_read(void *context, uint8_t *output, size_t capacity,
                                size_t *read_bytes) {
    pxa_posix_asset_input_t *stream = context;
    ssize_t count;
    pxa_status_t status;
    if (stream == NULL || stream->fd < 0 || output == NULL || read_bytes == NULL ||
        capacity > PXA_ASSET_READ_CHUNK_BYTES)
        return PXA_STATUS_INVALID_ARGUMENT;
    *read_bytes = 0;
    if (stream->gate) {
        size_t bytes=0;
        status=pxa_posix_storage_gate_read(stream->gate,stream->storage_lane,stream->fd,
            output,capacity,&bytes,stream->cancel_context,stream->cancelled);
        if(status) return status;
        count=(ssize_t)bytes;
    } else {
        do { count = read(stream->fd, output, capacity); } while (count < 0 && errno == EINTR);
        if (count < 0) return PXA_STATUS_IO_ERROR;
    }
    if ((uint64_t)count > stream->expected_bytes - stream->read_bytes)
        return PXA_STATUS_PROTOCOL_ERROR;
    stream->read_bytes += (uint64_t)count;
    stream->position += (uint64_t)count;
    *read_bytes = (size_t)count;
    return PXA_STATUS_OK;
}

void pxa_posix_asset_close(pxa_posix_asset_input_t *stream) {
    if (stream == NULL) return;
    if (stream->fd >= 0) close(stream->fd);
    stream->fd = -1;
}

pxa_status_t pxa_posix_asset_open(pxa_posix_asset_input_t *stream,
                                  const char *root, pxa_bytes_t path,
                                  uint64_t expected_bytes,
                                  pxa_asset_input_t *input) {
    char relative[PXA_ASSET_PATH_MAX + 1];
    char *part, *slash;
    int parent, fd = -1, saved_errno = 0;
    struct stat statbuf;
    if (stream == NULL || input == NULL || root == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(input, 0, sizeof(*input));
    memset(stream, 0, sizeof(*stream));
    stream->fd = -1;
    if (!pxa_asset_path_valid(path)) return PXA_STATUS_DENIED;
    memcpy(relative, path.data, path.size);
    relative[path.size] = 0;
    parent = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (parent < 0) return PXA_STATUS_IO_ERROR;
    part = relative;
    for (;;) {
        slash = strchr(part, '/');
        if (slash != NULL) *slash = 0;
        fd = openat(parent, part, O_RDONLY | O_CLOEXEC | O_NOFOLLOW |
                    (slash != NULL ? O_DIRECTORY : O_NONBLOCK));
        saved_errno = errno;
        close(parent);
        if (fd < 0 || slash == NULL) break;
        parent = fd;
        part = slash + 1;
    }
    if (fd < 0)
        return saved_errno == ENOENT ? PXA_STATUS_NOT_FOUND : PXA_STATUS_DENIED;
    if (fstat(fd, &statbuf) != 0 || !S_ISREG(statbuf.st_mode) || statbuf.st_size < 0 ||
        (uint64_t)statbuf.st_size != expected_bytes) {
        close(fd);
        return PXA_STATUS_DENIED;
    }
    stream->fd = fd;
    stream->expected_bytes = expected_bytes;
    stream->file_bytes = expected_bytes;
    input->context = stream;
    input->read = input_read;
    return PXA_STATUS_OK;
}

static pxa_status_t position_range(pxa_posix_asset_input_t *stream,
    uint32_t offset, uint32_t bytes);

pxa_status_t pxa_posix_asset_open_range(pxa_posix_asset_input_t *stream,
    const char *root, pxa_bytes_t path, uint64_t file_bytes,
    uint32_t offset, uint32_t bytes, pxa_asset_input_t *input) {
    if ((uint64_t)offset > file_bytes || bytes > file_bytes - offset ||
        bytes > PXA_ASSET_BLOB_BLOCK_BYTES) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_status_t status = pxa_posix_asset_open(stream, root, path, file_bytes, input);
    if (status != PXA_STATUS_OK) return status;
    status = position_range(stream, offset, bytes);
    if (status) {
        pxa_posix_asset_close(stream);
        memset(input, 0, sizeof(*input));
    }
    return status;
}

static pxa_status_t position_range(pxa_posix_asset_input_t *stream,
    uint32_t offset, uint32_t bytes) {
    if (!stream || stream->fd < 0 || offset > stream->file_bytes ||
        bytes > stream->file_bytes - offset || bytes > PXA_ASSET_BLOB_BLOCK_BYTES)
        return PXA_STATUS_INVALID_ARGUMENT;
    if ((uint64_t)(off_t)offset != offset ||
        (stream->position != offset && lseek(stream->fd, (off_t)offset, SEEK_SET) < 0)) return PXA_STATUS_IO_ERROR;
    stream->position = offset;
    stream->expected_bytes = bytes;
    stream->read_bytes = 0;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_posix_asset_select_range(pxa_posix_asset_input_t *stream,
    uint32_t offset, uint32_t bytes) {
    pxa_status_t status = position_range(stream, offset, bytes);
    if (status) return status;
    return PXA_STATUS_OK;
}

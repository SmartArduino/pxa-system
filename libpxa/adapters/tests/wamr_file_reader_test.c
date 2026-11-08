#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned seeks;
static int interrupt_read, fail_read, fail_seek, fail_alloc;
static size_t max_read = SIZE_MAX;
static off_t counted_seek(int fd, off_t offset, int whence) {
    ++seeks;
    if (fail_seek) { errno = EIO; return -1; }
    return lseek(fd, offset, whence);
}
static ssize_t short_read(int fd, void *data, size_t size) {
    if (interrupt_read) { interrupt_read = 0; errno = EINTR; return -1; }
    if (fail_read) { errno = EIO; return -1; }
    return read(fd, data, size < max_read ? size : max_read);
}
static void *allocate(size_t size) { return fail_alloc ? NULL : malloc(size); }
#define lseek counted_seek
#define read short_read
#define malloc allocate
#include "../wamr/pxa_wamr_file_reader.c"
#undef lseek
#undef read
#undef malloc

int main(void) {
    char path[] = "pxa-reader-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    pxa_bytes_t name = {(const uint8_t *)path, strlen(path)};
    pxa_wamr_artifact_reader_t reader;
    assert(pxa_wamr_open_artifact_file(NULL, name, &reader) == PXA_STATUS_IO_ERROR);
    const char data[] = "0123456789abcdef";
    assert(write(fd, data, sizeof(data)-1) == sizeof(data)-1);
    close(fd);
    fail_alloc = 1;
    assert(pxa_wamr_open_artifact_file(NULL, name, &reader) == PXA_STATUS_RESOURCE_LIMIT);
    assert(reader.context == NULL && reader.close == NULL);
    fail_alloc = 0;
    assert(pxa_wamr_open_artifact_file(NULL, name, &reader) == PXA_STATUS_OK);
    assert(reader.size == sizeof(data)-1);
    uint8_t output[32]; size_t size;
    max_read = 3; interrupt_read = 1;
    for (size_t offset = 0; offset < reader.size;) {
        assert(reader.read_at(reader.context, offset, output, sizeof(output), &size) == PXA_STATUS_OK);
        assert(size && size <= max_read && !memcmp(output, data+offset, size));
        offset += size;
    }
    assert(seeks == 0);
    assert(reader.read_at(reader.context, reader.size, output, sizeof(output), &size) == PXA_STATUS_OK && size == 0);
    assert(reader.read_at(reader.context, 2, output, 3, &size) == PXA_STATUS_OK && size == 3 && !memcmp(output,"234",3));
    assert(seeks == 1);
    fail_seek = 1;
    assert(reader.read_at(reader.context, 9, output, 1, &size) == PXA_STATUS_IO_ERROR && size == 0);
    fail_seek = 0;
    assert(reader.read_at(reader.context, 5, output, 1, &size) == PXA_STATUS_OK && output[0] == '5');
    fail_read = 1;
    assert(reader.read_at(reader.context, 6, output, 1, &size) == PXA_STATUS_IO_ERROR && size == 0);
    fail_read = 0;
    assert(reader.read_at(reader.context, 6, output, 1, &size) == PXA_STATUS_OK && output[0] == '6');
    assert(reader.read_at(reader.context, UINT64_MAX, output, 1, &size) == PXA_STATUS_RESOURCE_LIMIT && size == 0);
    reader.close(reader.context);
    assert(unlink(path) == 0);
    assert(pxa_wamr_open_artifact_file(NULL, name, &reader) == PXA_STATUS_NOT_FOUND);
    assert(pxa_wamr_open_artifact_file(NULL, (pxa_bytes_t){(const uint8_t *)".",1}, &reader) == PXA_STATUS_IO_ERROR);
    puts("PASS: artifact reader sequential, random, short, interrupted and failed I/O");
    return 0;
}

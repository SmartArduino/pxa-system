#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pxa/posix/pxa_posix_install_record.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include "pxa/esp/pxa_esp_posix_shim.h"
#define openat pxa_esp_openat
#define fstat pxa_esp_fstat
#define unlinkat pxa_esp_unlinkat
#define fsync pxa_esp_fsync
#endif

static const char *const slots[] = {"install.0.pxi", "install.1.pxi"};
static const uint8_t sealed_header[8]={'P','X','I','S',1,0,0,0};
static const uint8_t purpose[]="install-record/1";

static int valid_auth(const pxa_posix_install_record_auth_t *auth) {
    return !auth || (auth->make_tag && auth->binding.data && auth->binding.size && auth->binding.size<=512);
}
pxa_status_t pxa_posix_install_record_pack(const pxa_install_record_t *record,
    const pxa_posix_install_record_auth_t *auth,uint8_t output[PXA_POSIX_INSTALL_RECORD_SEALED_BYTES],size_t *size) {
    pxa_status_t status;
    if(size)*size=0;
    if(!output || !size || !valid_auth(auth))return PXA_STATUS_INVALID_ARGUMENT;
    if(auth)memcpy(output,sealed_header,sizeof(sealed_header));
    status=pxa_install_record_encode(record,output+(auth?sizeof(sealed_header):0));
    if(status!=PXA_STATUS_OK)return status;
    if(auth) {
        status=auth->make_tag(auth->context,(pxa_bytes_t){purpose,sizeof(purpose)-1},auth->binding,
            (pxa_bytes_t){output,sizeof(sealed_header)+PXA_INSTALL_RECORD_BYTES},
            output+sizeof(sealed_header)+PXA_INSTALL_RECORD_BYTES);
        if(status!=PXA_STATUS_OK)return status;
    }
    *size=auth?PXA_POSIX_INSTALL_RECORD_SEALED_BYTES:PXA_INSTALL_RECORD_BYTES;
    return PXA_STATUS_OK;
}
pxa_status_t pxa_posix_install_record_unpack(pxa_bytes_t input,const pxa_volume_id_t *volume,
    const pxa_posix_install_record_auth_t *auth,pxa_install_record_t *record) {
    uint8_t expected[32];volatile uint8_t different=0;pxa_status_t status;
    if(!record || !volume || !valid_auth(auth))return PXA_STATUS_INVALID_ARGUMENT;
    memset(record,0,sizeof(*record));
    if(!auth)return pxa_install_record_decode(input,volume,record);
    if(!input.data || (input.size!=PXA_POSIX_INSTALL_RECORD_SEALED_BYTES &&
        input.size!=PXA_POSIX_INSTALL_RECORD_SEALED_V1_BYTES) ||
        memcmp(input.data,sealed_header,sizeof(sealed_header)))return PXA_STATUS_DENIED;
    size_t body_bytes=input.size-40;
    status=auth->make_tag(auth->context,(pxa_bytes_t){purpose,sizeof(purpose)-1},auth->binding,
        (pxa_bytes_t){input.data,sizeof(sealed_header)+body_bytes},expected);
    if(status!=PXA_STATUS_OK)return status;
    for(unsigned i=0;i<sizeof(expected);++i)
        different|=expected[i]^input.data[sizeof(sealed_header)+body_bytes+i];
    if(different)return PXA_STATUS_DENIED;
    return pxa_install_record_decode((pxa_bytes_t){input.data+sizeof(sealed_header),body_bytes},volume,record);
}

static pxa_status_t read_slot_bytes(int directory, uint8_t slot,
    const pxa_posix_install_record_auth_t *auth,
    uint8_t bytes[PXA_POSIX_INSTALL_RECORD_SEALED_BYTES], size_t *size) {
    struct stat before, after;
    size_t offset = 0,expected=auth?PXA_POSIX_INSTALL_RECORD_SEALED_BYTES:PXA_INSTALL_RECORD_BYTES;
    size_t legacy=auth?PXA_POSIX_INSTALL_RECORD_SEALED_V1_BYTES:PXA_INSTALL_RECORD_V1_BYTES;
    int fd = openat(directory, slots[slot], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    *size = 0;
    if (fd < 0) return errno == ENOENT ? PXA_STATUS_OK : PXA_STATUS_IO_ERROR;
    if (fstat(fd, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_nlink > 1 || (before.st_size != (off_t)expected && before.st_size != (off_t)legacy)) {
        close(fd);
        /* A present malformed slot is not equivalent to a missing record. */
        *size = 1;
        bytes[0] = 0;
        return PXA_STATUS_OK;
    }
    expected=(size_t)before.st_size;
    while (offset < expected) {
        ssize_t count = read(fd, bytes + offset, expected - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); return PXA_STATUS_IO_ERROR; }
        offset += (size_t)count;
    }
    if (fstat(fd, &after) != 0 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size ||
        before.st_mtime != after.st_mtime) {
        close(fd);
        return PXA_STATUS_DENIED;
    }
    if (close(fd) != 0) return PXA_STATUS_IO_ERROR;
    *size = offset;
    return PXA_STATUS_OK;
}

static pxa_status_t read_slot(int directory,uint8_t slot,const pxa_volume_id_t *volume,
    const pxa_posix_install_record_auth_t *auth,
    uint8_t bytes[PXA_POSIX_INSTALL_RECORD_SEALED_BYTES],size_t *size) {
    pxa_status_t status=read_slot_bytes(directory,slot,auth,bytes,size);
    if(status!=PXA_STATUS_OK || !*size)return status;
    size_t offset=*size;
    if(auth) {
        pxa_install_record_t record;
        pxa_status_t status=pxa_posix_install_record_unpack((pxa_bytes_t){bytes,offset},volume,auth,&record);
        if(status==PXA_STATUS_DENIED || status==PXA_STATUS_INVALID_ARGUMENT) {
            *size=1;bytes[0]=0;return PXA_STATUS_OK;
        }
        if(status!=PXA_STATUS_OK)return status;
        *size=offset-40;memmove(bytes,bytes+sizeof(sealed_header),*size);
    }
    return PXA_STATUS_OK;
}

static pxa_status_t load(int directory, const pxa_volume_id_t *volume,
    const pxa_posix_install_record_auth_t *auth,pxa_install_record_t *record, uint8_t *slot) {
    uint8_t a[PXA_POSIX_INSTALL_RECORD_SEALED_BYTES], b[PXA_POSIX_INSTALL_RECORD_SEALED_BYTES];
    size_t na, nb;
    pxa_status_t status;
    if (directory < 0 || !volume || !record || !valid_auth(auth)) return PXA_STATUS_INVALID_ARGUMENT;
    status = read_slot(directory, 0, volume, auth, a, &na);
    if (status != PXA_STATUS_OK) return status;
    status = read_slot(directory, 1, volume, auth, b, &nb);
    if (status != PXA_STATUS_OK) return status;
    return pxa_install_record_select((pxa_bytes_t){a, na}, (pxa_bytes_t){b, nb},
        volume, record, slot);
}

pxa_status_t pxa_posix_install_record_load(int directory,
    const pxa_volume_id_t *volume, pxa_install_record_t *record) {
    return pxa_posix_install_record_load_with_auth(directory,volume,NULL,record);
}
pxa_status_t pxa_posix_install_record_load_with_auth(int directory,
    const pxa_volume_id_t *volume,const pxa_posix_install_record_auth_t *auth,pxa_install_record_t *record) {
    uint8_t slot;
    return load(directory, volume, auth, record, &slot);
}

static pxa_status_t write_slot(int directory,uint8_t current_slot,const uint8_t *bytes,size_t size) {
    struct stat metadata;size_t offset=0;int fd;pxa_status_t status;
    /* Remove only the inactive slot. The committed slot remains untouched.
     * Fixed-size writes plus CRC tolerate filesystems without atomic replace. */
    fd = openat(directory, slots[current_slot], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd >= 0) {
        int regular = fstat(fd, &metadata) == 0 && S_ISREG(metadata.st_mode) &&
            metadata.st_nlink <= 1;
        close(fd);
        if (!regular) return PXA_STATUS_DENIED;
        if (unlinkat(directory, slots[current_slot], 0) != 0) return PXA_STATUS_IO_ERROR;
    } else if (errno != ENOENT) return PXA_STATUS_DENIED;
    fd = openat(directory, slots[current_slot],
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return PXA_STATUS_IO_ERROR;
    status = PXA_STATUS_OK;
    while (offset < size) {
        ssize_t count = write(fd, bytes + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { status = PXA_STATUS_IO_ERROR; break; }
        offset += (size_t)count;
    }
    if (status == PXA_STATUS_OK && fsync(fd) != 0) status = PXA_STATUS_IO_ERROR;
    if (close(fd) != 0) status = PXA_STATUS_IO_ERROR;
    if (status == PXA_STATUS_OK && fsync(directory) != 0) status = PXA_STATUS_IO_ERROR;
    return status;
}

pxa_status_t pxa_posix_install_record_commit(int directory,
    uint64_t expected, const pxa_install_record_t *record) {
    return pxa_posix_install_record_commit_with_auth(directory,expected,record,NULL);
}
pxa_status_t pxa_posix_install_record_commit_with_auth(int directory,
    uint64_t expected,const pxa_install_record_t *record,const pxa_posix_install_record_auth_t *auth) {
    uint8_t bytes[PXA_POSIX_INSTALL_RECORD_SEALED_BYTES], current_slot = 1;
    pxa_install_record_t current;
    pxa_status_t status;
    size_t size=0;
    if (directory < 0 || !record) return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_posix_install_record_pack(record,auth,bytes,&size);
    if (status != PXA_STATUS_OK) return status;
    status = load(directory, &record->app_volume, auth, &current, &current_slot);
    if (status == PXA_STATUS_NOT_FOUND) {
        if (expected || record->generation != 1) return PXA_STATUS_BAD_STATE;
        current_slot = 1;
    } else if (status != PXA_STATUS_OK) return status;
    else {
        if (current.generation != expected || expected == UINT64_MAX ||
            record->generation != expected + 1) return PXA_STATUS_BAD_STATE;
        if (memcmp(current.install_id, record->install_id, sizeof(current.install_id)) ||
            memcmp(current.publisher_root, record->publisher_root, sizeof(current.publisher_root)) ||
            strcmp(current.app_id, record->app_id)) return PXA_STATUS_DENIED;
    }
    current_slot ^= 1;
    return write_slot(directory,current_slot,bytes,size);
}

static int same_record(const pxa_install_record_t *a,const pxa_install_record_t *b) {
    uint8_t first[PXA_INSTALL_RECORD_BYTES],second[PXA_INSTALL_RECORD_BYTES];
    return pxa_install_record_encode(a,first)==PXA_STATUS_OK &&
        pxa_install_record_encode(b,second)==PXA_STATUS_OK && !memcmp(first,second,sizeof(first));
}
pxa_status_t pxa_posix_install_record_mirror(int directory,
    const pxa_install_record_t *expected,const pxa_posix_install_record_auth_t *auth) {
    pxa_install_record_t selected;uint8_t slot,bytes[PXA_POSIX_INSTALL_RECORD_SEALED_BYTES];size_t size=0;
    if(directory<0 || !expected || !valid_auth(auth))return PXA_STATUS_INVALID_ARGUMENT;
    pxa_status_t status=load(directory,&expected->app_volume,auth,&selected,&slot);
    if(status!=PXA_STATUS_OK)return status;
    if(selected.generation!=expected->generation)return PXA_STATUS_BAD_STATE;
    if(!same_record(&selected,expected))return PXA_STATUS_DENIED;
    status=read_slot_bytes(directory,slot,auth,bytes,&size);if(status!=PXA_STATUS_OK)return status;
    status=pxa_posix_install_record_unpack((pxa_bytes_t){bytes,size},&expected->app_volume,auth,&selected);
    if(status!=PXA_STATUS_OK)return status;
    if(!same_record(&selected,expected))return PXA_STATUS_BAD_STATE;
    return write_slot(directory,(uint8_t)(slot^1),bytes,size);
}

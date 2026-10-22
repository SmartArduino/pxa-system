#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pxa/posix/pxa_posix_install_record.h"
#include "pxa/posix/pxa_posix_volume.h"
#include "pxa/container.h"
#include "record_test_auth.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static pxa_install_record_t example(void) {
    pxa_install_record_t record = {0};
    record.generation = 1;
    record.release_sequence = 200;
    record.data_generation=7;
    record.flags = PXA_INSTALL_RECORD_ENABLED | PXA_INSTALL_RECORD_HAS_MEDIA |
        PXA_INSTALL_RECORD_MANAGED_MEDIA;
    memset(record.install_id, 1, sizeof(record.install_id));
    memset(record.publisher_root, 2, sizeof(record.publisher_root));
    memset(record.app_volume.bytes, 3, sizeof(record.app_volume.bytes));
    memset(record.media_volume.bytes, 4, sizeof(record.media_volume.bytes));
    memset(record.manifest_sha256, 5, sizeof(record.manifest_sha256));
    memset(record.media_set_sha256, 6, sizeof(record.media_set_sha256));
    strcpy(record.app_id, "pxa-quake");
    return record;
}

static void format_checks(void) {
    pxa_install_record_t record = example(), decoded;
    uint8_t a[PXA_INSTALL_RECORD_BYTES], b[PXA_INSTALL_RECORD_BYTES], slot;
    pxa_bytes_t empty = {NULL, 0};
    size_t i;
    assert(pxa_install_record_encode(&record, a) == PXA_STATUS_OK);
    assert(pxa_install_record_decode((pxa_bytes_t){a, sizeof(a)},
        &record.app_volume, &decoded) == PXA_STATUS_OK);
    assert(decoded.release_sequence == 200 && decoded.data_generation==7 && !strcmp(decoded.app_id, record.app_id));
    for (i = 0; i < sizeof(a); ++i) {
        memcpy(b, a, sizeof(b)); b[i] ^= 1;
        assert(pxa_install_record_decode((pxa_bytes_t){b, sizeof(b)},
            &record.app_volume, &decoded) == PXA_STATUS_DENIED);
    }
    assert(pxa_install_record_select(empty, empty, &record.app_volume,
        &decoded, &slot) == PXA_STATUS_NOT_FOUND);
    assert(pxa_install_record_select((pxa_bytes_t){a, 12}, empty,
        &record.app_volume, &decoded, &slot) == PXA_STATUS_DENIED);
    record.generation = 2;
    assert(pxa_install_record_encode(&record, b) == PXA_STATUS_OK);
    assert(pxa_install_record_select((pxa_bytes_t){a, sizeof(a)},
        (pxa_bytes_t){b, sizeof(b)}, &record.app_volume, &decoded, &slot) == PXA_STATUS_OK);
    assert(decoded.generation == 2 && slot == 1);
    record.generation = 1; record.flags ^= PXA_INSTALL_RECORD_ENABLED;
    assert(pxa_install_record_encode(&record, b) == PXA_STATUS_OK);
    assert(pxa_install_record_select((pxa_bytes_t){a, sizeof(a)},
        (pxa_bytes_t){b, sizeof(b)}, &record.app_volume, &decoded, &slot) == PXA_STATUS_DENIED);
    record.generation = 2; record.install_id[0] ^= 1;
    assert(pxa_install_record_encode(&record, b) == PXA_STATUS_OK);
    assert(pxa_install_record_select((pxa_bytes_t){a, sizeof(a)},
        (pxa_bytes_t){b, sizeof(b)}, &record.app_volume, &decoded, &slot) == PXA_STATUS_DENIED);
    record.app_id[0] = '/';
    assert(pxa_install_record_encode(&record, b) == PXA_STATUS_INVALID_ARGUMENT);
}

static void durable_slots(void) {
    char directory[] = "/tmp/pxa-install-record-XXXXXX";
    pxa_install_record_t record = example(), loaded;
    pxa_volume_id_t other = {{99}};
    int fd, file;
    uint8_t byte = 0;
    assert(mkdtemp(directory));
    fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(fd >= 0);
    assert(pxa_posix_install_record_load(fd, &record.app_volume, &loaded) == PXA_STATUS_NOT_FOUND);
    assert(pxa_posix_install_record_commit(fd, 0, &record) == PXA_STATUS_OK);
    assert(pxa_posix_install_record_load(fd, &record.app_volume, &loaded) == PXA_STATUS_OK);
    assert(loaded.generation == 1);
    assert(pxa_posix_install_record_commit(fd, 0, &record) == PXA_STATUS_BAD_STATE);
    record.generation = 2; record.release_sequence = 201;
    assert(pxa_posix_install_record_commit(fd, 1, &record) == PXA_STATUS_OK);
    close(fd);
    fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(pxa_posix_install_record_load(fd, &record.app_volume, &loaded) == PXA_STATUS_OK);
    assert(loaded.generation == 2 && loaded.release_sequence == 201);
    assert(pxa_posix_install_record_load(fd, &other, &loaded) == PXA_STATUS_DENIED);
    /* A torn inactive update leaves the previous committed generation usable. */
    file = openat(fd, "install.0.pxi", O_WRONLY | O_TRUNC);
    assert(file >= 0 && write(file, &byte, 1) == 1 && close(file) == 0);
    assert(pxa_posix_install_record_load(fd, &record.app_volume, &loaded) == PXA_STATUS_OK);
    assert(loaded.generation == 2);
    record.generation = 3;
    assert(pxa_posix_install_record_commit(fd, 2, &record) == PXA_STATUS_OK);
    assert(pxa_posix_install_record_load(fd, &record.app_volume, &loaded) == PXA_STATUS_OK);
    assert(loaded.generation == 3);
    /* Wrong identity cannot take over an existing application's data. */
    record.generation = 4; record.publisher_root[0] ^= 1;
    assert(pxa_posix_install_record_commit(fd, 3, &record) == PXA_STATUS_DENIED);
    record.publisher_root[0] ^= 1;
    /* Refuse an inactive hard link before modifying it. */
    assert(unlinkat(fd, "install.1.pxi", 0) == 0);
    assert(linkat(fd, "install.0.pxi", fd, "install.1.pxi", 0) == 0);
    assert(pxa_posix_install_record_commit(fd, 3, &record) == PXA_STATUS_DENIED);
    assert(unlinkat(fd, "install.1.pxi", 0) == 0);
    assert(symlinkat("install.0.pxi", fd, "install.1.pxi") == 0);
    assert(pxa_posix_install_record_commit(fd, 3, &record) != PXA_STATUS_OK);
    assert(unlinkat(fd, "install.1.pxi", 0) == 0);
    assert(pxa_posix_install_record_commit(fd, 3, &record) == PXA_STATUS_OK);
    assert(pxa_posix_install_record_load(fd, &record.app_volume, &loaded) == PXA_STATUS_OK);
    assert(loaded.generation == 4);
    assert(unlinkat(fd, "install.0.pxi", 0) == 0);
    assert(unlinkat(fd, "install.1.pxi", 0) == 0);
    close(fd);
    assert(rmdir(directory) == 0);
}

static void authenticated_records(void) {
    char path[]="/tmp/pxa-record-auth-XXXXXX";
    record_test_auth_context_t key={{1},1};
    const char locator[]="doit/pxa-quake",other_locator[]="other/pxa-quake";
    pxa_posix_install_record_auth_t auth={&key,{(const uint8_t *)locator,sizeof(locator)-1},record_test_tag};
    pxa_posix_install_record_auth_t other=auth;
    pxa_install_record_t record=example(),loaded,forged;
    uint8_t encoded[PXA_POSIX_INSTALL_RECORD_SEALED_BYTES],changed[sizeof(encoded)],plain[sizeof(encoded)];
    size_t size=0,plain_size=0;int directory,file;
    assert(!pxa_posix_install_record_pack(&record,&auth,encoded,&size) && size==sizeof(encoded));
    assert(!pxa_posix_install_record_unpack((pxa_bytes_t){encoded,size},&record.app_volume,&auth,&loaded));
    assert(!pxa_posix_install_record_pack(&record,NULL,plain,&plain_size) && plain_size==PXA_INSTALL_RECORD_BYTES);
    assert(pxa_posix_install_record_unpack((pxa_bytes_t){plain,plain_size},&record.app_volume,&auth,&loaded)==PXA_STATUS_DENIED);
    for(unsigned i=0;i<sizeof(encoded);++i) {
        memcpy(changed,encoded,sizeof(changed));changed[i]^=1;
        assert(pxa_posix_install_record_unpack((pxa_bytes_t){changed,sizeof(changed)},&record.app_volume,&auth,&loaded)==PXA_STATUS_DENIED);
    }
    /* Recomputing the public CRC does not authenticate modified policy. */
    forged=record;forged.flags^=PXA_INSTALL_RECORD_ENABLED;
    memcpy(changed,encoded,sizeof(changed));assert(!pxa_install_record_encode(&forged,changed+8));
    assert(pxa_posix_install_record_unpack((pxa_bytes_t){changed,sizeof(changed)},&record.app_volume,&auth,&loaded)==PXA_STATUS_DENIED);
    other.binding=(pxa_bytes_t){(const uint8_t *)other_locator,sizeof(other_locator)-1};
    assert(pxa_posix_install_record_unpack((pxa_bytes_t){encoded,size},&record.app_volume,&other,&loaded)==PXA_STATUS_DENIED);
    key.key[0]^=128;
    assert(pxa_posix_install_record_unpack((pxa_bytes_t){encoded,size},&record.app_volume,&auth,&loaded)==PXA_STATUS_DENIED);
    key.key[0]^=128;
    assert(mkdtemp(path));directory=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);assert(directory>=0);
    assert(!pxa_posix_install_record_commit_with_auth(directory,0,&record,&auth));
    assert(!pxa_posix_install_record_load_with_auth(directory,&record.app_volume,&auth,&loaded) && loaded.generation==1);
    assert(pxa_posix_install_record_load(directory,&record.app_volume,&loaded)==PXA_STATUS_DENIED);
    record.generation=2;assert(!pxa_posix_install_record_commit_with_auth(directory,1,&record,&auth));
    assert(pxa_posix_install_record_load_with_auth(directory,&record.app_volume,&other,&loaded)==PXA_STATUS_DENIED);
    key.available=0;record.generation=3;
    assert(pxa_posix_install_record_commit_with_auth(directory,2,&record,&auth)==PXA_STATUS_UNAVAILABLE);
    assert(pxa_posix_install_record_load_with_auth(directory,&record.app_volume,&auth,&loaded)==PXA_STATUS_UNAVAILABLE);
    key.available=1;
    assert(!pxa_posix_install_record_load_with_auth(directory,&record.app_volume,&auth,&loaded) && loaded.generation==2);
    file=openat(directory,"install.1.pxi",O_RDWR);assert(file>=0);
    uint8_t bad;
    assert(pread(file,&bad,1,PXA_POSIX_INSTALL_RECORD_SEALED_BYTES-1)==1);bad^=1;
    assert(pwrite(file,&bad,1,PXA_POSIX_INSTALL_RECORD_SEALED_BYTES-1)==1 && !close(file));
    /* A torn authenticated slot can fall back to the other authenticated slot. */
    assert(!pxa_posix_install_record_load_with_auth(directory,&record.app_volume,&auth,&loaded) && loaded.generation==1);
    assert(!unlinkat(directory,"install.0.pxi",0) && !unlinkat(directory,"install.1.pxi",0));
    assert(!close(directory) && !rmdir(path));
}

static void volume_identity(void) {
    char path[] = "/tmp/pxa-volume-identity-XXXXXX";
    pxa_volume_id_t proposed = {{1}}, other = {{2}}, actual;
    uint8_t bytes[PXA_VOLUME_IDENTITY_BYTES], changed[PXA_VOLUME_IDENTITY_BYTES];
    unsigned i;
    int directory, file;
    assert(pxa_volume_identity_encode(&proposed, bytes) == PXA_STATUS_OK);
    for (i = 0; i < sizeof(bytes); ++i) {
        memcpy(changed, bytes, sizeof(changed)); changed[i] ^= 1;
        assert(pxa_volume_identity_decode((pxa_bytes_t){changed, sizeof(changed)},
            &actual) == PXA_STATUS_DENIED);
    }
    assert(mkdtemp(path));
    directory = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(directory >= 0);
    assert(pxa_posix_volume_identity_load(directory, &actual) == PXA_STATUS_NOT_FOUND);
    assert(pxa_posix_volume_identity_initialize(directory, &proposed, &actual) == PXA_STATUS_OK);
    assert(!memcmp(&actual, &proposed, sizeof(actual)));
    close(directory);
    directory = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(pxa_posix_volume_identity_initialize(directory, &other, &actual) == PXA_STATUS_OK);
    assert(!memcmp(&actual, &proposed, sizeof(actual)));
    file = openat(directory, "volume.0.pxv", O_WRONLY | O_TRUNC);
    assert(file >= 0 && write(file, "partial", 7) == 7 && close(file) == 0);
    assert(pxa_posix_volume_identity_load(directory, &actual) == PXA_STATUS_OK);
    assert(!memcmp(&actual, &proposed, sizeof(actual)));
    assert(pxa_volume_identity_encode(&other, changed) == PXA_STATUS_OK);
    file = openat(directory, "volume.0.pxv", O_WRONLY | O_TRUNC);
    assert(file >= 0 && write(file, changed, sizeof(changed)) == sizeof(changed) && close(file) == 0);
    assert(pxa_posix_volume_identity_load(directory, &actual) == PXA_STATUS_DENIED);
    assert(pxa_posix_volume_identity_initialize(directory, &proposed, &actual) == PXA_STATUS_DENIED);
    assert(unlinkat(directory, "volume.0.pxv", 0) == 0);
    assert(unlinkat(directory, "volume.1.pxv", 0) == 0);
    close(directory); assert(rmdir(path) == 0);
}

static void legacy_data_generation(void) {
    pxa_install_record_t record=example(),loaded;uint8_t current[PXA_INSTALL_RECORD_BYTES];
    uint8_t legacy[PXA_INSTALL_RECORD_V1_BYTES],sealed[PXA_POSIX_INSTALL_RECORD_SEALED_V1_BYTES];
    static const uint8_t locator[]="publisher/pxa-quake",purpose[]="install-record/1";
    record_test_auth_context_t key={{1},1};
    pxa_posix_install_record_auth_t auth={&key,{locator,sizeof(locator)-1},record_test_tag};
    record.data_generation=0;assert(!pxa_install_record_encode(&record,current));
    memcpy(legacy,current,240);pxa_write_u16(legacy+4,1);pxa_write_u16(legacy+6,sizeof(legacy));
    pxa_write_u32(legacy+240,pxa_container_crc32(legacy,240));
    assert(!pxa_install_record_decode((pxa_bytes_t){legacy,sizeof(legacy)},&record.app_volume,&loaded));
    assert(!loaded.data_generation && loaded.generation==1);
    memcpy(sealed,"PXIS\1\0\0\0",8);memcpy(sealed+8,legacy,sizeof(legacy));
    assert(!record_test_tag(&key,(pxa_bytes_t){purpose,sizeof(purpose)-1},auth.binding,
        (pxa_bytes_t){sealed,sizeof(sealed)-32},sealed+sizeof(sealed)-32));
    assert(!pxa_posix_install_record_unpack((pxa_bytes_t){sealed,sizeof(sealed)},&record.app_volume,&auth,&loaded));
    assert(!loaded.data_generation);
    char directory[]="/tmp/pxa-legacy-record-XXXXXX";assert(mkdtemp(directory));
    int dir=open(directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);assert(dir>=0);
    int file=openat(dir,"install.0.pxi",O_WRONLY|O_CREAT|O_EXCL,0600);assert(file>=0);
    assert(write(file,sealed,sizeof(sealed))==sizeof(sealed) && !close(file));
    assert(!pxa_posix_install_record_load_with_auth(dir,&record.app_volume,&auth,&loaded) && !loaded.data_generation);
    assert(!pxa_posix_install_record_mirror(dir,&record,&auth));
    struct stat old_slot;assert(!fstatat(dir,"install.1.pxi",&old_slot,AT_SYMLINK_NOFOLLOW) &&
        old_slot.st_size==PXA_POSIX_INSTALL_RECORD_SEALED_V1_BYTES);
    record.generation=2;record.data_generation=1;
    assert(!pxa_posix_install_record_commit_with_auth(dir,1,&record,&auth));
    assert(!pxa_posix_install_record_load_with_auth(dir,&record.app_volume,&auth,&loaded));
    assert(loaded.generation==2 && loaded.data_generation==1);
    /* A torn new generation still resolves the authenticated legacy data root. */
    file=openat(dir,"install.1.pxi",O_WRONLY|O_TRUNC);assert(file>=0);
    assert(write(file,"P",1)==1 && !close(file));
    assert(!pxa_posix_install_record_load_with_auth(dir,&record.app_volume,&auth,&loaded));
    assert(loaded.generation==1 && !loaded.data_generation);
    assert(!unlinkat(dir,"install.0.pxi",0) && !unlinkat(dir,"install.1.pxi",0) && !close(dir) && !rmdir(directory));
}

static void mirrored_revocation(void) {
    char directory[]="/tmp/pxa-record-mirror-XXXXXX";assert(mkdtemp(directory));
    int dir=open(directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);assert(dir>=0);
    pxa_install_record_t record=example(),loaded,stale=record;
    record_test_auth_context_t key={{1},1};static const uint8_t name[]="publisher/pxa-quake";
    pxa_posix_install_record_auth_t auth={&key,{name,sizeof(name)-1},record_test_tag};
    assert(!pxa_posix_install_record_commit_with_auth(dir,0,&record,&auth));
    ++record.generation;record.flags&=~PXA_INSTALL_RECORD_ENABLED;
    assert(!pxa_posix_install_record_commit_with_auth(dir,1,&record,&auth));
    assert(pxa_posix_install_record_mirror(dir,&stale,&auth)==PXA_STATUS_BAD_STATE);
    key.available=0;assert(pxa_posix_install_record_mirror(dir,&record,&auth)==PXA_STATUS_UNAVAILABLE);key.available=1;
    assert(!pxa_posix_install_record_mirror(dir,&record,&auth));
    uint8_t bad=0;
    int file=openat(dir,"install.1.pxi",O_WRONLY|O_TRUNC);assert(file>=0);
    assert(write(file,&bad,1)==1 && !close(file));
    assert(!pxa_posix_install_record_load_with_auth(dir,&record.app_volume,&auth,&loaded));
    assert(loaded.generation==2 && !(loaded.flags&PXA_INSTALL_RECORD_ENABLED));
    assert(!pxa_posix_install_record_mirror(dir,&record,&auth));
    file=openat(dir,"install.0.pxi",O_WRONLY|O_TRUNC);assert(file>=0);
    assert(write(file,&bad,1)==1 && !close(file));
    assert(!pxa_posix_install_record_load_with_auth(dir,&record.app_volume,&auth,&loaded));
    assert(loaded.generation==2 && !(loaded.flags&PXA_INSTALL_RECORD_ENABLED));
    assert(!unlinkat(dir,"install.0.pxi",0) && !unlinkat(dir,"install.1.pxi",0) && !close(dir) && !rmdir(directory));
}

int main(void) {
    format_checks();
    durable_slots();
    authenticated_records();
    legacy_data_generation();
    mirrored_revocation();
    volume_identity();
    puts("Volume-local installation records: reopen, torn update, wrong volume/identity, conflicts and link rejection passed");
    return 0;
}

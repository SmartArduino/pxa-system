#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pxa/posix/pxa_posix_app_store.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include "pxa/esp/pxa_esp_posix_shim.h"
#define open(...) pxa_esp_open(__VA_ARGS__)
#define close(fd) pxa_esp_close(fd)
#define dup pxa_esp_dup
#define openat pxa_esp_openat
#define fstat pxa_esp_fstat
#define mkdirat pxa_esp_mkdirat
#define unlinkat pxa_esp_unlinkat
#define fdopendir pxa_esp_fdopendir
#define fsync pxa_esp_fsync
#endif

static const char pending_name[] = "install.pending.pxi";
typedef char app_home_snapshot_budget[sizeof(pxa_posix_app_home_t)<=1024?1:-1];
static const pxa_posix_install_record_auth_t *record_auth(const pxa_posix_app_store_config_t *config,
    const char *locator,pxa_posix_install_record_auth_t *auth) {
    if(!config->record_tag)return NULL;
    *auth=(pxa_posix_install_record_auth_t){config->record_auth_context,
        {(const uint8_t *)locator,strlen(locator)},config->record_tag};
    return auth;
}

static int safe_component(const char *name, size_t size) {
    size_t i;
    if (!size || size > 64 || name[0] < 'a' || name[0] > 'z') return 0;
    for (i = 0; i < size; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.')) return 0;
    }
    return 1;
}
static int safe_publisher(const char *name,size_t size) {
    /* Digest-based publishers may start with a digit. Keep App ID rules and
     * the rest of the component grammar unchanged. */
    if(!size || size>64 || !((name[0]>='a' && name[0]<='z') || (name[0]>='0' && name[0]<='9')))return 0;
    for(size_t i=1;i<size;++i) {
        unsigned char c=(unsigned char)name[i];
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.'))return 0;
    }
    return 1;
}

static pxa_status_t paths(const pxa_posix_app_store_config_t *config,
    const char *locator, pxa_posix_app_home_t *home) {
    const char *slash;
    size_t root_size, locator_size;
    if (!config || !config->root || !locator || !home) return PXA_STATUS_INVALID_ARGUMENT;
    slash = strchr(locator, '/');
    if (!slash || strchr(slash + 1, '/') ||
        !safe_publisher(locator, (size_t)(slash - locator)) ||
        !safe_component(slash + 1, strlen(slash + 1))) return PXA_STATUS_INVALID_ARGUMENT;
    char physical[PXA_POSIX_APP_LOCATOR_BYTES];
    const char *disk_locator=locator;
    if(config->map_locator) {
        pxa_status_t status=config->map_locator(config->locator_context,locator,0,physical,sizeof(physical));
        if(status!=PXA_STATUS_OK)return status;
        slash=strchr(physical,'/');
        if(!slash || strchr(slash+1,'/') || !safe_publisher(physical,(size_t)(slash-physical)) ||
            !safe_component(slash+1,strlen(slash+1)))return PXA_STATUS_DENIED;
        disk_locator=physical;
    }
    root_size = strlen(config->root); locator_size = strlen(disk_locator);
    if (!root_size || config->root[0] != '/' || config->root[root_size - 1] == '/' ||
        root_size + locator_size + sizeof("/apps//private") > sizeof(home->home))
        return PXA_STATUS_LIMIT_EXCEEDED;
    memset(home, 0, sizeof(*home));
    strcpy(home->locator, locator);
    snprintf(home->home, sizeof(home->home), "%s/apps/%s", config->root, disk_locator);
    return PXA_STATUS_OK;
}

static int child_directory(int parent, const char *name, int create) {
    if (create && mkdirat(parent, name, 0700) != 0 && errno != EEXIST) return -1;
    return openat(parent, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
}

static pxa_status_t open_home(const pxa_posix_app_store_config_t *config,
    const char *locator, int create, int *output) {
    char publisher[65];
    char physical[PXA_POSIX_APP_LOCATOR_BYTES];
    if(config->map_locator) {
        pxa_status_t status=config->map_locator(config->locator_context,locator,0,physical,sizeof(physical));
        if(status!=PXA_STATUS_OK)return status;
        locator=physical;
    }
    const char *slash = strchr(locator, '/');
    if(!slash || strchr(slash+1,'/') || !safe_publisher(locator,(size_t)(slash-locator)) ||
        !safe_component(slash+1,strlen(slash+1)))return PXA_STATUS_DENIED;
    int root, apps, directory, home;
    *output = -1;
    memcpy(publisher, locator, (size_t)(slash - locator));
    publisher[slash - locator] = 0;
    root = open(config->root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root < 0) return PXA_STATUS_UNAVAILABLE;
    apps = child_directory(root, "apps", create); close(root);
    if (apps < 0) return errno == ENOENT ? PXA_STATUS_NOT_FOUND : PXA_STATUS_IO_ERROR;
    directory = child_directory(apps, publisher, create); close(apps);
    if (directory < 0) return errno == ENOENT ? PXA_STATUS_NOT_FOUND : PXA_STATUS_IO_ERROR;
    home = child_directory(directory, slash + 1, create); close(directory);
    if (home < 0) return errno == ENOENT ? PXA_STATUS_NOT_FOUND : PXA_STATUS_IO_ERROR;
    *output = home;
    return PXA_STATUS_OK;
}

static void installer_options(const pxa_posix_app_store_config_t *config,const char *root,
    pxa_posix_installer_config_t *options) {
    *options=config->installer;
    options->struct_size = sizeof(*options);
    options->storage_root = root;
    options->flags |= PXA_POSIX_INSTALLER_FLAG_APP_HOME |
        PXA_POSIX_INSTALLER_FLAG_COMPOSITE_IDENTITY |
        PXA_POSIX_INSTALLER_FLAG_RETAIN_PREVIOUS;
    options->layout_context = NULL;
    options->resolve_locator = NULL;
    options->incoming_ready = NULL;
}
size_t pxa_posix_app_store_workspace_size(const pxa_posix_app_store_config_t *config) {
    pxa_posix_installer_config_t options;
    if(!config || !config->root)return 0;
    installer_options(config,config->root,&options);
    return pxa_posix_installer_workspace_size(&options);
}
static pxa_status_t begin_installer(const pxa_posix_app_store_config_t *config,
    const char *root, pxa_posix_installer_t **installer, void **workspace) {
    pxa_posix_installer_config_t options;size_t size;pxa_status_t status;
    *workspace=NULL;*installer=NULL;
    installer_options(config,root,&options);
    size = pxa_posix_installer_workspace_size(&options);
    if(!size)return PXA_STATUS_INVALID_ARGUMENT;
    if(config->installer_workspace) {
        if(config->installer_workspace_bytes<size)return PXA_STATUS_RESOURCE_LIMIT;
        *workspace=config->installer_workspace;
    } else {
        if(config->installer_workspace_bytes)return PXA_STATUS_INVALID_ARGUMENT;
#ifdef ESP_PLATFORM
        return PXA_STATUS_UNSUPPORTED;
#else
        *workspace=malloc(size);
        if(!*workspace)return PXA_STATUS_RESOURCE_LIMIT;
#endif
    }
    status = pxa_posix_installer_init(*workspace, size, &options, installer);
    if(status!=PXA_STATUS_OK) {
        if(*workspace!=config->installer_workspace)free(*workspace);
        *workspace=NULL;
    }
    return status;
}

static void end_installer(const pxa_posix_app_store_config_t *config,pxa_posix_installer_t *installer,void *workspace) {
    if (installer) pxa_posix_installer_deinit(installer);
    if(workspace!=config->installer_workspace)free(workspace);
}

static pxa_status_t pending_read(int directory, const pxa_volume_id_t *volume,
    const pxa_posix_install_record_auth_t *auth,pxa_install_record_t *record) {
    uint8_t bytes[PXA_POSIX_INSTALL_RECORD_SEALED_BYTES];
    struct stat before, after;
    size_t offset = 0,size=auth?PXA_POSIX_INSTALL_RECORD_SEALED_BYTES:PXA_INSTALL_RECORD_BYTES;
    size_t legacy=auth?PXA_POSIX_INSTALL_RECORD_SEALED_V1_BYTES:PXA_INSTALL_RECORD_V1_BYTES;
    int file = openat(directory, pending_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (file < 0) return errno == ENOENT ? PXA_STATUS_NOT_FOUND : PXA_STATUS_IO_ERROR;
    if (fstat(file, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_nlink > 1 || (before.st_size != (off_t)size && before.st_size != (off_t)legacy)) {
        close(file); return PXA_STATUS_DENIED;
    }
    size=(size_t)before.st_size;
    while (offset < size) {
        ssize_t count = read(file, bytes + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(file); return PXA_STATUS_IO_ERROR; }
        offset += (size_t)count;
    }
    if (fstat(file, &after) != 0 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size ||
        before.st_mtime != after.st_mtime) { close(file); return PXA_STATUS_DENIED; }
    if (close(file) != 0) return PXA_STATUS_IO_ERROR;
    return pxa_posix_install_record_unpack((pxa_bytes_t){bytes,size},volume,auth,record);
}

static pxa_status_t pending_write(int directory, const pxa_install_record_t *record,
    const pxa_posix_install_record_auth_t *auth) {
    uint8_t bytes[PXA_POSIX_INSTALL_RECORD_SEALED_BYTES];
    size_t offset = 0,size=0;
    pxa_status_t status = pxa_posix_install_record_pack(record,auth,bytes,&size);
    int file;
    if (status != PXA_STATUS_OK) return status;
    file = openat(directory, pending_name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0) return PXA_STATUS_IO_ERROR;
    while (offset < size) {
        ssize_t count = write(file, bytes + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { status = PXA_STATUS_IO_ERROR; break; }
        offset += (size_t)count;
    }
    if (status == PXA_STATUS_OK && fsync(file) != 0) status = PXA_STATUS_IO_ERROR;
    if (close(file) != 0) status = PXA_STATUS_IO_ERROR;
    if (status == PXA_STATUS_OK && fsync(directory) != 0) status = PXA_STATUS_IO_ERROR;
    return status;
}

static pxa_status_t pending_remove(int directory) {
    if (unlinkat(directory, pending_name, 0) != 0 && errno != ENOENT)
        return PXA_STATUS_IO_ERROR;
    return fsync(directory) == 0 ? PXA_STATUS_OK : PXA_STATUS_IO_ERROR;
}

static int same_identity(const pxa_install_record_t *a, const pxa_install_record_t *b) {
    return !memcmp(a->install_id, b->install_id, sizeof(a->install_id)) &&
        !memcmp(a->publisher_root, b->publisher_root, sizeof(a->publisher_root)) &&
        !strcmp(a->app_id, b->app_id);
}

static int same_record(const pxa_install_record_t *a, const pxa_install_record_t *b) {
    uint8_t first[PXA_INSTALL_RECORD_BYTES], second[PXA_INSTALL_RECORD_BYTES];
    return pxa_install_record_encode(a, first) == PXA_STATUS_OK &&
        pxa_install_record_encode(b, second) == PXA_STATUS_OK &&
        !memcmp(first, second, sizeof(first));
}

static pxa_status_t manifest_digest(pxa_posix_installer_result_t *result, uint8_t digest[32]) {
    uint64_t size;
    if (!result || !result->manifest || !*result->manifest || !result->encoded ||
        result->encoded_capacity < 12) return PXA_STATUS_INVALID_ARGUMENT;
    size = (uint64_t)pxa_read_u32(result->encoded + 8) + 12;
    if (size > result->encoded_capacity) return PXA_STATUS_DENIED;
    return pxa_openssl_sha256(result->encoded, (size_t)size, digest);
}

static pxa_status_t matches_manifest(const pxa_install_record_t *record,
    pxa_posix_installer_result_t *result) {
    uint8_t digest[32];
    pxa_package_publisher_lineage_t lineage;
    const pxa_package_manifest_t *manifest = *result->manifest;
    pxa_status_t status = manifest_digest(result, digest);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_package_publisher_lineage_parse(manifest, &lineage);
    if (status != PXA_STATUS_OK) return status;
    return !memcmp(record->manifest_sha256, digest, sizeof(digest)) &&
        !memcmp(record->publisher_root, lineage.root_key_id, 32) &&
        strlen(record->app_id) == manifest->app_id.size &&
        !memcmp(record->app_id, manifest->app_id.data, manifest->app_id.size)
        ? PXA_STATUS_OK : PXA_STATUS_DENIED;
}

static pxa_status_t resources_ready(const pxa_posix_app_store_config_t *config,
    const pxa_install_record_t *record) {
    if (!(record->flags & PXA_INSTALL_RECORD_HAS_MEDIA)) return PXA_STATUS_OK;
    return config->media_ready ? config->media_ready(config->media_context, record)
        : PXA_STATUS_UNAVAILABLE;
}

static pxa_status_t recover_with_replacement(const pxa_posix_app_store_config_t *config,
    const char *locator, pxa_posix_installer_result_t *result, pxa_posix_app_home_t *output,
    const pxa_install_record_t *replacement) {
    pxa_posix_app_home_t *home=output;
    pxa_install_record_t pending, current;
    pxa_posix_installer_identity_t identity;
    pxa_posix_installer_t *installer = NULL;
    void *workspace = NULL;
    int directory = -1;
    pxa_status_t status, have_pending, have_current, code;
    pxa_posix_install_record_auth_t auth_storage;const pxa_posix_install_record_auth_t *auth;
    int corrupt_pending;
    if (!result || !output) return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    status = paths(config, locator, home);
    if (status != PXA_STATUS_OK) {memset(output,0,sizeof(*output));return status;}
    auth=record_auth(config,locator,&auth_storage);
    status = open_home(config, locator, 0, &directory);
    if (status != PXA_STATUS_OK) {memset(output,0,sizeof(*output));return status;}
    have_current = pxa_posix_install_record_load_with_auth(directory, &config->volume_id, auth, &current);
    have_pending = pending_read(directory, &config->volume_id, auth, &pending);
    corrupt_pending = have_pending == PXA_STATUS_DENIED && have_current == PXA_STATUS_OK;
    if(have_current!=PXA_STATUS_OK && have_current!=PXA_STATUS_NOT_FOUND) {
        status=have_current;goto done;
    }
    if(have_pending!=PXA_STATUS_OK && have_pending!=PXA_STATUS_NOT_FOUND && !corrupt_pending) {
        status=have_pending;goto done;
    }
    if (have_current != PXA_STATUS_OK && have_pending != PXA_STATUS_OK) {
        status = PXA_STATUS_NOT_FOUND; goto done;
    }
    if (have_pending == PXA_STATUS_OK) {
        if ((have_current == PXA_STATUS_OK &&
             (!same_identity(&current, &pending) || current.data_generation!=pending.data_generation || current.generation == UINT64_MAX ||
              (pending.generation != current.generation && pending.generation != current.generation + 1) ||
              (pending.generation == current.generation && !same_record(&current, &pending)))) ||
            (have_current == PXA_STATUS_NOT_FOUND && pending.generation != 1)) {
            status = PXA_STATUS_DENIED; goto done;
        }
        home->record = pending;
    } else home->record = current;
    if (strcmp(strchr(locator, '/') + 1, home->record.app_id)) {
        status = PXA_STATUS_DENIED; goto done;
    }
    status = begin_installer(config, home->home, &installer, &workspace);
    if (status != PXA_STATUS_OK) goto done;
    status = pxa_posix_installer_recover_pending(installer);
    if (status != PXA_STATUS_OK) goto done;
    identity.publisher_key_id = (pxa_bytes_t){home->record.publisher_root, 32};
    identity.app_id = (pxa_bytes_t){(const uint8_t *)home->record.app_id, strlen(home->record.app_id)};
    code = pxa_posix_installer_load_current(installer, &identity, result);
    if (have_pending == PXA_STATUS_OK && code == PXA_STATUS_OK &&
        matches_manifest(&pending, result) == PXA_STATUS_OK) {
        status = resources_ready(config, &pending);
        if (status == PXA_STATUS_OK && config->prepare_home)
            status = config->prepare_home(config->prepare_context, home);
        if (status == PXA_STATUS_OK) status = resources_ready(config, &pending);
        if (status == PXA_STATUS_OK && config->commit_ready &&
            (have_current == PXA_STATUS_NOT_FOUND || pending.generation != current.generation)) {
            status = config->commit_ready(config->commit_context, home);
            if (status == PXA_STATUS_OK) status = resources_ready(config, &pending);
        }
        if (status != PXA_STATUS_OK) {
            /* Before metadata commits, restore previous code if resource or
             * private-state preparation fails. Never rewind committed code. */
            if (have_current == PXA_STATUS_NOT_FOUND || pending.generation != current.generation) {
                pxa_status_t rolled_back = pxa_posix_installer_finish(installer, &identity, 1);
                if (rolled_back == PXA_STATUS_OK) rolled_back = pending_remove(directory);
                if (rolled_back != PXA_STATUS_OK) status = rolled_back;
            }
            goto done;
        }
        if (have_current == PXA_STATUS_NOT_FOUND || pending.generation != current.generation) {
            status = pxa_posix_install_record_commit_with_auth(directory,
                have_current == PXA_STATUS_OK ? current.generation : 0, &pending,auth);
            if (status != PXA_STATUS_OK) goto done;
        }
        status = pxa_posix_install_record_mirror(directory,&pending,auth);
        if(status!=PXA_STATUS_OK)goto done;
        status = pxa_posix_installer_finish(installer, &identity, 0);
        if (status == PXA_STATUS_OK) status = pending_remove(directory);
        goto done;
    }
    if (have_current == PXA_STATUS_OK &&
        ((code == PXA_STATUS_OK && matches_manifest(&current, result) == PXA_STATUS_OK) ||
         (code == PXA_STATUS_NOT_FOUND && (current.flags & PXA_INSTALL_RECORD_ARCHIVED)))) {
        home->record = current;
        status=(have_pending==PXA_STATUS_OK || corrupt_pending)?
            pxa_posix_install_record_mirror(directory,&current,auth):PXA_STATUS_OK;
        if(status==PXA_STATUS_OK)status = pxa_posix_installer_finish(installer, &identity, 0);
        if (status == PXA_STATUS_OK && (have_pending == PXA_STATUS_OK || corrupt_pending))
            status = pending_remove(directory);
        /* An authenticated update may replace an unavailable old resource
         * generation. Its new binding was verified before entering recovery.
         * Still recover/verify all code and pending transactions normally. */
        int replacing_media = replacement && current.generation != UINT64_MAX &&
            replacement->generation == current.generation + 1 && same_identity(&current, replacement) &&
            current.data_generation == replacement->data_generation &&
            (memcmp(current.media_volume.bytes, replacement->media_volume.bytes, 16) ||
             memcmp(current.media_set_sha256, replacement->media_set_sha256, 32) ||
             ((current.flags ^ replacement->flags) & PXA_INSTALL_RECORD_HAS_MEDIA));
        if (status == PXA_STATUS_OK && !(current.flags & PXA_INSTALL_RECORD_ARCHIVED) && !replacing_media)
            status = resources_ready(config, &current);
    } else if (have_current == PXA_STATUS_NOT_FOUND && code == PXA_STATUS_NOT_FOUND) {
        status = pxa_posix_installer_finish(installer, &identity, 0);
        if (status == PXA_STATUS_OK && have_pending == PXA_STATUS_OK) status = pending_remove(directory);
        if (status == PXA_STATUS_OK) status = PXA_STATUS_NOT_FOUND;
    } else status = code == PXA_STATUS_UNAVAILABLE ? code : PXA_STATUS_DENIED;
done:
    end_installer(config,installer,workspace);
    if (directory >= 0) close(directory);
    if(status!=PXA_STATUS_OK)memset(output,0,sizeof(*output));
    return status;
}

pxa_status_t pxa_posix_app_store_recover(const pxa_posix_app_store_config_t *config,
    const char *locator, pxa_posix_installer_result_t *result, pxa_posix_app_home_t *output) {
    return recover_with_replacement(config, locator, result, output, NULL);
}

static pxa_status_t repair_uncommitted(const pxa_posix_app_store_config_t *config,
    const pxa_posix_app_home_t *home, int directory, const pxa_install_record_t *approved,
    pxa_posix_installer_result_t *result) {
    pxa_install_record_t record;
    pxa_posix_installer_t *installer = NULL;
    void *workspace = NULL;
    pxa_status_t status;
    /* A partial first pending write precedes any executable mutation. Only an
     * explicitly approved, authenticated source may repair that empty home.
     * Corrupt committed records are preserved for explicit recovery. */
    pxa_posix_install_record_auth_t auth_storage;
    const pxa_posix_install_record_auth_t *auth=record_auth(config,home->locator,&auth_storage);
    if (pxa_posix_install_record_load_with_auth(directory, &config->volume_id, auth, &record) != PXA_STATUS_NOT_FOUND ||
        pending_read(directory, &config->volume_id, auth, &record) != PXA_STATUS_DENIED)
        return PXA_STATUS_DENIED;
    status = begin_installer(config, home->home, &installer, &workspace);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_posix_installer_recover_pending(installer);
    if (status == PXA_STATUS_OK) {
        pxa_posix_installer_identity_t identity = {{approved->publisher_root, 32},
            {(const uint8_t *)approved->app_id, strlen(approved->app_id)}};
        status = pxa_posix_installer_load_current(installer, &identity, result);
        if (status == PXA_STATUS_OK || status == PXA_STATUS_NOT_FOUND)
            status = pending_remove(directory);
    }
    end_installer(config,installer,workspace);
    return status;
}

/* Keep signed-candidate inspection out of the later FAT/LittleFS handoff
 * stack. Its lineage snapshot is needed only during this first phase. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static pxa_status_t inspect_candidate(const pxa_posix_app_store_config_t *config,
    const char *source,pxa_install_record_t *next,pxa_posix_installer_result_t *result) {
    pxa_package_publisher_lineage_t lineage;
    pxa_posix_installer_t *installer=NULL;void *workspace=NULL;pxa_status_t status;
    status = begin_installer(config, config->root, &installer, &workspace);
    if (status != PXA_STATUS_OK) goto done;
    status = pxa_posix_installer_read_signed_manifest(installer, source, result);
    if (status != PXA_STATUS_OK) goto done;
    const pxa_package_manifest_t *candidate=*result->manifest;
    if(candidate->package_kind==PXA_PACKAGE_KIND_RESOURCES) {status=PXA_STATUS_UNSUPPORTED;goto done;}
    /* A local binding to mutable manual media is independent of publisher
     * resource declarations. Only MANAGED_MEDIA represents a signed bundle;
     * neither an old manifest nor a manual binding may impersonate one. */
    if((candidate->resource_manifest_sha256!=NULL)!=!!(next->flags&PXA_INSTALL_RECORD_MANAGED_MEDIA) ||
        (candidate->resource_manifest_sha256 && (!(next->flags&PXA_INSTALL_RECORD_HAS_MEDIA) ||
         memcmp(candidate->resource_manifest_sha256,next->media_set_sha256,32)))) {
        status=PXA_STATUS_DENIED;goto done;
    }
    status = pxa_package_publisher_lineage_parse(*result->manifest, &lineage);
    if (status != PXA_STATUS_OK) goto done;
    if (memcmp(next->publisher_root, lineage.root_key_id, 32) ||
        strlen(next->app_id) != (*result->manifest)->app_id.size ||
        memcmp(next->app_id, (*result->manifest)->app_id.data, strlen(next->app_id))) {
        status = PXA_STATUS_DENIED; goto done;
    }
    next->release_sequence = (*result->manifest)->has_release_sequence ?
        (*result->manifest)->release_sequence : 0;
    status = manifest_digest(result, next->manifest_sha256);
    if (status != PXA_STATUS_OK) goto done;
    if(config->approved_manifest_sha256 && memcmp(config->approved_manifest_sha256,next->manifest_sha256,32)) {
        status=PXA_STATUS_DENIED;goto done;
    }
done:
    end_installer(config,installer,workspace);return status;
}

pxa_status_t pxa_posix_app_store_install(const pxa_posix_app_store_config_t *config,
    const char *locator, const char *source, uint64_t expected,
    const pxa_install_record_t *approved, pxa_posix_installer_result_t *result,
    pxa_posix_app_home_t *output) {
    pxa_posix_app_home_t *home=output;
    pxa_install_record_t next;
    pxa_posix_installer_t *installer = NULL;
    void *workspace = NULL;
    int directory = -1;
    pxa_status_t status;
    if (!approved || !source || !result || !output || expected == UINT64_MAX ||
        !memchr(approved->app_id, 0, sizeof(approved->app_id)) ||
        (approved->flags & PXA_INSTALL_RECORD_ARCHIVED))
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(output, 0, sizeof(*output));
    status = paths(config, locator, home);
    if (status != PXA_STATUS_OK) return status;
    if (strcmp(strchr(locator, '/') + 1, approved->app_id) ||
        memcmp(&approved->app_volume, &config->volume_id, sizeof(config->volume_id))) {
        memset(output,0,sizeof(*output));return PXA_STATUS_DENIED;
    }
    next = *approved;
    next.generation = expected + 1;
    status=inspect_candidate(config,source,&next,result);
    if(status!=PXA_STATUS_OK)goto done;
    status=resources_ready(config,&next);
    if(status!=PXA_STATUS_OK)goto done;
    status = open_home(config, locator, 1, &directory);
    if (status != PXA_STATUS_OK) goto done;
    status = recover_with_replacement(config, locator, result, home, &next);
    if(status==PXA_STATUS_DENIED && !expected) {
        /* Recovery clears unsuccessful output. Rebuild only the canonical
         * home prefix for the bounded uncommitted-new-home repair. */
        pxa_status_t located=paths(config,locator,home);
        if(located==PXA_STATUS_OK && repair_uncommitted(config,home,directory,&next,result)==PXA_STATUS_OK)
            status=PXA_STATUS_NOT_FOUND;
    }
    if (status == PXA_STATUS_OK) {
        if (home->record.generation != expected || !same_identity(&home->record, &next) ||
            home->record.data_generation!=next.data_generation) {
            status = PXA_STATUS_BAD_STATE; goto done;
        }
        /* Recovery already authenticated the installed bytes. Repeating the
         * identical approved release/policy needs no code copy or record
         * writes, and must not open/rebuild its private filesystem. */
        next.generation=home->record.generation;
        if(same_record(&home->record,&next)) {
            status=begin_installer(config,home->home,&installer,&workspace);
            if(status==PXA_STATUS_OK)status=pxa_posix_installer_verify_source(installer,source,result);
            if(status==PXA_STATUS_OK)status=pxa_posix_app_home_path(home,PXA_POSIX_APP_HOME_PACKAGE,result->root,result->root_capacity);
            goto done;
        }
        next.generation=expected+1;
    } else if (status != PXA_STATUS_NOT_FOUND || expected) {
        if (status == PXA_STATUS_NOT_FOUND) status = PXA_STATUS_BAD_STATE;
        goto done;
    }
    /* NOT_FOUND recovery also leaves output empty. Reuse the caller-owned
     * snapshot throughout the remaining handoff instead of stacking two
     * additional kilobyte-sized copies above FAT/SPI driver calls. */
    status=paths(config,locator,home);
    if(status!=PXA_STATUS_OK)goto done;
    {
        pxa_posix_install_record_auth_t auth_storage;
        status = pending_write(directory, &next,record_auth(config,locator,&auth_storage));
    }
    if (status != PXA_STATUS_OK) goto done;
    status = begin_installer(config, home->home, &installer, &workspace);
    if (status != PXA_STATUS_OK) goto done;
    pxa_posix_installer_identity_t identity = {
        {next.publisher_root, 32}, {(const uint8_t *)next.app_id, strlen(next.app_id)}};
    pxa_posix_install_disposition_t disposition;
    status = config->adopt_source_directory
        ? pxa_posix_installer_adopt_directory(installer, source, &identity, result, &disposition)
        : pxa_posix_installer_install_for_identity(installer, source, &identity, result, &disposition);
    if (status != PXA_STATUS_OK) goto done;
    end_installer(config,installer,workspace); installer = NULL; workspace = NULL;
    status = pxa_posix_app_store_recover(config, locator, result, output);
done:
    end_installer(config,installer,workspace);
    if (directory >= 0) close(directory);
    if(status!=PXA_STATUS_OK)memset(output,0,sizeof(*output));
    return status;
}

typedef struct {
    pxa_posix_app_home_t home;
    pxa_posix_install_record_auth_t auth_storage;
    const pxa_posix_install_record_auth_t *auth;
    int directory;
} local_change_t;
static pxa_status_t read_maintenance_home(const pxa_posix_app_store_config_t *config,
    const char *locator,uint64_t expected,pxa_posix_app_home_t *home,
    pxa_posix_install_record_auth_t *auth_storage,int *directory) {
    pxa_install_record_t pending;*directory=-1;
    pxa_status_t status=paths(config,locator,home);
    if(status!=PXA_STATUS_OK)return status;
    const pxa_posix_install_record_auth_t *auth=record_auth(config,locator,auth_storage);
    status=open_home(config,locator,0,directory);if(status!=PXA_STATUS_OK)return status;
    status=pxa_posix_install_record_load_with_auth(*directory,&config->volume_id,auth,&home->record);
    if(status!=PXA_STATUS_OK)goto failed;
    if(expected!=UINT64_MAX && home->record.generation!=expected) {status=PXA_STATUS_BAD_STATE;goto failed;}
    if(strcmp(strchr(locator,'/')+1,home->record.app_id)) {status=PXA_STATUS_DENIED;goto failed;}
    status=pending_read(*directory,&config->volume_id,auth,&pending);
    if(status==PXA_STATUS_NOT_FOUND)return PXA_STATUS_OK;
    if(status==PXA_STATUS_OK)status=PXA_STATUS_BUSY;
failed:
    close(*directory);*directory=-1;return status;
}
static pxa_status_t begin_change(const pxa_posix_app_store_config_t *config,
    const char *locator,uint64_t expected,local_change_t *change) {
    memset(change,0,sizeof(*change));
    pxa_status_t status=read_maintenance_home(config,locator,expected,&change->home,&change->auth_storage,&change->directory);
    if(status==PXA_STATUS_OK)change->auth=config->record_tag?&change->auth_storage:NULL;
    return status;
}
pxa_status_t pxa_posix_app_store_lookup(const pxa_posix_app_store_config_t *config,
    const char *locator,pxa_posix_app_home_t *output) {
    pxa_posix_install_record_auth_t auth_storage;int directory=-1;
    if(!output)return PXA_STATUS_INVALID_ARGUMENT;
    memset(output,0,sizeof(*output));
    pxa_status_t status=read_maintenance_home(config,locator,UINT64_MAX,output,&auth_storage,&directory);
    if(status==PXA_STATUS_OK)close(directory);
    else memset(output,0,sizeof(*output));
    return status;
}
pxa_status_t pxa_posix_app_store_set_enabled(const pxa_posix_app_store_config_t *config,
    const char *locator,uint64_t expected,uint8_t enabled,pxa_posix_app_home_t *output) {
    local_change_t change;
    if(output)memset(output,0,sizeof(*output));
    if(!output || enabled>1 || expected==UINT64_MAX)return PXA_STATUS_INVALID_ARGUMENT;
    pxa_status_t status=begin_change(config,locator,expected,&change);
    if(status!=PXA_STATUS_OK)return status;
    if(!!(change.home.record.flags&PXA_INSTALL_RECORD_ENABLED)!=enabled) {
        change.home.record.flags=(change.home.record.flags&~PXA_INSTALL_RECORD_ENABLED)|(enabled?PXA_INSTALL_RECORD_ENABLED:0);
        ++change.home.record.generation;
        status=pxa_posix_install_record_commit_with_auth(change.directory,expected,&change.home.record,change.auth);
    }
    if(status==PXA_STATUS_OK) {
        *output=change.home;
        status=pxa_posix_install_record_mirror(change.directory,&change.home.record,change.auth);
    }
    close(change.directory);return status;
}
pxa_status_t pxa_posix_app_store_rebind_media(const pxa_posix_app_store_config_t *config,
    const char *locator,const pxa_install_record_t *approved,
    const pxa_volume_id_t *target,pxa_posix_app_home_t *output) {
    local_change_t change;
    if(output)memset(output,0,sizeof(*output));
    if(!config || !approved || !target || !output || !approved->generation ||
        approved->generation==UINT64_MAX)return PXA_STATUS_INVALID_ARGUMENT;
    if(!config->media_ready)return PXA_STATUS_UNSUPPORTED;
    pxa_status_t status=begin_change(config,locator,approved->generation,&change);
    if(status!=PXA_STATUS_OK)return status;
    if(!same_record(&change.home.record,approved)) {status=PXA_STATUS_BAD_STATE;goto done;}
    const uint32_t managed=PXA_INSTALL_RECORD_HAS_MEDIA|PXA_INSTALL_RECORD_MANAGED_MEDIA;
    if((approved->flags&managed)!=managed || (approved->flags&PXA_INSTALL_RECORD_ARCHIVED) ||
        !memcmp(target->bytes,(uint8_t[16]){0},16)) {status=PXA_STATUS_DENIED;goto done;}
    change.home.record.media_volume=*target;
    status=config->media_ready(config->media_context,&change.home.record);
    if(status!=PXA_STATUS_OK)goto done;
    if(memcmp(approved->media_volume.bytes,target->bytes,16)) {
        ++change.home.record.generation;
        status=pxa_posix_install_record_commit_with_auth(change.directory,approved->generation,
            &change.home.record,change.auth);
    }
    if(status==PXA_STATUS_OK) {
        *output=change.home;
        status=pxa_posix_install_record_mirror(change.directory,&change.home.record,change.auth);
    }
done:
    close(change.directory);return status;
}
pxa_status_t pxa_posix_app_store_reset_data(const pxa_posix_app_store_config_t *config,
    const char *locator,uint64_t expected,uint64_t expected_data,pxa_posix_app_home_t *output) {
    local_change_t change;
    if(output)memset(output,0,sizeof(*output));
    if(!config || !output || expected==UINT64_MAX || expected_data==UINT64_MAX)return PXA_STATUS_INVALID_ARGUMENT;
    if(!config->prepare_home)return PXA_STATUS_UNSUPPORTED;
    pxa_status_t status=begin_change(config,locator,expected,&change);if(status!=PXA_STATUS_OK)return status;
    if(change.home.record.data_generation!=expected_data) {status=PXA_STATUS_BAD_STATE;goto done;}
    ++change.home.record.data_generation;++change.home.record.generation;
    status=config->prepare_home(config->prepare_context,&change.home);
    if(status==PXA_STATUS_OK)status=pxa_posix_install_record_commit_with_auth(change.directory,expected,&change.home.record,change.auth);
    if(status==PXA_STATUS_OK) {
        *output=change.home; /* The commit is known durable even if mirroring fails. */
        status=pxa_posix_install_record_mirror(change.directory,&change.home.record,change.auth);
    }
done:
    close(change.directory);return status;
}

pxa_status_t pxa_posix_app_store_archive(const pxa_posix_app_store_config_t *config,
    const char *locator,uint64_t expected,pxa_posix_app_home_t *output) {
    local_change_t change;pxa_posix_installer_t *installer=NULL;void *workspace=NULL;
    if(output)memset(output,0,sizeof(*output));
    if(!output || expected==UINT64_MAX)return PXA_STATUS_INVALID_ARGUMENT;
    pxa_status_t status=begin_change(config,locator,expected,&change);
    if(status!=PXA_STATUS_OK)return status;
    if(!(change.home.record.flags&PXA_INSTALL_RECORD_ARCHIVED)) {
        change.home.record.flags|=PXA_INSTALL_RECORD_ARCHIVED;++change.home.record.generation;
        status=pxa_posix_install_record_commit_with_auth(change.directory,expected,&change.home.record,change.auth);
    }
    if(status==PXA_STATUS_OK) {
        *output=change.home;
        status=pxa_posix_install_record_mirror(change.directory,&change.home.record,change.auth);
    }
    if(status==PXA_STATUS_OK)status=begin_installer(config,change.home.home,&installer,&workspace);
    if(status==PXA_STATUS_OK) {
        pxa_posix_installer_identity_t identity={{change.home.record.publisher_root,32},
            {(const uint8_t *)change.home.record.app_id,strlen(change.home.record.app_id)}};
        status=pxa_posix_installer_uninstall(installer,&identity);
    }
    end_installer(config,installer,workspace);close(change.directory);return status;
}

pxa_status_t pxa_posix_app_store_visit(const pxa_posix_app_store_config_t *config,
    pxa_posix_app_home_visitor_fn visitor, void *context, size_t *invalid) {
    int root, apps,scan;
    DIR *publishers;
    struct dirent *publisher;
    pxa_status_t status = PXA_STATUS_OK;
    if (!config || !config->root || !visitor || !invalid) return PXA_STATUS_INVALID_ARGUMENT;
    *invalid = 0;
    root = open(config->root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root < 0) return PXA_STATUS_UNAVAILABLE;
    apps = child_directory(root, "apps", 0); close(root);
    if (apps < 0) return errno == ENOENT ? PXA_STATUS_OK : PXA_STATUS_IO_ERROR;
    scan=dup(apps);
    if(scan<0) {close(apps);return PXA_STATUS_IO_ERROR;}
    publishers = fdopendir(scan);
    if (!publishers) {close(scan);close(apps); return PXA_STATUS_IO_ERROR;}
    while ((publisher = readdir(publishers)) != NULL) {
        DIR *applications;
        struct dirent *app;
        int publisher_fd;
        char name[65];
        if (!safe_publisher(publisher->d_name, strlen(publisher->d_name))) continue;
        strcpy(name, publisher->d_name);
        publisher_fd = child_directory(apps, name, 0);
        if (publisher_fd < 0) { ++*invalid; continue; }
        scan=dup(publisher_fd);
        if(scan<0) {close(publisher_fd);++*invalid;continue;}
        applications = fdopendir(scan);
        if (!applications) {close(scan);close(publisher_fd); ++*invalid; continue;}
        while ((app = readdir(applications)) != NULL) {
            pxa_posix_app_home_t home;
            pxa_posix_install_record_auth_t auth_storage;
            char locator[PXA_POSIX_APP_LOCATOR_BYTES];
            int directory;
            if (!safe_component(app->d_name, strlen(app->d_name))) continue;
            snprintf(locator, sizeof(locator), "%.64s/%.64s", name, app->d_name);
            if(config->map_locator) {
                char logical[PXA_POSIX_APP_LOCATOR_BYTES];
                status=config->map_locator(config->locator_context,locator,1,logical,sizeof(logical));
                if(status!=PXA_STATUS_OK) {++*invalid;continue;}
                strcpy(locator,logical);
            }
            status = paths(config, locator, &home);
            if (status != PXA_STATUS_OK) { ++*invalid; continue; }
            directory = child_directory(publisher_fd, app->d_name, 0);
            if (directory < 0) { ++*invalid; continue; }
            status = pxa_posix_install_record_load_with_auth(directory,&config->volume_id,
                record_auth(config,locator,&auth_storage),&home.record);
            close(directory);
            if (status == PXA_STATUS_NOT_FOUND) continue;
            if(status==PXA_STATUS_UNAVAILABLE || status==PXA_STATUS_RESOURCE_LIMIT || status==PXA_STATUS_INTERNAL) {
                closedir(applications);close(publisher_fd);closedir(publishers);close(apps);return status;
            }
            if (status != PXA_STATUS_OK || strcmp(home.record.app_id, app->d_name)) {
                ++*invalid; continue;
            }
            if (!visitor(context, &home)) {
                closedir(applications);close(publisher_fd);closedir(publishers);close(apps);return PXA_STATUS_OK;
            }
        }
        closedir(applications);
        close(publisher_fd);
    }
    closedir(publishers);
    close(apps);
    return PXA_STATUS_OK;
}

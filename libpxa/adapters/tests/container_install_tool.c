#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/resource.h>

#include "pxa/posix/pxa_posix_installer.h"
#include "../image/vendor/miniz.h"

#ifdef PXA_TEST_WRAP_ALLOC
void *__real_malloc(size_t size);
void *__real_realloc(void *ptr, size_t size);
static size_t denied_allocation_size;
void *__wrap_malloc(size_t size) {
    if(denied_allocation_size && size==denied_allocation_size)return NULL;
    return __real_malloc(size);
}
void *__wrap_realloc(void *ptr, size_t size) {
    if(denied_allocation_size && size==denied_allocation_size)return NULL;
    return __real_realloc(ptr,size);
}
#endif

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) <= 0 || length > 4096 ||
        fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

int main(int argc, char **argv) {
    pxa_openssl_publisher_key_t key;
    pxa_posix_installer_config_t config;
    pxa_posix_installer_t *installer = NULL;
    pxa_posix_installer_result_t result;
    pxa_package_manifest_t *manifest = NULL;
    pxa_posix_install_disposition_t disposition;
    uint8_t *public_key;
    uint8_t *installer_workspace;
    uint8_t *manifest_workspace;
    uint8_t *encoded;
    size_t public_key_size = 0;
    size_t installer_workspace_size;
    size_t manifest_workspace_size;
    char installed_root[512];
    int index;
    int exit_code = 1;
    if (argc < 4) return 2;
    public_key = read_file(argv[1], &public_key_size);
    if (public_key == NULL) return 2;
    key.spki = public_key;
    key.spki_size = public_key_size;
    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.storage_root = argv[2];
    config.trust.struct_size = sizeof(config.trust);
    config.trust.keys = &key;
    config.trust.key_count = 1;
    const char *binding=getenv("PXA_TEST_RESOURCE_MANIFEST");uint8_t digest[32];
    if(binding) {
        if(strlen(binding)!=64)return 2;
        for(unsigned i=0;i<32;++i) {
            unsigned byte;
            if(sscanf(binding+i*2,"%2x",&byte)!=1)return 2;
            digest[i]=(uint8_t)byte;
        }
        config.expected_manifest_sha256=digest;
        config.expected_package_kind=PXA_PACKAGE_KIND_RESOURCES;
    }
    pxa_package_limits_init(&config.limits);
    installer_workspace_size = pxa_posix_installer_workspace_size(&config);
    manifest_workspace_size = pxa_package_manifest_workspace_size(&config.limits);
    installer_workspace = malloc(installer_workspace_size);
    manifest_workspace = malloc(manifest_workspace_size);
    encoded = malloc(PXA_PACKAGE_TEST_MANIFEST_BYTES);
    if (installer_workspace == NULL || manifest_workspace == NULL ||
        encoded == NULL ||
        pxa_posix_installer_init(installer_workspace,
                                 installer_workspace_size, &config,
                                 &installer) != PXA_STATUS_OK) {
        goto done;
    }
    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    result.manifest_workspace = manifest_workspace;
    result.manifest_workspace_size = manifest_workspace_size;
    result.encoded = encoded;
    result.encoded_capacity = PXA_PACKAGE_TEST_MANIFEST_BYTES;
    result.manifest = &manifest;
    result.root = installed_root;
    result.root_capacity = sizeof(installed_root);
    for (index = 3; index < argc; ++index) {
        pxa_status_t status=PXA_STATUS_OK;
        const char *fragment=getenv("PXA_TEST_STREAM_FRAGMENT");
        char staged[512];const char *source=argv[index];
        if(fragment) {
            pxa_posix_container_stream_t *stream=NULL;
            size_t chunk=(size_t)strtoul(fragment,NULL,10);
            if(!chunk || chunk>65536)goto done;
            uint8_t *buffer=malloc(chunk);FILE *input=fopen(source,"rb");
            snprintf(staged,sizeof(staged),"%s/stream-%d",argv[2],index);
            if(!buffer || !input) {free(buffer);if(input)fclose(input);goto done;}
            const char *write_limit=getenv("PXA_TEST_STREAM_WRITE_LIMIT");
            if(write_limit) {
                struct rlimit limit={(rlim_t)strtoul(write_limit,NULL,10),(rlim_t)strtoul(write_limit,NULL,10)};
                signal(SIGXFSZ,SIG_IGN);if(setrlimit(RLIMIT_FSIZE,&limit))goto done;
            }
            status=pxa_posix_container_stream_begin(installer,staged,&stream);
#ifdef PXA_TEST_WRAP_ALLOC
            const char *deny_size=getenv("PXA_TEST_STREAM_DENY_ALLOC");
            denied_allocation_size=deny_size
                ?(!strcmp(deny_size,"inflate")?sizeof(tinfl_decompressor):(size_t)strtoul(deny_size,NULL,10)):0;
#endif
            size_t amount;
            while(status==PXA_STATUS_OK && (amount=fread(buffer,1,chunk,input)))
                status=pxa_posix_container_stream_feed(stream,buffer,amount);
            if(ferror(input))status=PXA_STATUS_IO_ERROR;
            if(status==PXA_STATUS_OK)status=pxa_posix_container_stream_finish(stream);
            pxa_posix_container_stream_close(stream);free(buffer);fclose(input);
#ifdef PXA_TEST_WRAP_ALLOC
            denied_allocation_size=0;
#endif
            source=staged;
        }
        if(status==PXA_STATUS_OK)status=getenv("PXA_TEST_STREAM_ADOPT")
            ?pxa_posix_installer_adopt_directory(installer,source,NULL,&result,&disposition)
            :pxa_posix_installer_install(installer,source,&result,&disposition);
        if (status != PXA_STATUS_OK) {
            fprintf(stderr, "install failed: %s status=%d\n", argv[index],
                    (int)status);
            goto done;
        }
    }
    exit_code = 0;
done:
    pxa_posix_installer_deinit(installer);
    free(encoded);
    free(manifest_workspace);
    free(installer_workspace);
    free(public_key);
    return exit_code;
}

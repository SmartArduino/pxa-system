#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pxa/openssl/pxa_openssl.h"
#include "pxa/package.h"
#include "pxa/posix/pxa_posix_installer.h"

typedef struct {
    const char *storage_root;
    const char *publisher_key;
    const char *source;
    const char *expected_id;
} options_t;

static void print_usage(const char *program) {
    fprintf(stderr, "Usage: %s --storage-root DIR --publisher-key DER --source PACKAGE --expected-id ID\n",
            program);
}

static int parse_options(int argc, char **argv, options_t *options) {
    int index;
    memset(options, 0, sizeof(*options));
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--storage-root") == 0 && index + 1 < argc)
            options->storage_root = argv[++index];
        else if (strcmp(argv[index], "--publisher-key") == 0 && index + 1 < argc)
            options->publisher_key = argv[++index];
        else if (strcmp(argv[index], "--source") == 0 && index + 1 < argc)
            options->source = argv[++index];
        else if (strcmp(argv[index], "--expected-id") == 0 && index + 1 < argc)
            options->expected_id = argv[++index];
        else
            return 0;
    }
    return options->storage_root != NULL && options->publisher_key != NULL &&
           options->source != NULL && options->expected_id != NULL;
}

static int read_file(const char *path, uint8_t **output, size_t *output_size) {
    FILE *file;
    long size;
    uint8_t *buffer;
    if (path == NULL || output == NULL || output_size == NULL) return 0;
    *output = NULL;
    *output_size = 0;
    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (size = ftell(file)) <= 0 || size > 4096) {
        if (file != NULL) fclose(file);
        return 0;
    }
    rewind(file);
    buffer = malloc((size_t)size);
    if (buffer == NULL || fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        fclose(file);
        return 0;
    }
    fclose(file);
    *output = buffer;
    *output_size = (size_t)size;
    return 1;
}

int main(int argc, char **argv) {
    options_t options;
    pxa_package_limits_t limits;
    pxa_posix_installer_config_t config = {0};
    pxa_posix_installer_t *installer = NULL;
    pxa_posix_installer_result_t package_result = {0};
    pxa_posix_install_disposition_t disposition;
    pxa_posix_installer_identity_t expected = {0};
    pxa_openssl_publisher_key_t key = {0};
    uint8_t *public_key = NULL;
    uint8_t *encoded = NULL;
    void *installer_workspace = NULL;
    void *manifest_workspace = NULL;
    pxa_package_manifest_t *manifest = NULL;
    char root[1024] = {0};
    size_t public_key_size = 0;
    size_t manifest_size = 0;
    size_t manifest_workspace_size;
    pxa_status_t status;
    int result = 1;

    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }
    if (!read_file(options.publisher_key, &public_key, &public_key_size)) {
        fprintf(stderr, "unable to read publisher key: %s\n", options.publisher_key);
        goto done;
    }
    key.spki = public_key;
    key.spki_size = public_key_size;
    pxa_package_limits_init(&limits);
    config.struct_size = sizeof(config);
    config.storage_root = options.storage_root;
    config.trust.struct_size = sizeof(config.trust);
    config.trust.keys = &key;
    config.trust.key_count = 1;
    config.limits = limits;
    installer_workspace = malloc(pxa_posix_installer_workspace_size(&config));
    if (installer_workspace == NULL ||
        pxa_posix_installer_init(installer_workspace,
            pxa_posix_installer_workspace_size(&config), &config, &installer) !=
            PXA_STATUS_OK) {
        fprintf(stderr, "unable to initialize package installer\n");
        goto done;
    }
    status = pxa_posix_installer_source_manifest_size(installer, options.source,
                                                      &manifest_size);
    if (status != PXA_STATUS_OK || manifest_size == 0) {
        fprintf(stderr, "unable to inspect package (status=%d)\n", (int)status);
        goto done;
    }
    encoded = malloc(manifest_size);
    manifest_workspace_size = pxa_package_manifest_workspace_size(&limits);
    manifest_workspace = malloc(manifest_workspace_size);
    if (encoded == NULL || manifest_workspace == NULL) {
        fprintf(stderr, "insufficient memory for package manifest\n");
        goto done;
    }
    package_result.struct_size = sizeof(package_result);
    package_result.manifest_workspace = manifest_workspace;
    package_result.manifest_workspace_size = manifest_workspace_size;
    package_result.encoded = encoded;
    package_result.encoded_capacity = manifest_size;
    package_result.manifest = &manifest;
    package_result.root = root;
    package_result.root_capacity = sizeof(root);
    status = pxa_posix_installer_verify_source(installer, options.source,
                                               &package_result);
    if (status != PXA_STATUS_OK || manifest == NULL ||
        manifest->publisher_key_id == NULL ||
        manifest->app_id.size != strlen(options.expected_id) ||
        memcmp(manifest->app_id.data, options.expected_id,
               manifest->app_id.size) != 0) {
        fprintf(stderr, "package identity does not match the requested app ID\n");
        goto done;
    }
    expected.publisher_key_id =
        (pxa_bytes_t){manifest->publisher_key_id, PXA_PACKAGE_DIGEST_BYTES};
    expected.app_id =
        (pxa_bytes_t){(const uint8_t *)options.expected_id,
                      strlen(options.expected_id)};
    status = pxa_posix_installer_install_for_identity(
        installer, options.source, &expected, &package_result, &disposition);
    if (status != PXA_STATUS_OK || manifest == NULL) {
        fprintf(stderr, "package installation rejected (status=%d)\n", (int)status);
        goto done;
    }
    printf("id=%.*s;root=%s;disposition=%s\n", (int)manifest->app_id.size,
           manifest->app_id.data, root,
           disposition == PXA_POSIX_INSTALL_ALREADY_CURRENT ? "current" : "installed");
    result = 0;
done:
    if (installer != NULL) pxa_posix_installer_deinit(installer);
    free(manifest_workspace);
    free(encoded);
    free(installer_workspace);
    free(public_key);
    return result;
}

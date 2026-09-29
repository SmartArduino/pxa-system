#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#include "pxa/posix/pxa_posix_asset.h"
#include <assert.h>
#include "pxa/openssl/pxa_openssl.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int cancelled(void *context) { (void)context; return 1; }
static pxa_status_t load(const char *root, const pxa_asset_blob_block_t *block,
    uint8_t output[4096], int cancel) {
    pxa_posix_asset_input_t stream;
    pxa_asset_input_t input;
    pxa_status_t status = pxa_posix_asset_open_range(&stream, root,
        block->info.path, block->info.stored_bytes, block->offset, block->bytes, &input);
    if (status) return status;
    input.cancelled = cancel ? cancelled : NULL;
    status = pxa_asset_load_blob_block(block, &input, output, 4096);
    if (!status) assert(stream.read_bytes == block->bytes);
    pxa_posix_asset_close(&stream);
    return status;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    static const char *names[] = {"assets/map.bin", "assets/resources.pxi", "assets/t.pxr", "assets/zero.bin"};
    pxa_package_file_t files[4];
    uint8_t digests[4][32], index[1024], buffer[4096];
    char path[1024]; size_t index_bytes = 0;
    for (unsigned i = 0; i < 4; ++i) {
        snprintf(path, sizeof(path), "%s/%s", argv[1], names[i]);
        FILE *file = fopen(path, "rb"); assert(file);
        pxa_openssl_sha256_stream_t hash = {0};
        assert(!pxa_openssl_sha256_stream_begin(&hash));
        size_t size = 0, count;
        while ((count = fread(buffer, 1, sizeof(buffer), file))) {
            assert(!pxa_openssl_sha256_stream_update(&hash, buffer, count));
            if (i == 1) {
                assert(size + count <= sizeof(index));
                memcpy(index + size, buffer, count); index_bytes += count;
            }
            size += count;
        }
        assert(feof(file) && !ferror(file)); fclose(file);
        assert(!pxa_openssl_sha256_stream_finish(&hash, digests[i]));
        files[i] = (pxa_package_file_t){{(const uint8_t *)names[i],strlen(names[i])},size,digests[i]};
    }
    pxa_package_manifest_t manifest = {0}; manifest.files = files; manifest.file_count = 4;
    pxa_asset_catalog_t catalog;
    pxa_asset_blob_block_t block;
    // Production authenticates the index against this manifest before init.
    assert(!pxa_openssl_sha256(index,index_bytes,buffer) && !memcmp(buffer,digests[1],32));
    assert(!pxa_asset_catalog_init(&catalog,(pxa_bytes_t){index,index_bytes},&manifest));
    for (uint32_t offset = 0; offset < 8193; offset += 4096) {
        assert(!pxa_asset_catalog_blob_block(&catalog,files[0].path,offset,&block));
        assert(!load(argv[1],&block,buffer,0));
        for (uint32_t i = 0; i < block.bytes; ++i) assert(buffer[i] == (offset+i)%251);
    }
    assert(!pxa_asset_catalog_blob_block(&catalog,files[0].path,4097,&block));
    assert(block.offset == 4097 && block.bytes == 4096);
    assert(load(argv[1],&block,buffer,1) == PXA_STATUS_CANCELLED);
    assert(!load(argv[1],&block,buffer,0));
    assert(!pxa_asset_catalog_blob_block(&catalog,files[0].path,8193,&block));
    assert(!load(argv[1],&block,buffer,0) && !block.bytes);
    assert(!pxa_asset_catalog_blob_block(&catalog,files[3].path,0,&block));
    assert(!load(argv[1],&block,buffer,0));
    assert(load(argv[1],&block,buffer,1) == PXA_STATUS_CANCELLED);
    assert(pxa_asset_catalog_blob_block(&catalog,files[2].path,0,&block) == PXA_STATUS_UNSUPPORTED);
    snprintf(path,sizeof(path),"%s/assets/map.bin",argv[1]);
    // Full signed file size is checked even when only the first block is read.
    assert(!truncate(path,8192));
    assert(!pxa_asset_catalog_blob_block(&catalog,files[0].path,0,&block));
    assert(load(argv[1],&block,buffer,0) == PXA_STATUS_DENIED);
    assert(!unlink(path) && !symlink("zero.bin",path));
    assert(load(argv[1],&block,buffer,0) == PXA_STATUS_DENIED);
    assert(!unlink(path));
    assert(load(argv[1],&block,buffer,0) == PXA_STATUS_NOT_FOUND);
    puts("blob input: installed metadata, bounded direct ranges, EOF, cancellation, size and confined open passed");
    return 0;
}

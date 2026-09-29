#undef NDEBUG
#include "pxa/asset_loader.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned allocations, read_calls, yield_calls, cancel_after;
static int fail_allocate, fail_read;
static size_t input_offset, input_size, max_read;
static uint8_t input_bytes[PXA_ASSET_FILE_HEADER_BYTES + 65536 + 1];
static uint8_t digest[32];

static void *allocate(void *context, size_t bytes) {
    void *p;
    if (fail_allocate) return NULL;
    size_t offset = (size_t)(uintptr_t)context;
    p = malloc(bytes + offset);
    assert(p);
    ++allocations;
    return (uint8_t *)p + offset;
}
static void deallocate(void *context, void *p) {
    assert(allocations);
    --allocations;
    free((uint8_t *)p - (size_t)(uintptr_t)context);
}
static pxa_status_t read_input(void *context, uint8_t *output, size_t capacity,
                               size_t *count) {
    (void)context;
    assert(capacity <= 4096);
    ++read_calls;
    if(fail_read) return PXA_STATUS_IO_ERROR;
    if (capacity > max_read) max_read = capacity;
    /* Exercise short successful reads, including a split file header. */
    if (capacity > 777) capacity = 777;
    *count = input_size - input_offset < capacity ? input_size - input_offset : capacity;
    memcpy(output, input_bytes + input_offset, *count);
    input_offset += *count;
    return PXA_STATUS_OK;
}
static int cancelled(void *context) {
    (void)context;
    return cancel_after && yield_calls >= cancel_after;
}
static void yield_input(void *context) { (void)context; ++yield_calls; }

static void file_header(uint16_t width, uint16_t height) {
    memset(input_bytes, 0, sizeof(input_bytes));
    pxa_write_u32(input_bytes, PXA_ASSET_FILE_MAGIC);
    pxa_write_u16(input_bytes + 4, 1);
    pxa_write_u16(input_bytes + 8, PXA_ASSET_TEXTURE);
    pxa_write_u16(input_bytes + 10, PXA_ASSET_ENCODING_INDEX8);
    pxa_write_u16(input_bytes + 12, width);
    pxa_write_u16(input_bytes + 14, height);
    pxa_write_u32(input_bytes + 16, (uint32_t)width * height);
    pxa_write_u32(input_bytes + 20, PXA_ASSET_FILE_HEADER_BYTES);
    input_size = PXA_ASSET_FILE_HEADER_BYTES + (size_t)width * height;
    memset(input_bytes + PXA_ASSET_FILE_HEADER_BYTES, 0x35, input_size - 32);
    input_offset = read_calls = yield_calls = max_read = 0;
}

static void test_blob_blocks(void) {
    static const uint8_t path[] = "assets/map.bin";
    uint8_t index[16 + 24 + 16 + 3 * 32] = {0}, output[4096];
    pxa_package_file_t file = {{path, sizeof(path)-1}, 8193, digest};
    pxa_package_manifest_t manifest = {0}; manifest.files = &file; manifest.file_count = 1;
    pxa_asset_catalog_t catalog;
    pxa_asset_blob_block_t block;
    pxa_asset_input_t input = {NULL, read_input, cancelled, yield_input};
    pxa_write_u32(index, PXA_ASSET_INDEX_MAGIC);
    pxa_write_u16(index+4, 1); pxa_write_u16(index+6, 1);
    pxa_write_u32(index+8, 1); pxa_write_u32(index+12, sizeof(index));
    pxa_write_u16(index+16, sizeof(path)-1); index[18] = PXA_ASSET_BLOB;
    pxa_write_u32(index+24, 8193); pxa_write_u32(index+28, 8193);
    memcpy(index+40, path, sizeof(path)-1);
    assert(!pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index,sizeof(index)}, &manifest));
    assert(!pxa_asset_catalog_blob_block(&catalog, file.path, 4097, &block));
    assert(block.offset == 4097 && block.bytes == 4096);
    input_offset = read_calls = yield_calls = max_read = 0; input_size = 4096;
    memset(input_bytes, 0x92, input_size);
    assert(!pxa_asset_load_blob_block(&block, &input, output, sizeof(output)));
    assert(read_calls == 6 && max_read == 4096 && output[4095] == 0x92);
    input_offset = read_calls = yield_calls = 0; fail_read = 1;
    assert(pxa_asset_load_blob_block(&block, &input, output, sizeof(output)) == PXA_STATUS_IO_ERROR);
    fail_read = 0; input_offset = read_calls = yield_calls = 0; cancel_after = 1;
    assert(pxa_asset_load_blob_block(&block, &input, output, sizeof(output)) == PXA_STATUS_CANCELLED);
    assert(read_calls == 1); cancel_after = 0;
    input_offset = read_calls = yield_calls = 0; input_size = 4095;
    assert(pxa_asset_load_blob_block(&block, &input, output, sizeof(output)) == PXA_STATUS_IO_ERROR);
    assert(!pxa_asset_catalog_blob_block(&catalog, file.path, 8192, &block));
    assert(block.offset == 8192 && block.bytes == 1);
    input_offset = read_calls = yield_calls = 0; input_size = 1;
    assert(!pxa_asset_load_blob_block(&block, &input, output, sizeof(output)) && read_calls == 1);
    assert(!pxa_asset_catalog_blob_block(&catalog, file.path, 8193, &block));
    assert(block.offset == 8193 && !block.bytes);
    assert(!pxa_asset_load_blob_block(&block, &input, NULL, 0) && read_calls == 1);
    assert(pxa_asset_catalog_blob_block(&catalog, file.path, 8194, &block) == PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u16(index+6, PXA_ASSET_INDEX_MINOR+1);
    assert(pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index,sizeof(index)}, &manifest) == PXA_STATUS_UNSUPPORTED);
    pxa_write_u16(index+6, 1); pxa_write_u32(index+12, sizeof(index)-1);
    assert(pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index,sizeof(index)-1}, &manifest) == PXA_STATUS_PROTOCOL_ERROR);
    pxa_write_u32(index+12, sizeof(index));
    pxa_write_u32(index+24, UINT32_MAX); pxa_write_u32(index+28, UINT32_MAX); file.size = UINT32_MAX;
    assert(pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index,sizeof(index)}, &manifest) == PXA_STATUS_PROTOCOL_ERROR);
    pxa_write_u32(index+24, 8193); pxa_write_u32(index+28, 8193); file.size = 8193;
    pxa_write_u16(index+6, 0); pxa_write_u32(index+12, 56);
    assert(!pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index,56}, &manifest));
    assert(!pxa_asset_catalog_blob_block(&catalog, file.path, 0, &block));
    pxa_write_u16(index+6, 1); pxa_write_u32(index+24, 0); pxa_write_u32(index+28, 0); file.size = 0;
    assert(!pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index,56}, &manifest));
    assert(!pxa_asset_catalog_blob_block(&catalog, file.path, 0, &block) && !block.bytes);
}

static void test_native_images(void) {
    pxa_asset_input_t input = {NULL, read_input, cancelled, yield_input};
    const unsigned encodings[] = {2u, 7u, 8u};
    for (size_t encoding_index = 0;
         encoding_index < sizeof(encodings) / sizeof(encodings[0]);
         ++encoding_index) {
        unsigned encoding = encodings[encoding_index];
        file_header(320,8);
        size_t payload=320*8*(encoding==2 ? 2u : 4u);
        pxa_write_u16(input_bytes+8,PXA_ASSET_IMAGE);
        pxa_write_u16(input_bytes+10,(uint16_t)encoding);
        pxa_write_u32(input_bytes+16,(uint32_t)payload);
        input_size=32+payload;
        for(size_t i=0;i<payload;++i) input_bytes[32+i]=(uint8_t)(i%251);
        pxa_asset_info_t info;
        assert(!pxa_asset_file_header(input_bytes,32,input_size,&info));
        pxa_package_file_t file={.size=input_size}; info.file=&file;
        pxa_asset_object_t *asset=NULL;
        assert(!pxa_asset_load_resident(&info,&input,allocate,deallocate,NULL,&asset));
        pxa_asset_object_view_t view; pxa_asset_object_view(asset,&view);
        assert(allocations==1 && view.kind==PXA_ASSET_IMAGE && view.encoding==encoding);
        assert(view.width==320 && view.height==8 && view.bytes==payload && max_read<=4096);
        assert((uintptr_t)view.data % PXA_ASSET_IMAGE_ALIGNMENT == 0);
        assert(pxa_asset_object_allocation_bytes(asset)==pxa_asset_object_required_bytes(&info));
        if (encoding==2) {
            for(size_t i=0;i<payload;i+=2) assert(((const uint16_t *)view.data)[i/2]==pxa_read_u16(input_bytes+32+i));
        } else assert(!memcmp(view.data,input_bytes+32,payload));
        pxa_asset_object_release(asset); assert(!allocations);
        /* Aligned pixels must not depend on malloc coincidentally returning
         * a cache-line-aligned object. Preserve bytes and the base free. */
        for (size_t offset=0;offset<PXA_ASSET_IMAGE_ALIGNMENT;offset+=sizeof(void *)) {
            input_offset=read_calls=yield_calls=0;
            assert(!pxa_asset_load_resident(&info,&input,allocate,deallocate,
                (void *)(uintptr_t)offset,&asset));
            pxa_asset_object_view(asset,&view);
            assert((uintptr_t)view.data % PXA_ASSET_IMAGE_ALIGNMENT==0);
            assert(view.bytes==payload);
            if (encoding==2) {
                for(size_t i=0;i<payload;i+=2)
                    assert(((const uint16_t *)view.data)[i/2]==pxa_read_u16(input_bytes+32+i));
            } else assert(!memcmp(view.data,input_bytes+32,payload));
            pxa_asset_object_release(asset); assert(!allocations);
        }
        assert(!pxa_raster_asset_required_bytes(&info));
        input_offset=read_calls=yield_calls=0; fail_allocate=1;
        assert(pxa_asset_load_resident(&info,&input,allocate,deallocate,NULL,&asset)==PXA_STATUS_RESOURCE_LIMIT);
        assert(!asset && !allocations && input_offset==32); fail_allocate=0;
        input_offset=read_calls=yield_calls=0; cancel_after=2;
        assert(pxa_asset_load_resident(&info,&input,allocate,deallocate,NULL,&asset)==PXA_STATUS_CANCELLED);
        assert(!asset && !allocations); cancel_after=0;
        input_offset=read_calls=yield_calls=0; --input_size;
        assert(pxa_asset_load_resident(&info,&input,allocate,deallocate,NULL,&asset)==PXA_STATUS_IO_ERROR);
        assert(!asset && !allocations);
        pxa_write_u16(input_bytes+12,4097);
        assert(pxa_asset_file_header(input_bytes,32,input_size,&info)==PXA_STATUS_UNSUPPORTED);
        assert(!pxa_asset_object_required_bytes(&info));
    }
}

int main(void) {
    static const uint8_t path[] = "assets/t.pxr";
    uint8_t index[52] = {0};
    pxa_package_file_t file = {{path, sizeof(path) - 1}, 36, digest};
    pxa_package_manifest_t manifest = {0};
    pxa_asset_catalog_t catalog;
    pxa_asset_info_t info;
    pxa_raster_asset_t *asset;
    pxa_asset_input_t input = {NULL, read_input, cancelled, yield_input};
    manifest.files = &file;
    manifest.file_count = 1;
    pxa_write_u32(index, PXA_ASSET_INDEX_MAGIC);
    pxa_write_u16(index + 4, 1);
    pxa_write_u32(index + 8, 1);
    pxa_write_u32(index + 12, sizeof(index));
    pxa_write_u16(index + 16, sizeof(path) - 1);
    index[18] = PXA_ASSET_TEXTURE;
    index[19] = PXA_ASSET_ENCODING_INDEX8;
    pxa_write_u16(index + 20, 2);
    pxa_write_u16(index + 22, 2);
    pxa_write_u32(index + 24, 36);
    pxa_write_u32(index + 28, 4);
    pxa_write_u16(index + 32, 1);
    pxa_write_u32(index + 36, 32);
    memcpy(index + 40, path, sizeof(path) - 1);
    assert(pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index, sizeof(index)},
                                  &manifest) == 0);
    assert(pxa_asset_catalog_find(&catalog, file.path, &info) == 0);
    assert(info.decoded_bytes == 4 && info.width == 2 && info.file == &file);
    assert(pxa_asset_catalog_find(&catalog, (pxa_bytes_t){(const uint8_t *)"assets/no", 9},
                                  &info) == PXA_STATUS_NOT_FOUND);
    {
        const char *bad[] = {"../secret", "assets/../secret", "assets//secret", "assets/./t",
                            "assets/", "assets/a\\b", "/assets/t", "assets/t\n"};
        for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
            assert(!pxa_asset_path_valid((pxa_bytes_t){(const uint8_t *)bad[i], strlen(bad[i])}));
    }
    file.size = 37;
    assert(pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index, sizeof(index)},
                                  &manifest) == PXA_STATUS_DENIED);
    assert(catalog.manifest == NULL);
    file.size = 36;
    index[34] = 1;
    assert(pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index, sizeof(index)},
                                  &manifest) == PXA_STATUS_PROTOCOL_ERROR);
    index[34] = 0;
    index[4] = 2;
    assert(pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index, sizeof(index)},
                                  &manifest) == PXA_STATUS_UNSUPPORTED);
    index[4] = 1;
    index[40 + 7] = '/';
    assert(pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index, sizeof(index)},
                                  &manifest) == PXA_STATUS_DENIED);

    file_header(256, 256);
    file.size = input_size;
    assert(pxa_asset_file_header(input_bytes, 32, file.size, &info) == 0);
    info.file = &file;
    assert(pxa_asset_load_raster(&info, &input, allocate, deallocate, NULL, &asset) == 0);
    assert(allocations == 1 && read_calls > 16 && max_read == 4096);
    assert(pxa_raster_asset_allocation_bytes(asset) == pxa_raster_asset_required_bytes(&info));
    {
        pxa_raster_bindings_t bindings = {0};
        pxa_raster_resources_t view;
        assert(pxa_raster_bindings_replace(&bindings, 0, asset) == NULL);
        pxa_raster_asset_release(asset);
        pxa_raster_bindings_view(&bindings, 0, &view);
        assert(view.textures[0].width == 256 && view.textures[0].pixels[65535] == 0x35);
        pxa_raster_bindings_release(&bindings);
    }
    assert(!allocations);
    file_header(256, 256);
    fail_read = 1;
    assert(pxa_asset_load_raster(&info, &input, allocate, deallocate, NULL, &asset) == PXA_STATUS_IO_ERROR);
    assert(asset == NULL && !allocations);
    fail_read = 0;
    file_header(256, 256);
    cancel_after = 3;
    assert(pxa_asset_load_raster(&info, &input, allocate, deallocate, NULL, &asset) == PXA_STATUS_CANCELLED);
    assert(asset == NULL && !allocations && read_calls == 3);
    cancel_after = 0;
    file_header(256, 256);
    --input_size;
    assert(pxa_asset_load_raster(&info, &input, allocate, deallocate, NULL, &asset) == PXA_STATUS_IO_ERROR);
    assert(!allocations);
    file_header(256, 256);
    ++input_size;
    assert(pxa_asset_load_raster(&info, &input, allocate, deallocate, NULL, &asset) == PXA_STATUS_PROTOCOL_ERROR);
    assert(!allocations);
    file_header(256, 256);
    fail_allocate = 1;
    assert(pxa_asset_load_raster(&info, &input, allocate, deallocate, NULL, &asset) == PXA_STATUS_RESOURCE_LIMIT);
    assert(!allocations && input_offset == 32);
    fail_allocate = 0;
    file_header(256, 256);
    pxa_write_u32(input_bytes + 16, UINT32_MAX);
    assert(pxa_asset_load_raster(&info, &input, allocate, deallocate, NULL, &asset) == PXA_STATUS_PROTOCOL_ERROR);
    assert(!allocations);
    test_blob_blocks();
    test_native_images();
    return 0;
}

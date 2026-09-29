#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#include "pxa/posix/pxa_posix_asset.h"
#include <assert.h>
#include "pxa/openssl/pxa_openssl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned allocations;
static void *allocate(void *c, size_t n) { (void)c; void *p = malloc(n); assert(p); ++allocations; return p; }
static void deallocate(void *c, void *p) { (void)c; assert(allocations); --allocations; free(p); }
static int cancel_now(void *c) { (void)c; return 1; }
static unsigned pcm_yields;
static void pcm_yield(void *c) { (void)c; ++pcm_yields; }
static int pcm_cancel_after_block(void *c) { (void)c; return pcm_yields != 0; }

static void test_pcm(const char *root) {
    uint8_t data[4097], digest[32];
    char path[1024];
    memset(data, 173, sizeof(data));
    snprintf(path, sizeof(path), "%s/assets/sound.pcm", root);
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(data, 1, sizeof(data), file) == sizeof(data));
    assert(!fclose(file));
    assert(!pxa_openssl_sha256(data, sizeof(data), digest));
    const char *name = "assets/sound.pcm";
    pxa_package_file_t member = {{(const uint8_t *)name, strlen(name)}, sizeof(data), digest};
    pxa_asset_info_t info = {0};
    info.kind = PXA_ASSET_AUDIO;
    info.encoding = PXA_ASSET_ENCODING_PCM_U8_16K_MONO;
    info.path = member.path; info.file = &member;
    info.stored_bytes = info.decoded_bytes = sizeof(data);
    pxa_posix_asset_input_t stream;
    pxa_asset_input_t input;
    pxa_asset_object_t *asset;
    assert(!pxa_posix_asset_open(&stream, root, info.path, info.stored_bytes, &input));
    assert(!pxa_asset_load_resident(&info, &input, allocate, deallocate, NULL, &asset));
    pxa_asset_object_view_t view;
    pxa_asset_object_view(asset, &view);
    assert(view.kind == PXA_ASSET_AUDIO && view.bytes == sizeof(data));
    assert(!memcmp(view.data, data, sizeof(data)) && allocations == 1);
    pxa_asset_object_release(asset); pxa_posix_asset_close(&stream);
    assert(!allocations);

    /* The headerless PCM branch must discard partial data on cancellation. */
    assert(!pxa_posix_asset_open(&stream, root, info.path, info.stored_bytes, &input));
    pcm_yields = 0; input.yield = pcm_yield; input.cancelled = pcm_cancel_after_block;
    assert(pxa_asset_load_resident(&info, &input, allocate, deallocate, NULL, &asset) == PXA_STATUS_CANCELLED);
    assert(!asset && !allocations && pcm_yields == 1);
    pxa_posix_asset_close(&stream);

    assert(!truncate(path, sizeof(data) - 1));
    assert(pxa_posix_asset_open(&stream, root, info.path, info.stored_bytes, &input) != PXA_STATUS_OK);

    /* Invalid declarations must be rejected before invoking any input or allocator. */
    info.stored_bytes = info.decoded_bytes = 16001;
    member.size = 16001;
    memset(&input, 0, sizeof(input));
    assert(pxa_asset_load_resident(&info, &input, allocate, deallocate, NULL, &asset) == PXA_STATUS_INVALID_ARGUMENT);
    assert(!asset && !allocations && !unlink(path));
    puts("resident PCM: installed bytes, partial-read cancellation, truncation and size limit passed");
}

int main(int argc, char **argv) {
    static const char *names[] = {"assets/p.pxr", "assets/resources.pxi", "assets/t.pxr"};
    pxa_package_file_t files[3];
    pxa_package_manifest_t manifest = {0};
    pxa_asset_catalog_t catalog;
    pxa_asset_input_t input;
    pxa_posix_asset_input_t stream;
    pxa_raster_bindings_t bindings = {0};
    pxa_raster_resources_t view;
    pxa_asset_info_t info;
    pxa_raster_asset_t *asset;
    uint8_t digests[3][32], index[1024], buffer[1024];
    char full[1024];
    size_t index_size = 0;
    assert(argc == 2);
    test_pcm(argv[1]);
    for (unsigned i = 0; i < 3; ++i) {
        FILE *file;
        size_t bytes;
        snprintf(full, sizeof(full), "%s/%s", argv[1], names[i]);
        file = fopen(full, "rb");
        assert(file);
        bytes = fread(buffer, 1, sizeof(buffer), file);
        assert(feof(file) && !ferror(file));
        fclose(file);
        assert(pxa_openssl_sha256(buffer, bytes, digests[i]) == 0);
        files[i] = (pxa_package_file_t){{(const uint8_t *)names[i], strlen(names[i])}, bytes, digests[i]};
        if (i == 1) { memcpy(index, buffer, bytes); index_size = bytes; }
    }
    manifest.files = files;
    manifest.file_count = 3;
    /* Read the catalog from the installed package. */
    assert(pxa_posix_asset_open(&stream, argv[1], files[1].path, files[1].size, &input) == 0);
    size_t count;
    assert(input.read(input.context, buffer, sizeof(buffer), &count) == 0 && count == index_size);
    pxa_posix_asset_close(&stream);
    assert(pxa_asset_catalog_init(&catalog, (pxa_bytes_t){index, index_size}, &manifest) == 0);
    assert(catalog.count == 2);
    for (unsigned i = 0; i < 3; i += 2) {
        assert(pxa_asset_catalog_find(&catalog, files[i].path, &info) == 0);
        assert(pxa_posix_asset_open(&stream, argv[1], info.path, info.stored_bytes, &input) == 0);
        assert(pxa_asset_load_raster(&info, &input, allocate, deallocate, NULL, &asset) == 0);
        pxa_posix_asset_close(&stream);
        assert(pxa_raster_bindings_replace(&bindings, 0, asset) == NULL);
        pxa_raster_asset_release(asset);
    }
    pxa_raster_bindings_view(&bindings, PXA_RASTER_CAP_KNOWN_MASK, &view);
    assert(view.palette[1] == 0xf800 && view.palette[2] == 0x07e0);
    assert(view.textures[0].width == 2 && view.textures[0].height == 2);
    {
        uint8_t draw[PXA_RASTER_DRAW_HEADER_BYTES + PXA_RASTER_SPRITE_BYTES] = {0};
        uint16_t pixels[16] = {0};
        pxa_raster_target_t target = {0};
        pxa_raster_draw_list_view_t list;
        uint8_t *r = draw + PXA_RASTER_DRAW_HEADER_BYTES;
        pxa_write_u32(draw, PXA_RASTER_DRAW_MAGIC);
        pxa_write_u16(draw + 4, PXA_RASTER_ABI_MAJOR);
        pxa_write_u16(draw + 6, PXA_RASTER_ABI_MINOR);
        pxa_write_u32(draw + 8, sizeof(draw));
        pxa_write_u32(draw + 16, 1);
        pxa_write_u64(draw + 20, 1);
        r[0] = PXA_RASTER_RECORD_SPRITE;
        pxa_write_u16(r + 2, PXA_RASTER_SPRITE_BYTES);
        pxa_write_u16(r + 12, 4); pxa_write_u16(r + 14, 4);
        pxa_write_u16(r + 20, 2); pxa_write_u16(r + 22, 2);
        target.pixels = pixels;
        target.stride_pixels = target.width = target.height = 4;
        target.scratch_mode = PXA_RASTER_SCRATCH_NONE;
        assert(pxa_raster_validate_draw_list(draw, sizeof(draw), &target, &view, &list) == 0);
        pxa_raster_execute_draw_list(draw, &list, &target, &view, NULL);
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 4; ++x)
                assert(pixels[y * 4 + x] == view.palette[(y / 2 * 2 + x / 2 + 1) % 4]);
    }
    pxa_raster_bindings_release(&bindings);
    assert(allocations == 0);
    assert(pxa_posix_asset_open(&stream, argv[1], info.path, info.stored_bytes, &input) == 0);
    input.cancelled = cancel_now;
    assert(pxa_asset_load_raster(&info, &input, allocate, deallocate, NULL, &asset) == PXA_STATUS_CANCELLED);
    pxa_posix_asset_close(&stream);
    assert(allocations == 0);
    /* Only regular files within the package root are valid resource inputs. */
    snprintf(full, sizeof(full), "%s/assets/t.pxr", argv[1]);
    assert(unlink(full) == 0 && symlink("p.pxr", full) == 0);
    assert(pxa_posix_asset_open(&stream, argv[1], info.path, info.stored_bytes, &input) == PXA_STATUS_DENIED);
    assert(unlink(full) == 0 && mkfifo(full, 0600) == 0);
    assert(pxa_posix_asset_open(&stream, argv[1], info.path, info.stored_bytes, &input) == PXA_STATUS_DENIED);
    snprintf(full, sizeof(full), "%s/assets/alias", argv[1]);
    assert(symlink(".", full) == 0);
    const char *alias = "assets/alias/p.pxr";
    assert(pxa_posix_asset_open(&stream, argv[1],
        (pxa_bytes_t){(const uint8_t *)alias, strlen(alias)}, files[0].size,
        &input) == PXA_STATUS_DENIED);
    const char *bad = "assets/../secret";
    assert(pxa_posix_asset_open(&stream, argv[1], (pxa_bytes_t){(const uint8_t *)bad, strlen(bad)}, 0, &input) == PXA_STATUS_DENIED);
    const char *missing = "assets/missing.pxr";
    assert(pxa_posix_asset_open(&stream, argv[1], (pxa_bytes_t){(const uint8_t *)missing, strlen(missing)}, 32, &input) == PXA_STATUS_NOT_FOUND);
    puts("asset input: installed files -> parsed catalog -> streamed textures -> raster pixels passed");
    return 0;
}

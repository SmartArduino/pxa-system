#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#include "pxa/asset_stream.h"
#include "pxa/posix/pxa_posix_asset.h"
#include <assert.h>
#include "pxa/openssl/pxa_openssl.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    pxa_posix_asset_input_t file;
    pxa_asset_input_t input;
    int cancelled, cancel_after_load;
    unsigned loads;
} source_t;
static int cancelled(void *context) { return ((source_t *)context)->cancelled; }
static pxa_status_t load(void *context, const pxa_asset_block_t *block,
    uint8_t *output, size_t capacity) {
    source_t *source = context;
    ++source->loads;
    pxa_status_t status = pxa_posix_asset_select_range(&source->file, block->offset, block->bytes);
    if (!status) status = pxa_asset_load_block(block, &source->input, output, capacity);
    if (source->cancel_after_load) source->cancelled = 1;
    return status;
}
static void start(source_t *source, pxa_asset_stream_t *stream,
    const char *root, const pxa_asset_block_map_t *map, uint8_t *buffer) {
    (void)buffer;
    memset(source, 0, sizeof(*source));
    assert(!pxa_posix_asset_open(&source->file, root, map->info.path,
                                map->info.stored_bytes, &source->input));
    assert(!pxa_asset_stream_init(stream, map, source, load, cancelled));
}
static size_t read_file(const char *root, const char *name, uint8_t *bytes, size_t capacity) {
    char path[1024]; snprintf(path,sizeof(path),"%s/%s",root,name);
    FILE *file=fopen(path,"rb"); assert(file);
    size_t n=fread(bytes,1,capacity,file);
    assert(feof(file) && !ferror(file)); fclose(file); return n;
}
int main(int argc, char **argv) {
    assert(argc==2);
    static const char *names[]={"assets/music.ogg","assets/resources.pxi"};
    uint8_t original[10000], index[1024], digests[2][32], buffer[4096], output[4096];
    size_t bytes=read_file(argv[1],names[0],original,sizeof(original));
    size_t index_bytes=read_file(argv[1],names[1],index,sizeof(index));
    assert(bytes>8192 && bytes<sizeof(original));
    assert(!pxa_openssl_sha256(original,bytes,digests[0]));
    assert(!pxa_openssl_sha256(index,index_bytes,digests[1]));
    pxa_package_file_t files[2]={
        {{(const uint8_t*)names[0],strlen(names[0])},bytes,digests[0]},
        {{(const uint8_t*)names[1],strlen(names[1])},index_bytes,digests[1]}};
    pxa_package_manifest_t manifest={0}; manifest.files=files; manifest.file_count=2;
    pxa_asset_catalog_t catalog;
    pxa_asset_block_map_t map;
    assert(!pxa_asset_catalog_init(&catalog,(pxa_bytes_t){index,index_bytes},&manifest));
    assert(!pxa_asset_catalog_block_map(&catalog,files[0].path,&map));
    assert(map.info.decoded_bytes==0 && index_bytes<100);
    pxa_asset_blob_block_t blob;
    assert(pxa_asset_catalog_blob_block(&catalog,files[0].path,0,&blob)==PXA_STATUS_UNSUPPORTED);
    source_t source; pxa_asset_stream_t stream; size_t count;
    start(&source,&stream,argv[1],&map,buffer);
    assert(!pxa_asset_stream_read(&stream,output,7,&count) && count==7);
    assert(!memcmp(output,original,count) && source.loads==1);
    assert(!pxa_asset_stream_read(&stream,output,17,&count) && count==17);
    assert(!memcmp(output,original+7,count) && source.loads==2);
    assert(!pxa_asset_stream_seek(&stream,4090));
    assert(!pxa_asset_stream_read(&stream,output,sizeof(output),&count) && count==4096);
    assert(!memcmp(output,original+4090,count));
    assert(pxa_asset_stream_seek(&stream,(uint32_t)bytes+1)==PXA_STATUS_INVALID_ARGUMENT);
    assert(stream.position==8186);
    assert(!pxa_asset_stream_seek(&stream,(uint32_t)bytes));
    assert(!pxa_asset_stream_read(&stream,output,sizeof(output),&count) && !count);
    // Looping/seek uses bounded storage and never reopens the path.
    for(unsigned loop=0;loop<3;++loop) {
        assert(!pxa_asset_stream_seek(&stream,0)); size_t offset=0;
        do {
            assert(!pxa_asset_stream_read(&stream,output,777,&count));
            assert(!memcmp(output,original+offset,count)); offset+=count;
        } while(count);
        assert(offset==bytes);
    }
    source.cancelled=1; memset(output,0xcd,sizeof(output));
    assert(pxa_asset_stream_seek(&stream,0)==PXA_STATUS_CANCELLED);
    source.cancelled=0;
    assert(pxa_asset_stream_read(&stream,output,sizeof(output),&count)==PXA_STATUS_CANCELLED && !count);
    pxa_posix_asset_close(&source.file);
    start(&source,&stream,argv[1],&map,buffer);
    source.cancel_after_load=1;
    assert(pxa_asset_stream_read(&stream,output,sizeof(output),&count)==PXA_STATUS_CANCELLED && !count);
    pxa_posix_asset_close(&source.file);
    start(&source,&stream,argv[1],&map,buffer);
    char path[1024], moved[1024];
    snprintf(path,sizeof(path),"%s/%s",argv[1],names[0]);
    snprintf(moved,sizeof(moved),"%s/assets/retired.ogg",argv[1]);
    assert(!rename(path,moved) && !symlink("retired.ogg",path));
    assert(!pxa_asset_stream_read(&stream,output,sizeof(output),&count) && !memcmp(output,original,count));
    pxa_posix_asset_input_t denied; pxa_asset_input_t ignored;
    assert(pxa_posix_asset_open(&denied,argv[1],files[0].path,bytes,&ignored)==PXA_STATUS_DENIED);
    assert(!unlink(path) && !rename(moved,path));
    pxa_posix_asset_close(&source.file);
    start(&source,&stream,argv[1],&map,buffer);
    assert(!truncate(path,4096));
    assert(!pxa_asset_stream_seek(&stream,4096));
    assert(pxa_asset_stream_read(&stream,output,sizeof(output),&count)==PXA_STATUS_IO_ERROR && !count);
    pxa_posix_asset_close(&source.file);
    puts("installed stream: direct bounded reads/seek/loop, EOF, cancellation, stable descriptor, truncation passed");
    return 0;
}

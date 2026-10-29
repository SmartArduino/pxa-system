#undef NDEBUG
#include "pxa/asset_cache.h"
#include "pxa/image/codecs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const uint8_t *bytes; size_t size, position, cancel_after; } source_t;
static pxa_status_t read_input(void *context, uint8_t *out, size_t capacity, size_t *count) {
    source_t *s=context;
    *count=s->size-s->position;
    if (*count>capacity) *count=capacity;
    if (*count>97) *count=97; /* Split PNG chunks, zlib blocks and palette data. */
    memcpy(out,s->bytes+s->position,*count); s->position+=*count;
    return PXA_STATUS_OK;
}
static int cancelled(void *context) {
    source_t *s=context; return s->cancel_after && s->position>=s->cancel_after;
}
static void *allocate(void *context,size_t n) { (void)context; return malloc(n); }
static void release(void *context,void *p) { (void)context; free(p); }
static uint8_t *read_file(const char *path,size_t *size) {
    FILE *f=fopen(path,"rb"); assert(f && !fseek(f,0,SEEK_END));
    long length=ftell(f); assert(length>0 && !fseek(f,0,SEEK_SET));
    uint8_t *bytes=malloc((size_t)length); assert(bytes);
    assert(fread(bytes,1,(size_t)length,f)==(size_t)length); fclose(f); *size=(size_t)length; return bytes;
}
int main(int argc,char **argv) {
    assert(argc>=5 && argc<=7);
    const int opaque=argc>=6;
    size_t size, reference_size;
    uint8_t *png=read_file(argv[1],&size), *reference=read_file(argv[2],&reference_size);
    unsigned width=(unsigned)atoi(argv[3]),height=(unsigned)atoi(argv[4]);
    uint8_t digest[32]={0}, package_key[32]={1};
    pxa_bytes_t path={(const uint8_t *)"assets/pose.png",15};
    pxa_package_file_t file={path,size,digest};
    pxa_asset_info_t info={0};
    info.path=path; info.kind=PXA_ASSET_IMAGE; info.encoding=PXA_ASSET_ENCODING_PNG;
    info.width=(uint16_t)width; info.height=(uint16_t)height;
    info.stored_bytes=(uint32_t)size; info.decoded_bytes=width*height*4; info.file=&file;
    if (opaque) {
        assert(!pxa_asset_file_header(png,size,size,&info));
        assert(info.encoding==PXA_ASSET_ENCODING_PNG_RGB565);
        info.path=path;info.file=&file;
    }
    assert(reference_size==info.decoded_bytes);
    pxa_memory_budget_config_t limits={.limit={1024*1024,1024*1024},.temporary_limit={0,128*1024}};
    pxa_memory_budget_t budget; pxa_memory_owner_t owner;
    assert(!pxa_memory_budget_init(&budget,&limits));
    const size_t owner_limits[]={1024*1024,1024*1024};
    assert(!pxa_memory_owner_open(&budget,owner_limits,&owner));
    pxa_memory_allocator_t pixels={&budget,owner,1,PXA_MEMORY_IMAGE,NULL,allocate,release};
    pxa_memory_allocator_t temporary={&budget,owner,1,PXA_MEMORY_TEMPORARY,NULL,allocate,release};
    static const pxa_image_codec_t *const codecs[]={&pxa_stream_png_codec};
    const pxa_image_backend_t backend={codecs,1};
    source_t source={png,size,0,0};
    pxa_asset_input_t input={&source,read_input,cancelled,NULL};
    pxa_asset_object_t *image=NULL;
    if (argc==7) {
        assert(pxa_asset_load_png(&info,&input,&pixels,&temporary,&backend,&image)==PXA_STATUS_PROTOCOL_ERROR);
        assert(!image);
        pxa_memory_stats_t memory;assert(!pxa_memory_budget_stats(&budget,owner,&memory));
        assert(!memory.charged[0] && !memory.charged[1]);
        assert(!pxa_memory_owner_close(&budget,owner));
        free(reference);free(png);
        puts("PNG RGB565 rejects alpha without leaking allocations");
        return 0;
    }
    assert(!pxa_asset_load_png(&info,&input,&pixels,&temporary,&backend,&image));
    pxa_asset_object_view_t view; pxa_asset_object_view(image,&view);
    assert(view.width==width && view.height==height && view.bytes==reference_size);
    assert(view.encoding==(opaque ? PXA_ASSET_ENCODING_RGB565 : PXA_ASSET_ENCODING_BGRA8888_PREMULTIPLIED));
    assert(!memcmp(view.data,reference,reference_size));
    assert(pxa_asset_resident_required_bytes(&info)==pxa_asset_object_allocation_bytes(image));
    pxa_asset_object_release(image); image=NULL;

    /* Concurrent requests coalesce; later requests reuse the decoded pixels. */
    pxa_asset_cache_config_t config={8,8,8,8,{4096,512*1024},{4096,512*1024}};
    size_t bytes=pxa_asset_cache_workspace_size(&config);void *workspace=malloc(bytes);
    pxa_asset_cache_t *cache;assert(!pxa_asset_cache_init(workspace,bytes,&config,&cache));
    pxa_asset_ticket_t a,b,c;pxa_asset_job_t job,discard;
    assert(!pxa_asset_cache_request(cache,1,package_key,&info,1,&a));
    assert(!pxa_asset_cache_request(cache,1,package_key,&info,1,&b));
    assert(!pxa_asset_cache_next_job(cache,&job) && job.kind==PXA_ASSET_JOB_LOAD);
    source.position=0;
    assert(!pxa_asset_load_png(&job.info,&input,&pixels,&temporary,&backend,&image));
    assert(!pxa_asset_cache_finish_load(cache,job.token,0,image,&discard) && !discard.kind);
    pxa_asset_object_t *first,*second;
    assert(!pxa_asset_cache_acquire(cache,1,a,&first));
    assert(!pxa_asset_cache_acquire(cache,1,b,&second) && first==second);
    pxa_asset_object_release(first);pxa_asset_object_release(second);
    assert(!pxa_asset_cache_release(cache,1,a) && !pxa_asset_cache_release(cache,1,b));
    assert(!pxa_asset_cache_request(cache,1,package_key,&info,1,&c));
    pxa_asset_cache_stats_t stats;pxa_asset_cache_stats(cache,&stats);
    assert(stats.coalesced_requests==1 && stats.cache_hits==1 && stats.load_failures==0);
    assert(!pxa_asset_cache_release(cache,1,c));
    pxa_asset_cache_shutdown(cache);
    assert(!pxa_asset_cache_next_job(cache,&job) && job.kind==PXA_ASSET_JOB_RELEASE);
    pxa_asset_object_release(job.asset);assert(!pxa_asset_cache_finish_release(cache,job.token));
    assert(pxa_asset_cache_drained(cache));free(workspace);

    source.position=0;source.cancel_after=97;image=NULL;
    assert(pxa_asset_load_png(&info,&input,&pixels,&temporary,&backend,&image)==PXA_STATUS_CANCELLED && !image);
    source.position=0;source.cancel_after=0;source.size=size-1;
    assert(pxa_asset_load_png(&info,&input,&pixels,&temporary,&backend,&image)!=0 && !image);
    source.position=0;source.size=size;png[0]^=1;
    assert(pxa_asset_load_png(&info,&input,&pixels,&temporary,&backend,&image)!=0 && !image);png[0]^=1;
    source.position=0;info.width++;
    assert(pxa_asset_load_png(&info,&input,&pixels,&temporary,&backend,&image)!=0 && !image);
    pxa_memory_stats_t memory;assert(!pxa_memory_budget_stats(&budget,owner,&memory));
    assert(!memory.charged[0] && !memory.charged[1]);
    assert(!pxa_memory_owner_close(&budget,owner));
    free(reference);free(png);
    puts("PNG assets: exact native pixels, cached/coalesced loads, cancellation and corrupt input passed");
    return 0;
}

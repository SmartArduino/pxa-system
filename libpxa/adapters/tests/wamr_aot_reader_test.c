#include "../wamr/pxa_wamr_aot_reader.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Count requested executable mappings, not virtual linear-memory reservations
 * or process RSS. A failed load must release every executable allocation. */
static size_t mapped_bytes, mapped_peak;
static size_t runtime_bytes, artifact_bytes, total_peak, allocations, fail_allocation;
typedef union { size_t size; long double alignment; } Header;
static void peak(void) {
    size_t n=mapped_bytes+runtime_bytes+artifact_bytes;
    if(n>total_peak)total_peak=n;
}
static void *allocate(unsigned int size) {
    if(++allocations==fail_allocation)return NULL;
    Header *h=malloc(sizeof(*h)+size);if(!h)return NULL;
    h->size=size;runtime_bytes+=size;peak();return h+1;
}
static void release(void *p) {
    if(p){Header *h=(Header *)p-1;runtime_bytes-=h->size;free(h);}
}
static void *reallocate(void *p,unsigned int size) {
    if(!p)return allocate(size);
    if(++allocations==fail_allocation)return NULL;
    Header *h=(Header *)p-1;size_t old=h->size;
    h=realloc(h,sizeof(*h)+size);if(!h)return NULL;
    h->size=size;runtime_bytes=runtime_bytes-old+size;peak();return h+1;
}
#if defined(PXA_TEST_WRAP_MMAP)
static struct {void *address; size_t size;} mappings[16];
/* WAMR's Linux os_file_handle is int. Keep private platform configuration out
 * of this consumer; the GNU linker wrappers are enabled only on Linux. */
void *__real_os_mmap(void *, size_t, int, int, int);
void __real_os_munmap(void *, size_t);
void *__wrap_os_mmap(void *hint, size_t size, int prot, int flags, int file) {
    void *p=__real_os_mmap(hint,size,prot,flags,file);
    if(p && (prot & 4)) {
        unsigned n=0;while(n<16 && mappings[n].address)++n;assert(n<16);
        mappings[n].address=p;mappings[n].size=size;
        mapped_bytes+=size;if(mapped_bytes>mapped_peak)mapped_peak=mapped_bytes;
        peak();
    }
    return p;
}
void __wrap_os_munmap(void *address,size_t size) {
    for(unsigned n=0;n<16;++n) if(mappings[n].address==address) {
        mapped_bytes-=mappings[n].size;mappings[n].address=NULL;break;
    }
    __real_os_munmap(address,size);
}
#endif

typedef struct {
    uint8_t *bytes;
    size_t size, limit, calls, fail_at, max_read;
} Source;

static pxa_status_t read_at(void *context, uint64_t offset, uint8_t *output,
                            size_t capacity, size_t *size) {
    Source *s = context;
    ++s->calls;
    if (capacity > s->max_read) s->max_read = capacity;
    *size = 0;
    if (s->calls == s->fail_at) return PXA_STATUS_IO_ERROR;
    if (offset > s->size) return PXA_STATUS_IO_ERROR;
    size_t n = s->size - (size_t)offset;
    if (n > capacity) n = capacity;
    if (s->limit && n > s->limit) n = s->limit;
    memcpy(output, s->bytes + (size_t)offset, n);
    *size = n;
    return PXA_STATUS_OK;
}

static wasm_module_t load(Source *source, pxa_status_t *status, int *buffered) {
    pxa_wamr_artifact_reader_t reader = { .context=source, .size=source->size, .read_at=read_at };
    wasm_module_t module = NULL;
    char error[128] = {0};
    *status = pxa_wamr_read_aot(&reader, &module, buffered, error, sizeof(error));
    assert(source->max_read <= 16384);
    return module;
}

static void run(wasm_module_t module) {
    char error[128];
    wasm_module_inst_t instance = wasm_runtime_instantiate(module, 16384, 16384, error, sizeof(error));
    assert(instance);
    wasm_exec_env_t env = wasm_runtime_create_exec_env(instance, 16384);
    assert(env);
    wasm_function_inst_t start = wasm_runtime_lookup_function(instance, "pxa_app_start");
    assert(start);
    uint32_t args[2] = {0,0};
    assert(wasm_runtime_call_wasm(env, start, 2, args));
    assert(args[0] == 0);
    wasm_runtime_destroy_exec_env(env);
    wasm_runtime_deinstantiate(instance);
    wasm_runtime_unload(module);
    assert(mapped_bytes==0);
}

static uint32_t u32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned n=0;n<4;++n) p[n]=(uint8_t)(v>>(8*n));
}

int main(void) {
    FILE *file = fopen(PXA_WAMR_TEST_AOT_DATA_PATH,"rb");
    assert(file && !fseek(file,0,SEEK_END));
    size_t length = (size_t)ftell(file);
    rewind(file);
    uint8_t *original = malloc(length), *input = malloc(length);
    assert(original && input && fread(original,1,length,file)==length);
    fclose(file);
    RuntimeInitArgs init={0};init.mem_alloc_type=Alloc_With_Allocator;
    union { void *object; void *(*function)(unsigned int); } a={.function=allocate};
    union { void *object; void *(*function)(void *,unsigned int); } b={.function=reallocate};
    union { void *object; void (*function)(void *); } c={.function=release};
    init.mem_alloc_option.allocator.malloc_func=a.object;
    init.mem_alloc_option.allocator.realloc_func=b.object;
    init.mem_alloc_option.allocator.free_func=c.object;
    assert(wasm_runtime_full_init(&init));
    size_t runtime_base=runtime_bytes;
    for (unsigned n=0;n<20;++n) {
        memcpy(input,original,length);
        Source source = {.bytes=input,.size=length,.limit=n%2?7:0};
        pxa_status_t status; int buffered;
        wasm_module_t module=load(&source,&status,&buffered);
        if(status) fprintf(stderr,"load status=%d, length=%zu\n",status,length);
        assert(status==PXA_STATUS_OK && module && !buffered);
        assert(wasm_runtime_get_module_package_version(module)==
               wasm_runtime_get_file_package_version(original,(uint32_t)length));
        memset(input,0xa5,length); /* no initial-data or name pointers into the source */
        run(module);
    }
    memcpy(input,original,length);
    Source good = {.bytes=input,.size=length};
    pxa_status_t status; int buffered;
    wasm_module_t module=load(&good,&status,&buffered);
    assert(status==PXA_STATUS_OK && module); run(module);
    size_t allocation_count=allocations;
    Source allocation_probe={.bytes=input,.size=length};
    module=load(&allocation_probe,&status,&buffered);
    assert(status==PXA_STATUS_OK && module);
    size_t load_allocations=allocations-allocation_count;
    run(module);
    for(size_t n=1;n<=load_allocations;++n) {
        fail_allocation=allocations+n;
        Source bad={.bytes=input,.size=length};
        module=load(&bad,&status,&buffered);
        fail_allocation=0;
        if(module)run(module);
        assert(mapped_bytes==0 && runtime_bytes==runtime_base);
    }
    for (size_t fail=1;fail<=good.calls;++fail) {
        Source source={.bytes=input,.size=length,.fail_at=fail};
        module=load(&source,&status,&buffered);
        assert(status==PXA_STATUS_IO_ERROR && !module && !buffered);
        assert(mapped_bytes==0);
    }
    for(size_t size=0;size<length;++size) {
        Source source={.bytes=input,.size=size};
        module=load(&source,&status,&buffered);
        assert(status!=PXA_STATUS_OK && !module);
        assert(mapped_bytes==0);
    }
    uint8_t *trailing=calloc(1,length+8);assert(trailing);
    memcpy(trailing,original,length);
    for(size_t n=1;n<=8;++n) {
        Source bad={.bytes=trailing,.size=length+n};
        module=load(&bad,&status,&buffered);
        assert(status!=PXA_STATUS_OK && !module && mapped_bytes==0);
    }
    free(trailing);
    input[20]=4; input[21]=0; /* TARGET_INFO e_type = XIP */
    Source xip={.bytes=input,.size=length};
    module=load(&xip,&status,&buffered);
    assert(status==PXA_STATUS_UNSUPPORTED && !module && !buffered);
    memcpy(input,original,length);
    size_t function=8;
    while(u32(original+function)!=3) function=(function+8+u32(original+function+4)+3)&~(size_t)3;
    for(unsigned n=0;n<20;++n) {
        memcpy(input,original,length);
        memset(input+function+8,0xff,8); /* fail after TEXT ownership transfer */
        Source bad={.bytes=input,.size=length};
        module=load(&bad,&status,&buffered);
        assert(status!=PXA_STATUS_OK && !module);
        assert(mapped_bytes==0);
    }
    /* A large unused TEXT tail exercises bounded reads without growing data
       segments. Existing functions/relocations keep their offsets and results. */
    size_t text=8;
    while(u32(original+text)!=2) text=(text+8+u32(original+text+4)+3)&~(size_t)3;
    size_t size=u32(original+text+4), extra=2*1024*1024, end=text+8+size;
    uint8_t *large=calloc(1,length+extra);
    assert(large);
    memcpy(large,original,end);
    memcpy(large+end+extra,original+end,length-end);
    put32(large+text+4,(uint32_t)(size+extra));
    Source source={.bytes=large,.size=length+extra,.limit=1023};
    total_peak=0;
    module=load(&source,&status,&buffered);
    assert(status==PXA_STATUS_OK && module && !buffered);
    size_t stream_peak=total_peak, stream_live=runtime_bytes+mapped_bytes;
    wasm_runtime_unload(module);assert(mapped_bytes==0 && runtime_bytes==runtime_base);
    total_peak=0;artifact_bytes=length+extra;peak();
    char error[128];LoadArgs args={0};args.name="";args.wasm_binary_freeable=true;
    module=wasm_runtime_load_ex(large,(uint32_t)(length+extra),&args,error,sizeof(error));
    assert(module && wasm_runtime_is_underlying_binary_freeable(module));
    artifact_bytes=0;
    size_t buffered_peak=total_peak,buffered_live=runtime_bytes+mapped_bytes;
    assert(stream_live==buffered_live && stream_peak<buffered_peak);
    printf("AOT allocations (requested bytes, includes global runtime baseline): stream peak=%zu buffered peak=%zu; live=%zu/%zu; source=%zu; allocation failures=%zu\n",stream_peak,buffered_peak,stream_live,buffered_live,length+extra,load_allocations);
    memset(large,0xa5,length+extra); run(module);
    printf("AOT reader: 20 poisoned-source starts; %zu read failures; %zu truncations; XIP rejected; 2 MiB TEXT, max request=%zu OK\n",good.calls,length,source.max_read);
    free(large);free(input);free(original);
    wasm_runtime_destroy();
    return 0;
}

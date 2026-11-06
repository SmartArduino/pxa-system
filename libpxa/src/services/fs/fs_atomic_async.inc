/* SPDX-License-Identifier: MIT
 * Native atomic-write sessions. Only the owner touches Runtime APIs; executors
 * own copied payloads and publish results through the phase acquire/release. */
#include <stdatomic.h>

typedef struct fs_atomic_job {
    pxa_fs_service_t *service;
    pxa_component_t component;
    pxa_request_token_t token;
    uint64_t identity, ticket, expected, written;
    uint32_t generation;
    uint16_t operation, temporary_size, destination_size, data_size;
    uint8_t used, retired, kind;
    atomic_int phase, cancelled;
    void *resource;
    pxa_status_t status;
    uint8_t temporary[PXA_FS_MAX_PATH_BYTES], destination[PXA_FS_MAX_PATH_BYTES];
    uint8_t data[PXA_FS_ATOMIC_MAX_CHUNK];
} fs_atomic_job_t;
struct fs_atomic_state {
    pxa_fs_close_checked_fn close_checked;
    pxa_fs_async_submit_fn submit;
    void *executor;
    pxa_fs_backend_gate_fn lock, unlock;
    void *gate;
    uint16_t capacity;
    uint8_t stopping;
    fs_atomic_job_t jobs[];
};
static void fs_backend_lock(pxa_fs_service_t *s) {
    if(s->atomic)s->atomic->lock(s->atomic->gate);
}
static void fs_backend_unlock(pxa_fs_service_t *s) {
    if(s->atomic)s->atomic->unlock(s->atomic->gate);
}
size_t pxa_fs_service_atomic_workspace_size(uint16_t capacity) {
    if(!capacity || capacity>16)return 0;
    return PXA_INTERNAL_WORKSPACE_ALIGNMENT-1+sizeof(struct fs_atomic_state)+
        capacity*sizeof(fs_atomic_job_t);
}
pxa_status_t pxa_fs_service_enable_async_atomic(pxa_fs_service_t *s,
    void *workspace,size_t bytes,uint16_t capacity,pxa_fs_close_checked_fn close_checked,pxa_fs_async_submit_fn submit,
    void *executor,pxa_fs_backend_gate_fn lock,pxa_fs_backend_gate_fn unlock,void *gate) {
    size_t required=pxa_fs_service_atomic_workspace_size(capacity);
    if(!service_valid(s) || !workspace || !required || bytes<required ||
        !close_checked || !submit || !lock || !unlock || capacity>s->slot_count ||
        (uintptr_t)workspace>UINTPTR_MAX-bytes)return PXA_STATUS_INVALID_ARGUMENT;
    if(s->registered || s->atomic)return PXA_STATUS_BAD_STATE;
    if(!s->backend.replace)return PXA_STATUS_UNSUPPORTED;
    struct fs_atomic_state *a=(void *)pxa_internal_align_pointer((uintptr_t)workspace,PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    memset(a,0,sizeof(*a)+capacity*sizeof(fs_atomic_job_t));
    a->capacity=capacity;a->close_checked=close_checked;a->submit=submit;a->executor=executor;
    a->lock=lock;a->unlock=unlock;a->gate=gate;
    for(unsigned i=0;i<capacity;++i) {
        atomic_init(&a->jobs[i].phase,0);atomic_init(&a->jobs[i].cancelled,0);
    }
    s->atomic=a;return PXA_STATUS_OK;
}
static pxa_bytes_t fs_atomic_temp(fs_atomic_job_t *j) {
    return (pxa_bytes_t){j->temporary,j->temporary_size};
}
static pxa_status_t fs_atomic_cleanup(fs_atomic_job_t *j) {
    pxa_fs_backend_t *b=&j->service->backend;
    pxa_status_t status=PXA_STATUS_OK;
    if(j->resource) {
        status=pxa_status_normalize(j->service->atomic->close_checked(b->context,j->resource,j->kind));
        j->resource=NULL;
    }
    pxa_status_t removed=pxa_status_normalize(b->remove(b->context,fs_atomic_temp(j)));
    j->retired=1;
    if(status!=PXA_STATUS_OK)return status;
    return removed==PXA_STATUS_NOT_FOUND?PXA_STATUS_OK:removed;
}
static void fs_atomic_work(void *context) {
    fs_atomic_job_t *j=context;
    pxa_fs_backend_t *b=&j->service->backend;
    pxa_status_t status=PXA_STATUS_OK;
    fs_backend_lock(j->service);
    if(atomic_load_explicit(&j->cancelled,memory_order_acquire))status=PXA_STATUS_CANCELLED;
    else if(j->operation==PXA_FS_ATOMIC_BEGIN) {
        status=pxa_status_normalize(b->open(b->context,fs_atomic_temp(j),
            PXA_FS_OPEN_WRITE|PXA_FS_OPEN_CREATE|PXA_FS_OPEN_TRUNCATE,&j->resource,&j->kind));
        if(status==PXA_STATUS_OK && (!j->resource || j->kind!=PXA_FS_KIND_REGULAR))status=PXA_STATUS_INTERNAL;
    } else if(j->operation==PXA_FS_ATOMIC_WRITE) {
        size_t offset=0;
        while(offset<j->data_size && status==PXA_STATUS_OK) {
            size_t n=0;
            if(atomic_load_explicit(&j->cancelled,memory_order_acquire)) {status=PXA_STATUS_CANCELLED;break;}
            status=pxa_status_normalize(b->write(b->context,j->resource,j->data+offset,j->data_size-offset,&n));
            if(status==PXA_STATUS_OK && (!n || n>j->data_size-offset))status=PXA_STATUS_IO_ERROR;
            if(status==PXA_STATUS_OK)offset+=n;
        }
        if(status==PXA_STATUS_OK)j->written+=offset;
    } else if(j->operation==PXA_FS_ATOMIC_COMMIT) {
        status=pxa_status_normalize(j->service->atomic->close_checked(b->context,j->resource,j->kind));
        j->resource=NULL;
        if(status==PXA_STATUS_OK && atomic_load_explicit(&j->cancelled,memory_order_acquire))status=PXA_STATUS_CANCELLED;
        if(status==PXA_STATUS_OK)status=pxa_status_normalize(b->replace(b->context,fs_atomic_temp(j),
            (pxa_bytes_t){j->destination,j->destination_size}));
        if(status==PXA_STATUS_OK)j->retired=1;
    } else if(j->operation==PXA_FS_ATOMIC_ABORT) {
        status=fs_atomic_cleanup(j);
    } else status=PXA_STATUS_INTERNAL;
    if(status!=PXA_STATUS_OK && !j->retired)(void)fs_atomic_cleanup(j);
    j->status=status;
    fs_backend_unlock(j->service);
    atomic_store_explicit(&j->phase,2,memory_order_release);
}
static void fs_atomic_recycle(fs_atomic_job_t *j) {
    j->used=0;j->token=0;j->identity=0;
    if(j->service->atomic_reserved)--j->service->atomic_reserved;
    atomic_store_explicit(&j->phase,0,memory_order_relaxed);
}
static int fs_atomic_request_alive(fs_atomic_job_t *j) {
    return j->token && pxa_request_is_active(j->service->runtime,j->component,j->token) &&
        pxa_request_identity(j->service->runtime,j->component,j->token)==j->identity;
}
static void fs_atomic_poll(pxa_fs_service_t *s) {
    struct fs_atomic_state *a=s->atomic;
    if(!a || a->stopping)return;
    for(unsigned i=0;i<a->capacity;++i) {
        fs_atomic_job_t *j=&a->jobs[i];
        if(!j->used)continue;
        int phase=atomic_load_explicit(&j->phase,memory_order_acquire);
        if(j->token && !fs_atomic_request_alive(j))atomic_store_explicit(&j->cancelled,1,memory_order_release);
        if(phase==2) {
            if(fs_atomic_request_alive(j)) {
                uint8_t body[8];size_t n=0;
                if(j->status==PXA_STATUS_OK) {
                    if(j->operation==PXA_FS_ATOMIC_BEGIN){pxa_write_u64(body,j->ticket);n=8;}
                    if(j->operation==PXA_FS_ATOMIC_WRITE){pxa_write_u32(body,j->data_size);n=4;}
                }
                pxa_status_t status=pxa_request_complete(s->runtime,j->component,j->token,j->status,n?body:NULL,n);
                if(status!=PXA_STATUS_OK) {
                    (void)pxa_request_cancel(s->runtime,j->component,j->token);
                    atomic_store_explicit(&j->cancelled,1,memory_order_release);
                }
            }
            j->token=0;j->identity=0;
            atomic_store_explicit(&j->phase,0,memory_order_relaxed);
            if(j->retired){fs_atomic_recycle(j);continue;}
            phase=0;
        }
        if(phase==0 && atomic_load_explicit(&j->cancelled,memory_order_acquire)) {
            j->operation=PXA_FS_ATOMIC_ABORT;
            atomic_store_explicit(&j->phase,1,memory_order_relaxed);
            if(a->submit(a->executor,fs_atomic_work,j)!=PXA_STATUS_OK)
                atomic_store_explicit(&j->phase,0,memory_order_relaxed);
        }
    }
}
static void fs_atomic_stop(pxa_fs_service_t *s,pxa_component_t component) {
    if(!s->atomic)return;
    for(unsigned i=0;i<s->atomic->capacity;++i) {
        fs_atomic_job_t *j=&s->atomic->jobs[i];
        if(j->used && j->component==component)atomic_store_explicit(&j->cancelled,1,memory_order_release);
    }
}
void pxa_fs_service_atomic_shutdown_begin(pxa_fs_service_t *s) {
    if(!service_valid(s) || !s->atomic)return;
    s->atomic->stopping=1;
    for(unsigned i=0;i<s->atomic->capacity;++i)
        atomic_store_explicit(&s->atomic->jobs[i].cancelled,1,memory_order_release);
}
void pxa_fs_service_atomic_shutdown_finish(pxa_fs_service_t *s) {
    if(!service_valid(s) || !s->atomic || !s->atomic->stopping)return;
    /* Caller drained executor: no job can retain arena/backend pointers now. */
    fs_backend_lock(s);
    for(unsigned i=0;i<s->atomic->capacity;++i) {
        fs_atomic_job_t *j=&s->atomic->jobs[i];
        if(j->used) {if(!j->retired)fs_atomic_cleanup(j);fs_atomic_recycle(j);}
    }
    fs_backend_unlock(s);
}
static int fs_atomic_same(const uint8_t *a,size_t an,const uint8_t *b,size_t bn) {
    return an==bn && !memcmp(a,b,an);
}
static pxa_status_t fs_atomic_control(pxa_fs_service_t *s,pxa_component_t component,const pxa_message_view_t *m) {
    struct fs_atomic_state *a=s->atomic;
    fs_atomic_job_t *j=NULL;
    pxa_authority_t authority=0;
    pxa_status_t status;
    const uint8_t *p=m->payload.data;
    size_t n=m->payload.size;
    if(!a || a->stopping)return PXA_STATUS_UNSUPPORTED;
    fs_atomic_poll(s);
    status=authorize_request(s,component,&authority);
    if(status!=PXA_STATUS_OK)return status;
    if(m->opcode==PXA_FS_ATOMIC_BEGIN) {
        if(n<16 || !p)return PXA_STATUS_INVALID_ARGUMENT;
        size_t tn=pxa_read_u16(p),dn=pxa_read_u16(p+2);
        uint64_t expected=pxa_read_u64(p+8);
        if(pxa_read_u32(p+4) || tn+dn!=n-16 || expected>PXA_FS_ATOMIC_MAX_FILE_BYTES ||
            !pxa_fs_path_is_valid((pxa_bytes_t){p+16,tn}) ||
            !pxa_fs_path_is_valid((pxa_bytes_t){p+16+tn,dn}) ||
            fs_atomic_same(p+16,tn,p+16+tn,dn))return PXA_STATUS_INVALID_ARGUMENT;
        unsigned occupied=s->atomic_reserved;
        for(unsigned i=0;i<s->slot_count;++i)occupied+=s->slots[i].occupied!=0;
        if(occupied>=s->slot_count)return PXA_STATUS_RESOURCE_LIMIT;
        for(unsigned i=0;i<a->capacity;++i) {
            fs_atomic_job_t *q=&a->jobs[i];
            if(!q->used){if(!j)j=q;continue;}
            if(fs_atomic_same(q->temporary,q->temporary_size,p+16,tn) ||
               fs_atomic_same(q->destination,q->destination_size,p+16,tn) ||
               fs_atomic_same(q->temporary,q->temporary_size,p+16+tn,dn) ||
               fs_atomic_same(q->destination,q->destination_size,p+16+tn,dn))return PXA_STATUS_WOULD_BLOCK;
        }
        if(!j)return PXA_STATUS_RESOURCE_LIMIT;
        j->generation++;if(!j->generation)j->generation++;
        j->ticket=((uint64_t)j->generation<<32)|(uint64_t)(j-a->jobs+1);
        j->service=s;j->component=component;j->used=1;j->retired=0;j->resource=NULL;
        j->temporary_size=(uint16_t)tn;j->destination_size=(uint16_t)dn;
        memcpy(j->temporary,p+16,tn);memcpy(j->destination,p+16+tn,dn);
        j->expected=expected;j->written=0;
        atomic_store_explicit(&j->cancelled,0,memory_order_relaxed);
        ++s->atomic_reserved;
    } else {
        if(!p || n<8)return PXA_STATUS_INVALID_ARGUMENT;
        uint64_t ticket=pxa_read_u64(p);
        uint32_t index=(uint32_t)ticket;
        if(!index || index>a->capacity)return PXA_STATUS_NOT_FOUND;
        j=&a->jobs[index-1];
        if(!j->used || j->ticket!=ticket || j->component!=component ||
            atomic_load_explicit(&j->cancelled,memory_order_acquire))return PXA_STATUS_NOT_FOUND;
        if(atomic_load_explicit(&j->phase,memory_order_acquire)!=0)return PXA_STATUS_WOULD_BLOCK;
        if(m->opcode==PXA_FS_ATOMIC_WRITE) {
            if(n<=8 || n-8>PXA_FS_ATOMIC_MAX_CHUNK || n-8>j->expected-j->written)return PXA_STATUS_INVALID_ARGUMENT;
            j->data_size=(uint16_t)(n-8);memcpy(j->data,p+8,n-8);
        } else if(n!=8)return PXA_STATUS_INVALID_ARGUMENT;
        if(m->opcode==PXA_FS_ATOMIC_COMMIT && j->written!=j->expected)return PXA_STATUS_BAD_STATE;
    }
    size_t capacity=m->opcode==PXA_FS_ATOMIC_BEGIN?8:m->opcode==PXA_FS_ATOMIC_WRITE?4:0;
    status=pxa_request_begin_reserved(s->runtime,component,m->request_token,PXA_FS_SERVICE_ID,m->opcode,authority,capacity);
    if(status!=PXA_STATUS_OK) {if(m->opcode==PXA_FS_ATOMIC_BEGIN)fs_atomic_recycle(j);return status;}
    status=pxa_request_commit(s->runtime,component,m->request_token);
    if(status!=PXA_STATUS_OK) {if(m->opcode==PXA_FS_ATOMIC_BEGIN)fs_atomic_recycle(j);return status;}
    j->operation=m->opcode;j->token=m->request_token;
    j->identity=pxa_request_identity(s->runtime,component,j->token);
    atomic_store_explicit(&j->phase,1,memory_order_release);
    status=a->submit(a->executor,fs_atomic_work,j);
    if(status!=PXA_STATUS_OK) {
        atomic_store_explicit(&j->phase,0,memory_order_relaxed);j->token=0;j->identity=0;
        if(m->opcode==PXA_FS_ATOMIC_BEGIN)fs_atomic_recycle(j);
        (void)pxa_request_complete(s->runtime,component,m->request_token,status,NULL,0);
    }
    return PXA_STATUS_OK;
}

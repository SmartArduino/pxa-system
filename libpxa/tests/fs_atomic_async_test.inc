/* SPDX-License-Identifier: MIT
 * Reuse the Core pressure/wire harness, but defer native execution so owner
 * progress, payload ownership, cancellation and publication are observable. */
typedef struct {
    test_fs_backend_t base;
    unsigned removed,published,depth,writes;
    unsigned invalid_write_count;
    pxa_status_t close_error,write_error,replace_error,remove_error;
    uint8_t committed[64];size_t committed_size;
} atomic_fs_test_t;
static void atomic_fs_enter(void *context) {++((atomic_fs_test_t *)context)->depth;}
static void atomic_fs_leave(void *context) {assert(((atomic_fs_test_t *)context)->depth--);}
static pxa_status_t atomic_fs_write(void *context,void *resource,const uint8_t *data,size_t size,size_t *written) {
    atomic_fs_test_t *b=context;assert(b->depth);++b->writes;
    if(b->write_error)return b->write_error;
    if(b->invalid_write_count) {
        *written=b->invalid_write_count==1?0:size+1;
        return PXA_STATUS_OK;
    }
    /* Force short native writes: one Guest result must cover its whole chunk. */
    return fs_write(&b->base,resource,data,size>3?3:size,written);
}
static pxa_status_t atomic_fs_close(void *context,void *resource,uint8_t kind) {
    atomic_fs_test_t *b=context;assert(b->depth);fs_close(&b->base,resource,kind);return b->close_error;
}
static pxa_status_t atomic_fs_remove(void *context,pxa_bytes_t path) {
    atomic_fs_test_t *b=context;assert(b->depth && test_path_equal(path,"notes/today.txt"));++b->removed;return b->remove_error;
}
static pxa_status_t atomic_fs_replace(void *context,pxa_bytes_t source,pxa_bytes_t destination) {
    atomic_fs_test_t *b=context;assert(b->depth && test_path_equal(source,"notes/today.txt") && test_path_equal(destination,"world.bin"));
    if(b->replace_error)return b->replace_error;
    ++b->published;memcpy(b->committed,b->base.file.bytes,b->base.file.size);b->committed_size=b->base.file.size;return 0;
}
static pxa_status_t atomic_fs_dispatch(test_runtime_t *t,pxa_component_t c,uint16_t op,uint64_t token,uint64_t session,const char *data) {
    uint8_t body[64],command[128];size_t n=0;
    if(op==PXA_FS_ATOMIC_BEGIN) {
        pxa_write_u16(body,15);pxa_write_u16(body+2,9);pxa_write_u32(body+4,0);
        pxa_write_u64(body+8,data?strlen(data):0);
        memcpy(body+16,"notes/today.txt",15);memcpy(body+31,"world.bin",9);n=40;
    } else {
        pxa_write_u64(body,session);n=8;
        if(data){memcpy(body+n,data,strlen(data));n+=strlen(data);}
    }
    pxa_writer_t writer;pxa_writer_init(&writer,command,sizeof(command));
    assert(!pxa_writer_message(&writer,PXA_FS_SERVICE_ID,op,token,body,n));
    assert(!pxa_component_begin_event(t->runtime,c));
    pxa_status_t status=pxa_runtime_control(t->runtime,c,command,writer.size);
    assert(!pxa_component_finish_event(t->runtime,c,1));
    memset(body,0xa5,sizeof(body));memset(command,0xa5,sizeof(command));return status;
}
static uint64_t atomic_fs_completion(test_runtime_t *t,pxa_component_t c,uint64_t token,pxa_status_t expected) {
    uint8_t data[96];pxa_event_view_t v;pxa_message_view_t m;
    size_t n=read_head_event(t->runtime,c,data,sizeof(data),&v);
    assert(!pxa_message_decode(data,n,PXA_MAX_CONTROL_MESSAGE,&m));
    assert(m.request_token==token && (int32_t)pxa_read_u32(m.payload.data)==expected);
    uint64_t result=m.payload.size==12?pxa_read_u64(m.payload.data+4):m.payload.size==8?pxa_read_u32(m.payload.data+4):0;
    assert(!pxa_event_consume(t->runtime,c,v.token));return result;
}
static void test_fs_atomic_async(void) {
    test_runtime_t t=make_runtime(2,12,12,4,16,10,2);
    pxa_component_t c=create_started(t.runtime,11),other=create_started(t.runtime,12);
    atomic_fs_test_t b={0};test_storage_executor_t executor={0};pxa_fs_config_t cfg={0};
    pxa_fs_service_t *fs=NULL;cfg.struct_size=sizeof(cfg);cfg.max_open_resources=1;
    cfg.backend=(pxa_fs_backend_t){.struct_size=sizeof(cfg.backend),.context=&b,
        .open=fs_open,.make_directory=fs_mutation,.remove=atomic_fs_remove,.rename=fs_rename,
        .stat=fs_stat,.read=fs_read,.write=atomic_fs_write,.seek=fs_seek,
        .read_directory=fs_read_directory,.close=fs_close,.replace=atomic_fs_replace};
    void *memory=malloc(pxa_fs_service_workspace_size(&cfg));
    assert(!pxa_fs_service_init(memory,pxa_fs_service_workspace_size(&cfg),t.runtime,&cfg,&fs));
    size_t jobs_size=pxa_fs_service_atomic_workspace_size(1);void *jobs=malloc(jobs_size);
    assert(!pxa_fs_service_atomic_workspace_size(0));
    assert(pxa_fs_service_enable_async_atomic(fs,jobs,jobs_size-1,1,atomic_fs_close,storage_submit,&executor,atomic_fs_enter,atomic_fs_leave,&b)==PXA_STATUS_INVALID_ARGUMENT);
    assert(!pxa_fs_service_enable_async_atomic(fs,jobs,jobs_size,1,atomic_fs_close,storage_submit,&executor,atomic_fs_enter,atomic_fs_leave,&b));
    assert(!pxa_fs_service_register(fs));
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,201,0,"hello world");
    assert(!b.base.opens && executor.count==1);
    /* Rendering/input turns can progress while a backend job has not run. */
    for(int i=0;i<20;++i){assert(!pxa_component_begin_event(t.runtime,c));assert(!pxa_component_finish_event(t.runtime,c,1));pxa_fs_service_poll(fs);}
    storage_execute_one(&executor);assert(b.base.opens==1);pxa_fs_service_poll(fs);
    uint64_t ticket=atomic_fs_completion(&t,c,201,0);assert(ticket>>32);
    assert(atomic_fs_dispatch(&t,other,PXA_FS_ATOMIC_WRITE,202,ticket,"bad")==PXA_STATUS_NOT_FOUND);
    assert(atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_COMMIT,203,ticket,NULL)==PXA_STATUS_BAD_STATE);
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_WRITE,204,ticket,"hello world");
    assert(atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_COMMIT,205,ticket,NULL)==PXA_STATUS_WOULD_BLOCK);
    storage_execute_one(&executor);pxa_fs_service_poll(fs);
    assert(atomic_fs_completion(&t,c,204,0)==11 && b.writes==4 && !b.published);
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_COMMIT,206,ticket,NULL);
    assert(!b.published);storage_execute_one(&executor);pxa_fs_service_poll(fs);atomic_fs_completion(&t,c,206,0);
    assert(b.published==1 && b.committed_size==11 && !memcmp(b.committed,"hello world",11));
    assert(atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_ABORT,207,ticket,NULL)==PXA_STATUS_NOT_FOUND);
    /* Close failure cannot publish and must consume the native resource. */
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,208,0,NULL);storage_execute_one(&executor);pxa_fs_service_poll(fs);ticket=atomic_fs_completion(&t,c,208,0);
    b.close_error=PXA_STATUS_IO_ERROR;atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_COMMIT,209,ticket,NULL);
    storage_execute_one(&executor);pxa_fs_service_poll(fs);atomic_fs_completion(&t,c,209,PXA_STATUS_IO_ERROR);
    assert(b.published==1 && b.removed==1);b.close_error=0;
    /* Cancelled begin with token reuse cannot deliver a stale session ID. */
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,210,0,NULL);
    assert(!pxa_request_complete(t.runtime,c,210,PXA_STATUS_CANCELLED,NULL,0));atomic_fs_completion(&t,c,210,PXA_STATUS_CANCELLED);
    pxa_fs_service_poll(fs);storage_execute_one(&executor);pxa_fs_service_poll(fs);
    assert(b.published==1 && b.removed==2);
    executor.reject=PXA_STATUS_WOULD_BLOCK;
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,210,0,NULL);atomic_fs_completion(&t,c,210,PXA_STATUS_WOULD_BLOCK);executor.reject=0;
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,210,0,NULL);storage_execute_one(&executor);pxa_fs_service_poll(fs);ticket=atomic_fs_completion(&t,c,210,0);
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_ABORT,211,ticket,NULL);storage_execute_one(&executor);pxa_fs_service_poll(fs);atomic_fs_completion(&t,c,211,0);
    assert(b.removed==3);
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,213,0,"unsafe");storage_execute_one(&executor);pxa_fs_service_poll(fs);ticket=atomic_fs_completion(&t,c,213,0);
    b.write_error=PXA_STATUS_QUOTA_EXCEEDED;atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_WRITE,214,ticket,"unsafe");
    storage_execute_one(&executor);pxa_fs_service_poll(fs);atomic_fs_completion(&t,c,214,PXA_STATUS_QUOTA_EXCEEDED);
    assert(b.published==1 && b.removed==4 && !memcmp(b.committed,"hello world",11));b.write_error=0;
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,215,0,NULL);storage_execute_one(&executor);pxa_fs_service_poll(fs);ticket=atomic_fs_completion(&t,c,215,0);
    b.replace_error=PXA_STATUS_IO_ERROR;atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_COMMIT,216,ticket,NULL);
    storage_execute_one(&executor);pxa_fs_service_poll(fs);atomic_fs_completion(&t,c,216,PXA_STATUS_IO_ERROR);
    assert(b.published==1 && b.removed==5 && !memcmp(b.committed,"hello world",11));b.replace_error=0;
    /* Idle session cleanup on Component stop requires no new Guest request. */
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,217,0,NULL);storage_execute_one(&executor);pxa_fs_service_poll(fs);ticket=atomic_fs_completion(&t,c,217,0);
    assert(!pxa_component_request_stop(t.runtime,c,PXA_STOP_NORMAL));
    assert(!pxa_component_begin_stop(t.runtime,c));assert(!pxa_component_finish_stop(t.runtime,c));
    pxa_fs_service_poll(fs);assert(executor.count==1);storage_execute_one(&executor);pxa_fs_service_poll(fs);
    assert(b.published==1 && b.removed==6);
    c=other;
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,218,0,NULL);storage_execute_one(&executor);pxa_fs_service_poll(fs);ticket=atomic_fs_completion(&t,c,218,0);
    b.remove_error=PXA_STATUS_IO_ERROR;atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_ABORT,219,ticket,NULL);
    storage_execute_one(&executor);pxa_fs_service_poll(fs);atomic_fs_completion(&t,c,219,PXA_STATUS_IO_ERROR);
    assert(b.published==1 && b.removed==7);b.remove_error=0;
    /* A stalled/malformed backend must terminate the session, not spin or
     * publish a truncated file. The next save must still be admitted. */
    for(unsigned invalid=1;invalid<=2;++invalid) {
        atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,220,0,"new");
        storage_execute_one(&executor);pxa_fs_service_poll(fs);
        ticket=atomic_fs_completion(&t,c,220,0);
        b.invalid_write_count=invalid;
        atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_WRITE,221,ticket,"new");
        storage_execute_one(&executor);pxa_fs_service_poll(fs);
        atomic_fs_completion(&t,c,221,PXA_STATUS_IO_ERROR);
        assert(b.published==1 && !memcmp(b.committed,"hello world",11));
        assert(atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_COMMIT,222,ticket,NULL)==PXA_STATUS_NOT_FOUND);
        b.invalid_write_count=0;
    }
    /* Backpressure at WRITE/COMMIT preserves both the session and byte
     * accounting. Reusing tokens and slots across many saves must not make
     * a stale ticket valid again or exhaust the single-handle budget. */
    uint64_t previous_ticket=ticket;
    for(unsigned iteration=0;iteration<128;++iteration) {
        atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,223,0,"again");
        storage_execute_one(&executor);pxa_fs_service_poll(fs);
        ticket=atomic_fs_completion(&t,c,223,0);
        assert(ticket!=previous_ticket);
        assert(atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_ABORT,224,previous_ticket,NULL)==PXA_STATUS_NOT_FOUND);
        executor.reject=PXA_STATUS_WOULD_BLOCK;
        atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_WRITE,225,ticket,"again");
        atomic_fs_completion(&t,c,225,PXA_STATUS_WOULD_BLOCK);
        executor.reject=0;
        atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_WRITE,225,ticket,"again");
        storage_execute_one(&executor);pxa_fs_service_poll(fs);
        assert(atomic_fs_completion(&t,c,225,0)==5);
        executor.reject=PXA_STATUS_WOULD_BLOCK;
        atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_COMMIT,226,ticket,NULL);
        atomic_fs_completion(&t,c,226,PXA_STATUS_WOULD_BLOCK);
        executor.reject=0;
        atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_COMMIT,226,ticket,NULL);
        storage_execute_one(&executor);pxa_fs_service_poll(fs);
        atomic_fs_completion(&t,c,226,0);
        assert(b.published==2+iteration && b.committed_size==5 && !memcmp(b.committed,"again",5));
        assert(!executor.count && !b.depth);
        previous_ticket=ticket;
    }
    /* Host cancellation observed before a queued commit prevents publish;
     * rejection of cleanup admission must be retried by a later owner poll. */
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,227,0,NULL);
    storage_execute_one(&executor);pxa_fs_service_poll(fs);
    ticket=atomic_fs_completion(&t,c,227,0);
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_COMMIT,228,ticket,NULL);
    assert(!pxa_request_complete(t.runtime,c,228,PXA_STATUS_CANCELLED,NULL,0));
    atomic_fs_completion(&t,c,228,PXA_STATUS_CANCELLED);
    pxa_fs_service_poll(fs);storage_execute_one(&executor);pxa_fs_service_poll(fs);
    assert(b.published==129 && b.removed==10 && !memcmp(b.committed,"again",5));
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,229,0,NULL);
    storage_execute_one(&executor);
    assert(!pxa_request_complete(t.runtime,c,229,PXA_STATUS_CANCELLED,NULL,0));
    atomic_fs_completion(&t,c,229,PXA_STATUS_CANCELLED);
    executor.reject=PXA_STATUS_WOULD_BLOCK;pxa_fs_service_poll(fs);
    assert(!executor.count && b.removed==10);
    executor.reject=0;pxa_fs_service_poll(fs);assert(executor.count==1);
    storage_execute_one(&executor);pxa_fs_service_poll(fs);assert(b.removed==11);
    /* Teardown after Runtime destruction drains owned jobs without Core APIs. */
    atomic_fs_dispatch(&t,c,PXA_FS_ATOMIC_BEGIN,212,0,"future");
    pxa_fs_service_atomic_shutdown_begin(fs);destroy_runtime(&t);
    storage_execute_one(&executor);pxa_fs_service_atomic_shutdown_finish(fs);
    assert(!executor.count && !b.depth && b.published==129 && b.removed==12);
    free(jobs);free(memory);
    puts("FS async: owner progress, copied chunks, short/invalid writes, ownership, checked close, cancelled commit/cleanup retry, 128 saves with backpressure/stale tickets and shutdown passed");
}

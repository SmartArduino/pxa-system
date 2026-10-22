/* Included by core_test.c to reuse the real Core wire/pressure harness. */
typedef struct { unsigned opens,closes,prompts,cancels,directory_reads;
    int directory,file,private_denied,grant_revoked,volume_unavailable;
    pxa_bytes_t directory_name;
} roots_test_t;
static pxa_status_t roots_test_authorize(void *c,pxa_component_t component,pxa_authority_t *authority) {
    roots_test_t *r=c; (void)component;
    if(r->private_denied) return PXA_STATUS_DENIED;
    *authority=707; return PXA_STATUS_OK;
}
static pxa_status_t roots_test_policy(void *c,pxa_bytes_t identity,pxa_bytes_t name,
    pxa_bytes_t scope,pxa_permission_decision_t *d) {
    (void)c;(void)identity;(void)name;(void)scope; *d=PXA_PERMISSION_ALLOW; return 0;
}
static pxa_status_t roots_test_save(void *c,pxa_bytes_t identity,pxa_bytes_t name,
    pxa_bytes_t scope,pxa_permission_decision_t d) {
    (void)c;(void)identity;(void)name;(void)scope;(void)d;return 0;
}
static pxa_status_t roots_test_open(void *c,uint8_t kind,const uint8_t id[16],void **p,uint32_t *rights) {
    roots_test_t *r=c;
    if((kind!=4 || id[0]!=7) && kind!=PXA_FS_ROOT_APP_MEDIA) return PXA_STATUS_DENIED;
    ++r->opens; *p=&r->directory; *rights=3; return 0;
}
static pxa_status_t roots_test_at(void *c,void *d,pxa_bytes_t path,uint32_t flags,
    void **p,pxa_fs_entry_t *info,uint64_t *features) {
    roots_test_t *r=c; assert(d==&r->directory && path.size);
    if(flags!=1) return PXA_STATUS_DENIED;
    ++r->opens; *p=&r->file; info->kind=1; info->size=123; *features=0; return 0;
}
static pxa_status_t roots_test_info(void *c,void *d,pxa_fs_root_info_t *i) {
    roots_test_t *r=c; assert(d==&r->directory);
    if(r->grant_revoked)return PXA_STATUS_DENIED;
    memset(i,0,sizeof(*i)); i->volume_id.bytes[0]=1; i->mount_epoch=19;
    i->rights=3; i->max_path_bytes=255; i->max_segment_bytes=255;
    i->naming=2; i->state=r->volume_unavailable?PXA_VOLUME_OFFLINE:PXA_VOLUME_READY; return 0;
}
static pxa_status_t roots_test_media_path(void *context,char *out,size_t capacity,size_t *size) {
    (void)context;const char path[]="/PXA/media/lucinhu/pxa-nes/";
    assert(capacity>=sizeof(path)-1);memcpy(out,path,sizeof(path)-1);*size=sizeof(path)-1;return PXA_STATUS_OK;
}
static void roots_test_close(void *c,void *r,uint8_t kind) {
    roots_test_t *t=c; (void)r;(void)kind; ++t->closes;
}
static pxa_status_t roots_test_read(void *c,void *r,uint8_t *p,size_t n,size_t *out) {
    (void)c;(void)r; if(n) p[0]=42; *out=n?1:0; return 0;
}
static pxa_status_t roots_test_seek(void *c,void *r,int64_t p,uint8_t origin,uint64_t *out) {
    (void)c;(void)r;(void)origin; *out=(uint64_t)p; return 0;
}
static pxa_status_t roots_test_read_at(void *c,void *r,uint64_t offset,
    uint8_t *p,size_t n,size_t *out) {
    (void)c;(void)r;assert(offset==3 && n==4);memcpy(p,"test",4);*out=4;return 0;
}
static pxa_status_t roots_test_next(void *c,void *r,pxa_fs_entry_t *i,uint8_t *end) {
    roots_test_t *t=c;(void)r;++t->directory_reads;
    if(t->directory_name.size) {*end=0;i->name=t->directory_name;i->kind=PXA_FS_KIND_REGULAR;i->size=0;}
    else *end=1;
    return 0;
}
static pxa_status_t roots_test_picker(void *c,pxa_component_t component,
    pxa_request_token_t token,pxa_bytes_t purpose,uint32_t rights,uint8_t persistent) {
    roots_test_t *r=c; (void)component;(void)token;
    assert(purpose.size==4 && rights==3 && persistent==1); ++r->prompts; return 0;
}
static void roots_test_cancel(void *c,pxa_component_t component,pxa_request_token_t token) {
    roots_test_t *r=c; (void)component;(void)token; ++r->cancels;
}
static void test_fs_scoped_service(void) {
    test_runtime_t test=make_runtime(1,12,12,12,16,10,2);
    roots_test_t state={0}; test_fs_backend_t legacy={0};
    pxa_fs_config_t fc={0}; pxa_fs_roots_backend_t roots={0};
    pxa_permission_config_t pc={0}; pxa_permission_declaration_t declaration={0};
    pxa_permission_service_t *permissions; pxa_fs_service_t *fs;
    void *pw,*fw; size_t n; pxa_component_t c;
    uint8_t command[256],event_bytes[256],p[64]={0},byte,id[16]={7};
    pxa_writer_t writer; pxa_message_view_t event; pxa_event_view_t view;
    uint64_t permission,root,file;
    assert(!pxa_component_create(test.runtime,999,&c));
    assert(!pxa_component_begin_start(test.runtime,c));
    assert(!pxa_component_set_wire_profile(test.runtime,c,1));
    assert(!pxa_component_validate_import(test.runtime,c));
    assert(!pxa_component_finish_start(test.runtime,c,0));
    declaration.name=(pxa_bytes_t){(const uint8_t *)"fs.user-directory",17};
    declaration.scope=(pxa_bytes_t){(const uint8_t *)"read",4};
    pc.struct_size=sizeof(pc); pc.app_identity=(pxa_bytes_t){(const uint8_t *)"app",3};
    pc.declarations=&declaration;pc.declaration_count=1;pc.max_authorities=2;
    pc.store.struct_size=sizeof(pc.store);pc.store.load=roots_test_policy;pc.store.save=roots_test_save;
    pw=malloc(pxa_permission_service_workspace_size(&pc)); assert(pw);
    assert(!pxa_permission_service_init(pw,pxa_permission_service_workspace_size(&pc),test.runtime,&pc,&permissions));
    assert(!pxa_permission_policy_load(permissions)); assert(!pxa_permission_service_register(permissions));
    n=make_permission_request(command,sizeof(command),2,1,declaration.name.data,17,declaration.scope.data,4);
    dispatch_control_completion(test.runtime,c,command,n,event_bytes,sizeof(event_bytes),&event);
    assert(event.payload.size==12 && !pxa_read_u32(event.payload.data));
    permission=pxa_read_u64(event.payload.data+4);
    roots.struct_size=sizeof(roots);roots.context=&state;
    roots.files.struct_size=sizeof(roots.files);roots.files.context=&state;
    roots.files.read=roots_test_read;roots.files.seek=roots_test_seek;
    roots.files.read_directory=roots_test_next;roots.files.close=roots_test_close;
    roots.open_root=roots_test_open;roots.open_at=roots_test_at;roots.query_root=roots_test_info;
    roots.request_access=roots_test_picker;roots.cancel_access=roots_test_cancel;
    roots.features=PXA_FS_FEATURE_APP_MEDIA_ROOT|PXA_FS_FEATURE_APP_MEDIA_PATH;
    roots.app_media_path=roots_test_media_path;
    roots.read_at=roots_test_read_at;
    fc.struct_size=sizeof(fc);fc.max_open_resources=4;fc.roots=&roots;fc.permissions=permissions;
    fc.authorization_context=&state;fc.authorize=roots_test_authorize;
    fc.backend.struct_size=sizeof(fc.backend);fc.backend.context=&legacy;
    fc.backend.open=fs_open;fc.backend.make_directory=fs_mutation;fc.backend.remove=fs_mutation;
    fc.backend.rename=fs_rename;fc.backend.stat=fs_stat;fc.backend.read=fs_read;
    fc.backend.write=fs_write;fc.backend.seek=fs_seek;fc.backend.read_directory=fs_read_directory;fc.backend.close=fs_close;
    fw=malloc(pxa_fs_service_workspace_size(&fc));assert(fw);
    assert(!pxa_fs_service_init(fw,pxa_fs_service_workspace_size(&fc),test.runtime,&fc,&fs));
    assert(!pxa_fs_service_register(fs));
    pxa_writer_init(&writer,command,sizeof(command));assert(!pxa_writer_message(&writer,5,PXA_FS_APP_MEDIA_PATH,99,NULL,0));
    dispatch_control_completion(test.runtime,c,command,writer.size,event_bytes,sizeof(event_bytes),&event);
    const char label[]="/PXA/media/lucinhu/pxa-nes/";
    assert(event.payload.size==6+sizeof(label)-1 && !pxa_read_u32(event.payload.data));
    assert(pxa_read_u16(event.payload.data+4)==sizeof(label)-1 && !memcmp(event.payload.data+6,label,sizeof(label)-1));
    assert(!state.opens && !state.prompts); /* no card access or authority */
    roots.struct_size=offsetof(pxa_fs_roots_backend_t,app_media_path);
    assert(!pxa_component_begin_event(test.runtime,c));
    assert(pxa_runtime_control(test.runtime,c,command,writer.size)==PXA_STATUS_UNSUPPORTED);
    assert(!pxa_component_finish_event(test.runtime,c,0));
    roots.struct_size=sizeof(roots);
    p[0]=4;pxa_write_u64(p+4,permission);memcpy(p+12,id,16);
    pxa_writer_init(&writer,command,sizeof(command));assert(!pxa_writer_message(&writer,5,9,2,p,28));
    dispatch_control_completion(test.runtime,c,command,writer.size,event_bytes,sizeof(event_bytes),&event);
    assert(event.payload.size==16 && !pxa_read_u32(event.payload.data));
    root=pxa_read_u64(event.payload.data+4);assert(pxa_read_u32(event.payload.data+12)==3);
    pxa_write_u64(p,root);pxa_writer_init(&writer,command,sizeof(command));
    assert(!pxa_writer_message(&writer,5,17,3,p,8));
    dispatch_control_completion(test.runtime,c,command,writer.size,event_bytes,sizeof(event_bytes),&event);
    assert(event.payload.size==76 && pxa_read_u64(event.payload.data+20)==19);
    pxa_write_u32(p+8,1);pxa_write_u16(p+12,1);p[14]='x';
    pxa_writer_init(&writer,command,sizeof(command));assert(!pxa_writer_message(&writer,5,11,4,p,15));
    dispatch_control_completion(test.runtime,c,command,writer.size,event_bytes,sizeof(event_bytes),&event);
    assert(event.payload.size==32 && !pxa_read_u32(event.payload.data));
    file=pxa_read_u64(event.payload.data+4);assert(pxa_read_u64(event.payload.data+24)==123);
    assert(dispatch_io64(test.runtime,c,file,1,&byte,1)==1 && byte==42);
    assert(dispatch_io64(test.runtime,c,file,2,&byte,1)==PXA_STATUS_DENIED);
    {
        uint8_t packet[28]={0};pxa_write_u64(packet,3);pxa_write_u64(packet+8,1);
        pxa_write_u32(packet+16,4);
        assert(dispatch_io64(test.runtime,c,file,16,packet,28)==4);
        assert(!memcmp(packet+24,"test",4));
        assert(dispatch_io64(test.runtime,c,file,16,packet,28)==PXA_STATUS_CANCELLED);
        pxa_write_u64(packet+8,2);packet[20]=1;
        assert(dispatch_io64(test.runtime,c,file,16,packet,28)==PXA_STATUS_INVALID_ARGUMENT);
        packet[20]=0;assert(dispatch_io64(test.runtime,c,file,16,packet,28)==4);
        pxa_write_u64(packet,3);assert(!dispatch_io64(test.runtime,c,file,17,packet,8));
        assert(!dispatch_io64(test.runtime,c,file,17,packet,8));
        pxa_write_u64(packet,3);pxa_write_u64(packet+8,3);
        assert(dispatch_io64(test.runtime,c,file,16,packet,28)==PXA_STATUS_CANCELLED);
        roots.struct_size=offsetof(pxa_fs_roots_backend_t,read_at);
        pxa_write_u64(packet+8,4);
        assert(dispatch_io64(test.runtime,c,file,16,packet,28)==PXA_STATUS_UNSUPPORTED);
    }
    assert(!pxa_handle_close64(test.runtime,c,root));
    assert(dispatch_io64(test.runtime,c,file,1,&byte,1)==1);
    assert(!pxa_handle_close64(test.runtime,c,permission));
    assert(dispatch_io64(test.runtime,c,file,1,&byte,1)==PXA_STATUS_NOT_FOUND);
    assert(state.opens==2 && state.closes==2);
    /* Reacquire a fresh authority, then cancel a queued trusted picker. */
    n=make_permission_request(command,sizeof(command),2,5,declaration.name.data,17,declaration.scope.data,4);
    dispatch_control_completion(test.runtime,c,command,n,event_bytes,sizeof(event_bytes),&event);
    permission=pxa_read_u64(event.payload.data+4);
    memset(p,0,sizeof(p));pxa_write_u64(p,permission);pxa_write_u32(p+8,3);
    pxa_write_u32(p+12,1);pxa_write_u16(p+16,4);memcpy(p+18,"data",4);
    pxa_writer_init(&writer,command,sizeof(command));assert(!pxa_writer_message(&writer,5,10,6,p,22));
    assert(!pxa_component_begin_event(test.runtime,c));
    assert(!pxa_runtime_control(test.runtime,c,command,writer.size));
    assert(!pxa_component_finish_event(test.runtime,c,0));assert(state.prompts==1);
    assert(!pxa_request_cancel(test.runtime,c,6));pxa_fs_service_poll(fs);
    assert(state.cancels==1);
    assert(pxa_fs_access_complete(fs,c,6,0,id)==PXA_STATUS_NOT_FOUND);
    assert(state.opens==2);
    n=read_head_event(test.runtime,c,event_bytes,sizeof(event_bytes),&view);
    assert(!pxa_message_decode(event_bytes,n,PXA_MAX_CONTROL_MESSAGE,&event));
    assert(event.request_token==6 && (int32_t)pxa_read_u32(event.payload.data)==PXA_STATUS_CANCELLED);
    assert(!pxa_event_consume(test.runtime,c,view.token));
    /* App-owned media is independent of the user-directory authority and
     * never creates a picker. Nonzero permission/id must still be rejected. */
    assert(!pxa_handle_close64(test.runtime,c,permission));
    memset(p,0,sizeof(p));p[0]=PXA_FS_ROOT_APP_MEDIA;
    state.private_denied=1;
    pxa_writer_init(&writer,command,sizeof(command));assert(!pxa_writer_message(&writer,5,9,7,p,28));
    dispatch_control_completion(test.runtime,c,command,writer.size,event_bytes,sizeof(event_bytes),&event);
    assert(event.payload.size==4 && (int32_t)pxa_read_u32(event.payload.data)==PXA_STATUS_DENIED);
    assert(state.opens==2);
    state.private_denied=0;
    pxa_writer_init(&writer,command,sizeof(command));assert(!pxa_writer_message(&writer,5,9,8,p,28));
    dispatch_control_completion(test.runtime,c,command,writer.size,event_bytes,sizeof(event_bytes),&event);
    assert(event.payload.size==16 && !pxa_read_u32(event.payload.data));
    root=pxa_read_u64(event.payload.data+4);assert(state.prompts==1 && state.opens==3);
    /* Cached entries are still subject to current grant and mount state. */
    uint8_t long_name[768];memset(long_name,'n',sizeof(long_name));
    state.directory_name=(pxa_bytes_t){long_name,sizeof(long_name)};
    for(unsigned change=1;change<=2;++change) {
        uint8_t filler[1500]={0},filler_payload[1472]={0};pxa_writer_t fill_writer;
        pxa_writer_init(&fill_writer,filler,sizeof(filler));
        assert(!pxa_writer_message(&fill_writer,2,1,0,filler_payload,sizeof(filler_payload)));
        assert(!pxa_event_post(test.runtime,c,filler,fill_writer.size,1,0));
        uint8_t handle[8];pxa_write_u64(handle,root);
        pxa_writer_init(&writer,command,sizeof(command));
        assert(!pxa_writer_message(&writer,5,PXA_FS_READ_DIRECTORY,200+change,handle,sizeof(handle)));
        assert(!pxa_component_begin_event(test.runtime,c));
        assert(pxa_runtime_control(test.runtime,c,command,writer.size)==PXA_STATUS_RESOURCE_LIMIT);
        assert(!pxa_component_finish_event(test.runtime,c,1));
        unsigned reads=state.directory_reads;
        assert(!pxa_event_peek(test.runtime,c,&view));assert(!pxa_event_consume(test.runtime,c,view.token));
        state.grant_revoked=change==1;state.volume_unavailable=change==2;
        dispatch_control_completion(test.runtime,c,command,writer.size,event_bytes,sizeof(event_bytes),&event);
        assert(event.payload.size==4 && (int32_t)pxa_read_u32(event.payload.data)==
            (change==1?PXA_STATUS_DENIED:PXA_STATUS_UNAVAILABLE));
        assert(state.directory_reads==reads);
        state.grant_revoked=state.volume_unavailable=0;
    }
    state.directory_name=(pxa_bytes_t){NULL,0};
    assert(!pxa_handle_close64(test.runtime,c,root));
    p[12]=7;
    pxa_writer_init(&writer,command,sizeof(command));assert(!pxa_writer_message(&writer,5,9,9,p,28));
    assert(!pxa_component_begin_event(test.runtime,c));
    assert(pxa_runtime_control(test.runtime,c,command,writer.size)==PXA_STATUS_INVALID_ARGUMENT);
    assert(!pxa_component_finish_event(test.runtime,c,0));
    assert(state.opens==3 && state.closes==3);
    (void)view;destroy_runtime(&test);free(fw);free(pw);
}

// SPDX-License-Identifier: MIT
import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,readFileSync,rmSync,writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {browserRequestUrl,installBrowserNetwork} from './network.mjs';
test('native URL policy upgrades only within signed authority and preserves loopback tests',()=>{
  assert.equal(browserRequestUrl('http://music.163.com/api/test',['web'],true).href,'https://music.163.com/api/test');
  assert.equal(browserRequestUrl('http://provider.test/api',['http://provider.test'],true).protocol,'http:');
  assert.equal(browserRequestUrl('http://provider.test/api',['http://provider.test','https://provider.test'],true).protocol,'https:');
  assert.equal(browserRequestUrl('http://127.0.0.1/test',['web'],true).protocol,'http:');
  for(const url of ['file:///etc/passwd','https://user:password@example.com/','https://foreign.test/'])assert.throws(()=>browserRequestUrl(url,['https://example.com'],true));
});
test('HTTP transport fetches upstream locally, omits cookies/forbidden headers and ignores legacy proxy',async()=>{
  const previous=globalThis.fetch,requests=[],headers=[],received=[];let resolve,heapAt=128;
  const finished=new Promise(r=>resolve=r),host={HEAPU8:new Uint8Array(65536),_malloc:n=>{const p=heapAt;heapAt+=Math.max(8,n);return p;},_free(){},
    _browser_net_header:(id,p,n,q,s)=>{headers.push([new TextDecoder().decode(host.HEAPU8.slice(p,p+n)),new TextDecoder().decode(host.HEAPU8.slice(q,q+s))]);return 1;},
    _browser_net_headers:(id,status)=>assert.equal(status,206),_browser_net_receive:(id,p,n)=>{received.push([...host.HEAPU8.slice(p,p+n)]);return n;},_browser_net_done:(id,status)=>resolve(status)};
  globalThis.fetch=async(url,options)=>{requests.push({url:String(url),options});return new Response(new Uint8Array([1,2,3]),{status:206,headers:{'content-type':'audio/mpeg'}});};
  const net=installBrowserNetwork(host,['web'],{url:'https://store.test/forbidden-relay',csrf:'must-not-leave'});
  try{
    host.browserNetStart({id:1,url:'https://provider.test/audio',method:'GET',headers:[['User-Agent','app-UA'],['Cookie','secret'],['Referer','https://other.test/'],['Range','bytes=0-2']],wanted:[],timeout:1000});
    assert.equal(await finished,0);assert.equal(requests.length,1);assert.equal(requests[0].url,'https://provider.test/audio');
    assert.equal(requests[0].options.credentials,'omit');assert.equal(requests[0].options.redirect,'follow');assert.deepEqual(requests[0].options.headers,[['Range','bytes=0-2']]);
    assert.deepEqual(headers,[['content-type','audio/mpeg']]);assert.deepEqual(received,[[1,2,3]]);
  }finally{net.close();globalThis.fetch=previous;}
});
test('a browser CORS failure is reported once with no proxy retry or URL query disclosure',async()=>{
  const previous=globalThis.fetch,notices=[];let resolve,calls=0;
  const finished=new Promise(r=>resolve=r),host={browserNotify:m=>notices.push(m),_browser_net_done:(id,status)=>resolve(status)};
  globalThis.fetch=async()=>{calls++;throw new TypeError('CORS blocked');};
  const net=installBrowserNetwork(host,['web'],{url:'https://store.test/relay'});
  try{
    host.browserNetStart({id:1,url:'https://provider.test/path?token=secret',method:'GET',headers:[],wanted:[],timeout:1000});
    assert.equal(await finished,-14);assert.equal(calls,1);assert.equal(notices.length,1);assert.ok(!notices[0].message.includes('secret'));assert.match(notices[0].message,/direct network failed/);
  }finally{net.close();globalThis.fetch=previous;}
});
test('Fetch header rejection preserves the real exchange limit reason, IO failures and cancellation',
  {skip:!process.env.WASI_SDK_DIR},async()=>{
  // Compile the real C start/open paths and shared stream for wasm32. Expose
  // EM_JS bodies as strings so the imports execute those same bindings in Node.
  // Only allocation is a test arena; mode, validation and diagnostics are real.
  const system=fileURLToPath(new URL('../../',import.meta.url)),temporary=mkdtempSync(join(tmpdir(),'pxa-browser-network-'));
  try{
    writeFileSync(join(temporary,'emscripten.h'),`#define EM_JS(ret,name,args,...) ret name args; \\
__attribute__((export_name("browser_test_js_" #name))) const char *browser_test_js_##name(void) { return #__VA_ARGS__; } \\
__attribute__((export_name("browser_test_args_" #name))) const char *browser_test_args_##name(void) { return #args; }
#define EMSCRIPTEN_KEEPALIVE
`);
    writeFileSync(join(temporary,'fixture.c'),`#include ${JSON.stringify(join(system,'simulator/browser/network.c'))}
static uint8_t test_arena[512*1024];
static size_t test_used;
void *browser_test_alloc(size_t size) {
    size_t start=(test_used+7u)&~(size_t)7u;
    if(start>sizeof(test_arena)||size>sizeof(test_arena)-start)return NULL;
    test_used=start+size;
    return test_arena+start;
}
void *calloc(size_t count,size_t size) {
    if(size&&count>SIZE_MAX/size)return NULL;
    void *p=browser_test_alloc(count*size);
    if(p)memset(p,0,count*size);
    return p;
}
void free(void *p) { (void)p; }
void browser_test_reset(void) {
    memset(exchanges,0,sizeof(exchanges));
    sequence=0;
    test_used=0;
}
int32_t browser_test_open(int streaming,uint32_t head_bytes) {
    static const uint8_t url[]="https://example.test/",name[]="x-large";
    pxa_bytes_t wanted={name,sizeof(name)-1};
    pxa_net_request_t r={0};
    r.url=(pxa_bytes_t){url,sizeof(url)-1};
    r.method=PXA_NET_METHOD_GET;r.buffer_bytes=512;r.response_header_bytes=head_bytes;
    r.max_body_bytes=64;r.max_response_bytes=64;r.timeout_ms=1000;
    r.wanted_response_headers=&wanted;r.wanted_response_header_count=1;
    if(streaming){void *exchange;return open_exchange(NULL,&r,&exchange);}
    uint64_t operation;return start(NULL,&r,&operation);
}
int32_t browser_test_io(uint32_t operation) {
    return pxa_http_stream_io(&exchanges[0].stream,operation,NULL,0);
}
int32_t browser_test_status(void) { return exchanges[0].stream.error; }
void browser_test_close(void) { close_exchange(NULL,&exchanges[0]); }
`);
    const output=join(temporary,'fixture.wasm'),compiler=join(resolve(process.env.WASI_SDK_DIR),'bin/clang');
    const built=spawnSync(compiler,['-std=c11','-O2','-nostartfiles','-I'+temporary,'-I'+join(system,'libpxa/include'),
      join(temporary,'fixture.c'),join(system,'libpxa/src/services/net/http_stream.c'),
      join(system,'libpxa/src/services/net/net_validate.c'),join(system,'libpxa/src/core/wire.c'),
      '-Wl,--no-entry','-Wl,--allow-undefined','-Wl,--export-memory',
      ...['browser_test_alloc','browser_test_reset','browser_test_open','browser_test_close','browser_test_io','browser_test_status',
        'browser_net_header','browser_net_headers','browser_net_receive','browser_net_done'].map(name=>'-Wl,--export='+name),
      '-o',output],{encoding:'utf8'});
    assert.equal(built.status,0,built.error?.message||built.stderr);
    let c,currentHost;
    const text=(p,n)=>{
      const heap=new Uint8Array(c.memory.buffer);
      return new TextDecoder().decode(heap.subarray(p,n===undefined?heap.indexOf(0,p):p+n));
    };
    const bindings=new Map(),module=new WebAssembly.Module(readFileSync(output));
    const env=Object.fromEntries(WebAssembly.Module.imports(module).map(({module:namespace,name})=>{
      assert.equal(namespace,'env');
      return [name,(...args)=>{
        if(!bindings.has(name)){
          const parameters=text(c['browser_test_args_'+name]()).slice(1,-1).split(',').filter(s=>s.trim()).map(s=>s.trim().match(/\w+$/)[0]);
          bindings.set(name,new Function('Module','HEAPU32','HEAPU8','UTF8ToString',...parameters,text(c['browser_test_js_'+name]())));
        }
        return bindings.get(name)(currentHost,new Uint32Array(c.memory.buffer),new Uint8Array(c.memory.buffer),text,...args);
      }];
    }));
    c=(await WebAssembly.instantiate(module,{env})).exports;
    const run=async({streaming=true,headBytes=64,invalidValue=false,fetchError=false,cancel=false,body='body',headers={'content-type':'text/plain'}}={})=>{
      c.browser_test_reset();
      let finish,releaseFetch,signal,headerCalls=0,mode;
      const finished=new Promise(r=>finish=r),previous=globalThis.fetch;
      const host={get HEAPU8(){return new Uint8Array(c.memory.buffer);},_malloc:c.browser_test_alloc,_free(){},
        _browser_net_header:(id,p,n,q,s)=>{
          headerCalls++;
          if(invalidValue)host.HEAPU8[q]=10;
          return c.browser_net_header(id,p,n,q,s);
        },
        _browser_net_headers:c.browser_net_headers,_browser_net_receive:c.browser_net_receive,
        _browser_net_done:(id,status)=>{c.browser_net_done(id,status);finish();}};
      globalThis.fetch=async(url,options)=>{
        signal=options.signal;
        if(fetchError)throw new TypeError('CORS blocked');
        if(cancel)return new Promise(r=>releaseFetch=r);
        return new Response(new TextEncoder().encode(body),{headers});
      };
      const net=installBrowserNetwork(host,['web']);
      const startFetch=host.browserNetStart;
      host.browserNetStart=options=>{mode=options.streaming;startFetch(options);};
      currentHost=host;
      try{
        assert.equal(c.browser_test_open(Number(streaming),headBytes),0);
        assert.equal(mode,streaming); // This flag must come from the real C path.
        if(cancel){
          c.browser_net_done(1,-10); // Native owner cancels before Fetch resolves.
          net.close();
          releaseFetch(new Response('body',{headers:{'content-type':'text/plain'}}));
          await new Promise(setImmediate);
        }else await finished;
        return {status:c.browser_test_status(),reason:c.browser_test_io(7),aborted:signal.aborted,headerCalls};
      }finally{net.close();c.browser_test_close();globalThis.fetch=previous;}
    };
    const limited=await run({headBytes:16});
    assert.deepEqual(limited,{status:-16,reason:1,aborted:true,headerCalls:1});
    // Both invalid and valid data exceed the same 16-byte head budget. Only
    // the valid header is a capacity limit, even after the JS abort path.
    assert.deepEqual(await run({headBytes:16,invalidValue:true}),{status:-14,reason:0,aborted:true,headerCalls:1});
    assert.deepEqual(await run({fetchError:true}),{status:-14,reason:0,aborted:true,headerCalls:0});
    assert.deepEqual(await run({cancel:true}),{status:-10,reason:0,aborted:true,headerCalls:0});
    assert.deepEqual(await run({body:'x'.repeat(65)}),{status:-16,reason:2,aborted:true,headerCalls:1});
    assert.deepEqual(await run(),{status:0,reason:0,aborted:false,headerCalls:1});
    // HTTP_OPEN uses the total head budget; buffered requests keep 256/96.
    for(const headers of [{'x-large':'x'.repeat(300)},{'content-type':'x'.repeat(97)}]){
      assert.deepEqual(await run({headers,headBytes:4096}),{status:0,reason:0,aborted:false,headerCalls:1});
      assert.deepEqual(await run({headers,headBytes:64}),{status:-16,reason:1,aborted:true,headerCalls:1});
      assert.deepEqual(await run({headers,headBytes:4096,streaming:false}),{status:-14,reason:0,aborted:true,headerCalls:0});
    }
    // A valid value too large for the wire field still exceeds every head
    // budget, rather than becoming a malformed-header or transport failure.
    assert.deepEqual(await run({headers:{'x-large':'x'.repeat(65536)},headBytes:4096}),{status:-16,reason:1,aborted:true,headerCalls:1});
  }finally{rmSync(temporary,{recursive:true,force:true});}
});

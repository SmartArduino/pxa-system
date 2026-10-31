// SPDX-License-Identifier: MIT
// No JavaScript/DOM/network imports are exposed to guest applications.
import {installBrowserFonts} from './fonts.mjs';
import {prepareStorage} from './storage.mjs';
import {installBrowserNetwork} from './network.mjs';
import {createBorrowBridge} from './borrow.mjs';
import {prepareGuestModule} from './wasm-policy.mjs';
import {installPackageFiles,importMediaFiles} from './package.mjs';
import {createSurfaceBridge} from './surface.mjs';
import {installBrowserSockets} from './sockets.mjs';
import {installBrowserCapture} from './capture.mjs';
export const MAX_IO = 4 * 1024 * 1024;
export const EVENT_BYTES = 4096;
export function memoryRange(memory, pointer, size) {
  pointer >>>= 0; size >>>= 0;
  if (pointer > memory.buffer.byteLength || size > memory.buffer.byteLength - pointer)
    throw new RangeError('Guest memory range is out of bounds');
  return new Uint8Array(memory.buffer, pointer, size);
}

export async function createRuntime(host, binary, {width=320, height=240, log=()=>{}, notify=()=>{}, locale='en-US',appId='local',networkOrigins=[],networkProxy=null,permissions=[],audioAvailable=false,mainLoop=false,assets=[]}={}) {
  const check = (value, operation) => { if (value < 0) throw new Error(`${operation}: ${value}`); return value; };
  host.browserNotify=notify;
  host.browserMusicAvailable=!!audioAvailable;
  host.browserLocale=locale;
  if(mainLoop&&(typeof WebAssembly.Suspending!=='function'||typeof WebAssembly.promising!=='function'))
    throw new Error('此游戏需要支持 JSPI 的浏览器，请使用新版 Chrome、Edge 或 Firefox');
  host.browserMainLoop=mainLoop;
  host.browserAssets=new Map(assets.map(asset=>[asset.path,new Uint8Array(asset.binary)]));
  const network=installBrowserNetwork(host,networkOrigins,networkProxy);
  installBrowserFonts(host);
  const sockets=installBrowserSockets(host,networkProxy,permissions);
  const capture=await installBrowserCapture(host,notify);
  host.browserCaptureAvailable=host.browserCaptureAvailable&&!!audioAvailable;
  host.browserCaptureDeclared=permissions.some(p=>p.name==='audio.capture'&&p.scope==='microphone');
  const storage=await prepareStorage(host,appId,notify);
  host.browserStorageChanged=storage.changed;
  try {
  installPackageFiles(host,assets);
  check(host._browser_init(width, height), 'Host initialization');
  let guest, scratch = 0, capacity = 0, callbacks = 0, copiedBytes = 0;
  let importJob=null;
  let pending=null,callbackError=null,waiter=null,suspended=false,stopping=false,lastYield=performance.now();
  const stopSignal=new Error('Cooperative browser stop');let cancelWait=false;
  let guestWorkMs=0,guestSince=0;
  const accountGuest=()=>{if(guestSince){guestWorkMs+=performance.now()-guestSince;guestSince=0;}};
  const wake=()=>waiter?.();
  const borrow=createBorrowBridge(host,()=>guest.memory,n=>{copiedBytes+=n;});
  const reserve = size => {
    if (size > MAX_IO) throw new RangeError('PXA import exceeds the browser I/O limit');
    if (size > capacity) {
      const next = host._malloc(size);
      if (!next) throw new Error('Host allocation failed');
      if (scratch) host._free(scratch);
      scratch = next; capacity = size;
    }
    return scratch;
  };
  const bridge = (operation, size, pointer, invoke, writeBack=false) => {
    size >>>= 0; pointer >>>= 0;
    if (operation === 'control' && size > EVENT_BYTES - 20) return -1;
    const input = memoryRange(guest.memory, pointer, size);
    reserve(size);
    host.HEAPU8.set(input, scratch); copiedBytes += size;
    const result = invoke(scratch, size);
    // I/O has in/out packet operations; copy the validated buffer back even
    // when its return value is a status rather than a byte count.
    if (writeBack) { memoryRange(guest.memory, pointer, size).set(host.HEAPU8.subarray(scratch, scratch + size)); copiedBytes += size; }
    return result;
  };
  const surface=createSurfaceBridge(host,()=>guest.memory,
    (handle,op,p,n)=>bridge('io',n,p,(ptr,len)=>host._browser_io(handle,op,ptr,len),true),
    n=>{copiedBytes+=n;});
  const view = () => new DataView(guest.memory.buffer);
  const write32 = (p,v) => { memoryRange(guest.memory,p,4); view().setUint32(p>>>0,v,true); };
  const waitEvent=(handle,p,n)=>{
    if(cancelWait)throw stopSignal;
    p>>>=0;n>>>=0;
    if(n<20||n>EVENT_BYTES)return -1;
    memoryRange(guest.memory,p,n);
    const timeout=view().getBigUint64(p,true);
    if(timeout>60000000n)return -16;
    const poll=()=>{
      // Verified MEMFS reads need not wait for the 60 Hz display clock. This
      // progresses bounded service jobs while Guest execution is suspended.
      host._browser_pump();
      reserve(n);host.HEAPU8.set(memoryRange(guest.memory,p,n),scratch);
      const result=host._browser_event_wait(handle,scratch,n);
      if(result>0){memoryRange(guest.memory,p,result).set(host.HEAPU8.subarray(scratch,scratch+result));copiedBytes+=result;}
      return result;
    };
    const result=suspended?-7:poll(),now=performance.now();
    // Also yield busy zero-timeout loops periodically so UI/stop messages can run.
    if(!suspended&&now-lastYield<8&&(result!==-7||timeout===0n))return result;
    accountGuest();
    return new Promise((resolve,reject)=>{
      const deadline=now+Number(timeout)/1000;let timer,readyResult=result===-7?null:result;
      const check=()=>{
        clearTimeout(timer);
        if(cancelWait){waiter=null;guestSince=performance.now();reject(stopSignal);return;}
        if(suspended&&!stopping){timer=setTimeout(check,100);return;}
        const next=readyResult??poll();readyResult=null;
        if(next!==-7||performance.now()>=deadline){
          waiter=null;lastYield=performance.now();guestSince=lastYield;resolve(next===-7&&timeout>0n?-12:next);
        }else timer=setTimeout(check,Math.max(1,deadline-performance.now()));
      };
      waiter=check;timer=setTimeout(check,readyResult!==null||timeout===0n?0:Math.min(16,Number(timeout)/1000));
    });
  };
  const wasi = {
    args_sizes_get: (a,b) => {write32(a,0);write32(b,0);return 0;}, args_get:()=>0,
    environ_sizes_get: (a,b) => {write32(a,0);write32(b,0);return 0;}, environ_get:()=>0,
    clock_time_get: (id,precision,p) => {
      if (id !== 0 && id !== 1) return 28;
      memoryRange(guest.memory,p,8);
      view().setBigUint64(p>>>0,BigInt(Math.floor((id===0?Date.now():performance.now())*1e6)),true);return 0;
    },
    random_get: (p,n) => {
      const bytes = memoryRange(guest.memory,p,n);
      for(let i=0;i<bytes.length;i+=65536) crypto.getRandomValues(bytes.subarray(i,i+65536));
      return 0;
    },
    fd_write: (fd,p,count,result) => {
      if(fd!==1 && fd!==2)return 8;
      if(count>1024)return 28;
      memoryRange(guest.memory,p,count*8);
      let written=0, output='';
      for(let i=0;i<count;i++){
        const address=view().getUint32(p+i*8,true), n=view().getUint32(p+i*8+4,true);
        const bytes=memoryRange(guest.memory,address,n); written+=n;
        if(written>MAX_IO)return 28;
        if(output.length<2048)output+=new TextDecoder().decode(bytes.subarray(0,2048-output.length));
      }
      log(output);write32(result,written);return 0;
    },
    fd_fdstat_get: (fd,p) => {if(fd!==1&&fd!==2)return 8;memoryRange(guest.memory,p,24).fill(0);memoryRange(guest.memory,p,1)[0]=2;return 0;},
    fd_close:()=>8, fd_seek:()=>8, fd_read:()=>8, fd_prestat_get:()=>8,
    fd_prestat_dir_name:()=>8, path_open:()=>76,
    proc_exit: code => {throw new Error(`Guest exited (${code})`);}, sched_yield:()=>0,
  };
  const imports = {'pxa.core.v2': {
    pxa_call:(route,token,p,n)=>bridge('control',n,p,(ptr,len)=>host._browser_call(route,token,ptr,len)),
    pxa_io:(handle,op,p,n)=>{
      if(mainLoop&&op===0x201)return waitEvent(handle,p,n);
      const kind=host._browser_io_memory_kind(handle,op);
      if(kind===3)return surface.call(handle,op,p>>>0,n>>>0);
      return kind?borrow(handle,op,p>>>0,n>>>0,kind):bridge('io',n,p,(ptr,len)=>host._browser_io(handle,op,ptr,len),true);
    },
  }, wasi_snapshot_preview1:wasi};
  const module = await WebAssembly.compile(prepareGuestModule(binary));
  for (const item of WebAssembly.Module.imports(module)) {
    if(item.kind!=='function' || typeof imports[item.module]?.[item.name]!=='function')
      throw new Error(`Unsupported browser import: ${item.module}.${item.name}`);
  }
  if(mainLoop)imports['pxa.core.v2'].pxa_io=new WebAssembly.Suspending(imports['pxa.core.v2'].pxa_io);
  guest=(await WebAssembly.instantiate(module,imports)).exports;
  for (const name of ['memory','pxa_app_start','pxa_app_on_event','pxa_app_stop'])
    if(!guest[name])throw new Error(`Missing guest export: ${name}`);
  guest._initialize?.();
  const stack=guest.__pxa_browser_stack;
  const onEvent=mainLoop?WebAssembly.promising(guest.pxa_app_on_event):guest.pxa_app_on_event;
  const allocated=stack?0:guest.malloc?.(EVENT_BYTES);
  if(!stack&&!allocated)throw new Error('Guest must export malloc/free or preserve the __stack_pointer name');
  // The event is borrowed for this callback only. Reserving it above the
  // callee stack never overlaps an application's private/static allocator.
  const withEvent=(bytes,fn)=>{
    const previous=stack?.value, event=stack?((previous-EVENT_BYTES)&~15):allocated;
    if(stack&&(!Number.isInteger(previous)||event<0||event>=previous))throw new Error('Invalid Guest stack');
    memoryRange(guest.memory,event,EVENT_BYTES);
    if(stack)stack.value=event;
    const restore=()=>{if(stack)stack.value=previous;};
    try{memoryRange(guest.memory,event,bytes.length).set(bytes);const result=fn(event);
      if(result?.then)return result.finally(restore);
      restore();return result;
    }catch(error){restore();throw error;}
  };
  reserve(EVENT_BYTES);
  const callback=(isEvent,fn)=>{
    check(host._browser_begin(isEvent), 'Begin callback');
    const complete=result=>{accountGuest();callbacks++;check(host._browser_finish(isEvent,result),'Complete callback');check(result,'Guest callback');};
    guestSince=performance.now();
    const result=fn();
    if(result?.then)return result.then(complete,error=>{
      accountGuest();
      if(error===stopSignal){check(host._browser_finish(isEvent,0),'Cancel callback');return;}
      throw error;
    });
    complete(result);
  };
  const drain=()=>{
    if(callbackError)throw callbackError;
    wake();
    if(pending)return;
    // Bound dispatch per turn. A guest producing more work yields to the next
    // frame; a synchronous infinite loop is killed by the page's watchdog.
    for(let i=0;i<128;i++){
      reserve(EVENT_BYTES);
      const size=check(host._browser_event(scratch,EVENT_BYTES),'Read event');
      if(!size)break;
      copiedBytes+=size;
      const result=withEvent(host.HEAPU8.subarray(scratch,scratch+size),event=>callback(1,()=>onEvent(event,size)));
      if(result?.then){pending=result.catch(error=>{callbackError=error;}).finally(()=>{pending=null;});break;}
    }
  };
  host._browser_tick(performance.now());
  const configSize=check(host._browser_configuration(scratch,EVENT_BYTES),'Start configuration');
  withEvent(host.HEAPU8.subarray(scratch,scratch+configSize),event=>callback(0,()=>guest.pxa_app_start(event,configSize)));drain();
  return {
    tick(ms){host._browser_tick(ms);drain();surface.collect();return host._browser_render();},
    pointer(x,y,down){host._browser_pointer(x,y,down?1:0);drain();},
    controller(buttons){check(host._browser_controller(buttons),'Controller');drain();},
    back(){check(host._browser_back(),'Back');drain();},
    volumeKey(key){check(host._browser_volume_key(key),'Volume key');drain();},
    focus(value){host._browser_focus(value?1:0);drain();},
    pause(value){suspended=!!value;wake();},
    edit({session,text,cursor=0,action=0}){
      const encoded=new TextEncoder().encode(String(text));
      if(encoded.length>EVENT_BYTES-44 || encoded.includes(0))return -1;
      reserve(encoded.length+1);host.HEAPU8.set(encoded,scratch);host.HEAPU8[scratch+encoded.length]=0;
      const result=host._browser_edit(session,scratch,encoded.length,cursor,action);drain();return result;
    },
    async importFiles(data){
      if(stopping)return;
      if(importJob){if(data.id)host._browser_import_finish(data.id,-6,0,0,0,0);notify({type:'toast',text:'文件正在导入，请稍候'});drain();return;}
      importJob=importMediaFiles(host,data,storage,notify,()=>!stopping);
      try{const result=await importJob;if(!stopping)drain();return result;}finally{importJob=null;}
    },
    cancelImport(id){host._browser_import_finish(id,-10,0,0,0,0);drain();},
    musicEvent(id,state,status){host._browser_music_event(id,state,status);},
    capturePcm(id,samples){capture.pcm(id,samples);},
    captureError(id,status){capture.error(id,status);},
    audio(){
      const p=host._browser_audio_render(960),source=host.HEAP16.subarray(p>>>1,(p>>>1)+960);
      const samples=new Float32Array(960);for(let i=0;i<samples.length;i++)samples[i]=source[i]/32768;
      return samples;
    },
    flush:storage.flush,
    async stop(){
      stopping=true;suspended=false;
      await importJob?.catch(()=>{});
      if(pending){
        host._browser_focus(0);wake();
        // Let the foreground loop consume BACKGROUND and its save completion.
        // Then unwind at an event wait; never invoke stop over an active stack.
        await Promise.race([pending,new Promise(resolve=>setTimeout(resolve,100))]);
        cancelWait=true;wake();await pending;
      }
      if(callbackError)throw callbackError;
      check(host._browser_begin_stop(),'Begin stop');guest.pxa_app_stop(0);check(host._browser_finish_stop(),'Complete stop');
      surface.close();
      capture.close();
      sockets.close();network.close();storage.changed();await storage.close();
    },
    pixels(){return host.HEAPU8.subarray(host._browser_pixels(),host._browser_pixels()+width*height*4);},
    stats(){return {callbacks,copiedBytes,guestWorkMs,guestBytes:guest.memory.buffer.byteLength,hostBytes:host.HEAPU8.length};},
  };
  }catch(error){
    capture.close();
    sockets.close();network.close();await storage.close().catch(()=>{});throw error;
  }
}

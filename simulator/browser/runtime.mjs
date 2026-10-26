// SPDX-License-Identifier: MIT
// No JavaScript/DOM/network imports are exposed to guest applications.
export const MAX_IO = 4 * 1024 * 1024;
export const EVENT_BYTES = 4096;
export function memoryRange(memory, pointer, size) {
  pointer >>>= 0; size >>>= 0;
  if (pointer > memory.buffer.byteLength || size > memory.buffer.byteLength - pointer)
    throw new RangeError('Guest memory range is out of bounds');
  return new Uint8Array(memory.buffer, pointer, size);
}

export async function createRuntime(host, binary, {width=320, height=240, log=()=>{}}={}) {
  const check = (value, operation) => { if (value < 0) throw new Error(`${operation}: ${value}`); return value; };
  check(host._browser_init(width, height), 'Host initialization');
  let guest, scratch = 0, capacity = 0, callbacks = 0, copiedBytes = 0;
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
  const view = () => new DataView(guest.memory.buffer);
  const write32 = (p,v) => { memoryRange(guest.memory,p,4); view().setUint32(p>>>0,v,true); };
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
    pxa_io:(handle,op,p,n)=>bridge('io',n,p,(ptr,len)=>host._browser_io(handle,op,ptr,len),true),
  }, wasi_snapshot_preview1:wasi};
  const module = await WebAssembly.compile(binary);
  for (const item of WebAssembly.Module.imports(module)) {
    if(item.kind!=='function' || typeof imports[item.module]?.[item.name]!=='function')
      throw new Error(`Unsupported browser import: ${item.module}.${item.name}`);
  }
  guest=(await WebAssembly.instantiate(module,imports)).exports;
  for (const name of ['memory','malloc','free','pxa_app_start','pxa_app_on_event','pxa_app_stop'])
    if(!guest[name])throw new Error(`Missing guest export: ${name}`);
  guest._initialize?.();
  const event=guest.malloc(EVENT_BYTES);
  if(!event)throw new Error('Guest event allocation failed');
  memoryRange(guest.memory,event,EVENT_BYTES);
  reserve(EVENT_BYTES);
  const callback=(isEvent,fn)=>{
    check(host._browser_begin(isEvent), 'Begin callback');
    const result=fn(); callbacks++;
    check(host._browser_finish(isEvent,result),'Complete callback');
    check(result,'Guest callback');
  };
  const drain=()=>{
    // Bound dispatch per turn. A guest producing more work yields to the next
    // frame; a synchronous infinite loop is killed by the page's watchdog.
    for(let i=0;i<128;i++){
      reserve(EVENT_BYTES);
      const size=check(host._browser_event(scratch,EVENT_BYTES),'Read event');
      if(!size)break;
      memoryRange(guest.memory,event,size).set(host.HEAPU8.subarray(scratch,scratch+size));
      copiedBytes+=size;
      callback(1,()=>guest.pxa_app_on_event(event,size));
    }
  };
  host._browser_tick(performance.now());
  const configSize=check(host._browser_configuration(scratch,EVENT_BYTES),'Start configuration');
  memoryRange(guest.memory,event,configSize).set(host.HEAPU8.subarray(scratch,scratch+configSize));
  callback(0,()=>guest.pxa_app_start(event,configSize));drain();
  return {
    tick(ms){host._browser_tick(ms);drain();return host._browser_render();},
    pointer(x,y,down){host._browser_pointer(x,y,down?1:0);drain();},
    controller(buttons){check(host._browser_controller(buttons),'Controller');drain();},
    pixels(){return host.HEAPU8.subarray(host._browser_pixels(),host._browser_pixels()+width*height*4);},
    stats(){return {callbacks,copiedBytes,guestBytes:guest.memory.buffer.byteLength,hostBytes:host.HEAPU8.length};},
  };
}

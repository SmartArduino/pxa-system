// SPDX-License-Identifier: MIT
// Persistent Surface protocol over two separate Wasm memories. Keep a pinned
// Host shadow; copy only the submitted bank, never the whole Guest heap.
export function createSurfaceBridge(host,getMemory,io,account=()=>{}){
  const mappings=new Map();
  const range=(p,n)=>{
    p>>>=0;n>>>=0;const memory=getMemory();
    if(p>memory.buffer.byteLength||n>memory.buffer.byteLength-p)throw new RangeError('Surface buffer range');
    return new Uint8Array(memory.buffer,p,n);
  };
  const collect=()=>{
    const retired=host.browserSurfaceRetired?.splice(0)??[];
    for(const address of retired)for(const [handle,map] of mappings)if(map.shadow===address){host._free(address);mappings.delete(handle);break;}
  };
  return {
    collect,
    call(handle,operation,pointer,size){
      collect();
      if(operation===0x100){
        if((pointer&63)||!size||size>24*1024*1024)return -1;
        if(mappings.has(handle))return -2;
        let data;try{data=range(pointer,size);}catch{return -1;}
        const shadow=host._malloc(size);if(!shadow)return -9;
        host.HEAPU8.set(data,shadow);account(size);
        const result=host._browser_io(handle,operation,shadow,size);
        if(result<0){host._free(shadow);return result;}
        const bytes=host._browser_surface_frame_bytes(shadow);
        if(!bytes||size%bytes)throw new Error('Invalid Host Surface registration');
        mappings.set(handle,{shadow,pointer,size,bytes});return result;
      }
      if(operation===0x102){
        const map=mappings.get(handle);if(!map)return -2;
        let packet;try{packet=range(pointer,size);}catch{return -1;}
        if(size!==16||packet[0]>=map.size/map.bytes||packet.subarray(1,8).some(Boolean))return -1;
        const frame=new DataView(packet.buffer,packet.byteOffset,16).getBigUint64(8,true);
        if(!host._browser_surface_can_present(map.shadow,packet[0],frame))return -2;
        const offset=packet[0]*map.bytes;
        host.HEAPU8.set(range(map.pointer+offset,map.bytes),map.shadow+offset);account(map.bytes);
      }
      return io(handle,operation,pointer,size);
    },
    close(){for(const map of mappings.values())host._free(map.shadow);mappings.clear();},
  };
}

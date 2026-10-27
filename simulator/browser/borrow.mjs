// SPDX-License-Identifier: MIT
// Sparse mirror of borrowed memory. Keep original offsets and aliasing so the
// existing C validators remain authoritative; only referenced ranges cross.
// Maximum mirror reservation is 64 MiB, maximum transfer per call is 4 MiB.
export function borrowedRanges(memory,kind,operation,pointer,size){
  const view=new DataView(memory.buffer),ranges=[];
  function range(at,n,write=false){
    if(!Number.isSafeInteger(at)||!Number.isSafeInteger(n)||at<0||n<0||at>view.byteLength||n>view.byteLength-at)
      throw new RangeError('Borrowed range outside guest memory');
    if(n)ranges.push({at,end:at+n,write});
  }
  range(pointer,size);const end=pointer+size;
  function u32(at){if(at<pointer||at+4>end)throw new RangeError('Truncated borrowed packet');return view.getUint32(at,true);}
  function u16(at){if(at<pointer||at+2>end)throw new RangeError('Truncated borrowed packet');return view.getUint16(at,true);}
  function pair(at,write=false){range(u32(at),u32(at+4),write);}
  if(kind===1){
    if(operation===0x109||operation===0x10a){
      const psg=operation===0x10a,count=u16(pointer+(psg?16:12)),stride=psg?40:32;
      if(count>(psg?8:32))throw new RangeError('Audio voice limit');
      range(u32(pointer),u32(pointer+4)*8,true);
      const records=u32(pointer+8);range(records,count*stride,psg);
      for(let i=0;i<count;i++){
        const at=records+i*stride;
        if(psg){if(view.getUint8(at)===2)range(view.getUint32(at+32,true),view.getUint8(at+31));}
        else range(view.getUint32(at,true),view.getUint32(at+4,true));
      }
    }
    else throw new RangeError('Unknown audio borrow profile');
  }else if(kind===2){
    if(operation===0x108||operation===0x109){pair(pointer);pair(pointer+8);pair(pointer+16,true);pair(pointer+40);}
    else if(operation===0x10a){const count=u32(pointer+24);range(u32(pointer+16),count*2,true);range(u32(pointer+20),count,true);}
    else throw new RangeError('Unknown raster borrow profile');
  }else throw new RangeError('Unknown borrow kind');
  ranges.sort((a,b)=>a.at-b.at);
  const merged=[];
  for(const r of ranges){const last=merged.at(-1);if(last&&r.at<=last.end){last.end=Math.max(last.end,r.end);last.write||=r.write;}else merged.push({...r});}
  if(merged.reduce((n,r)=>n+r.end-r.at,0)>4*1024*1024)throw new RangeError('Borrow transfer limit');
  return merged;
}
export function createBorrowBridge(host,getMemory,account=()=>{}){
  let base=0,capacity=0;
  return (handle,operation,pointer,size,kind)=>{
    const memory=getMemory();let ranges;
    try{ranges=borrowedRanges(memory,kind,operation,pointer,size);}catch{return -1;}
    const bytes=memory.buffer.byteLength;
    if(bytes>64*1024*1024)return -16;
    if(bytes>capacity){if(base)host._free(base);base=host._malloc(bytes);capacity=base?bytes:0;if(!base)return -9;}
    for(const r of ranges){host.HEAPU8.set(new Uint8Array(memory.buffer,r.at,r.end-r.at),base+r.at);account(r.end-r.at);}
    const result=host._browser_io_memory(handle,operation,base+pointer,size,base,bytes);
    // Validation is atomic. Rejected commands must never write stale mirror
    // bytes into live Guest memory, including ranges aliased with the packet.
    if(result>=0)for(const r of ranges)if(r.write){
      new Uint8Array(memory.buffer,r.at,r.end-r.at).set(host.HEAPU8.subarray(base+r.at,base+r.end));account(r.end-r.at);
    }
    return result;
  };
}

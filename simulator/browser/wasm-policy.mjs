// SPDX-License-Identifier: MIT
// Bound Guest allocations before instantiation. Expose the LLVM-named stack
// global for callback-only event buffers; executable function bodies are intact.
const LIMIT_PAGES=1024, LIMIT_TABLE=65536;
const utf8=new TextDecoder('utf-8',{fatal:true}), encoder=new TextEncoder();
function reader(bytes){
  let p=0;
  return {get offset(){return p;},get remaining(){return bytes.length-p;},
    byte(){if(p>=bytes.length)throw new Error('Truncated Wasm');return bytes[p++];},
    u32(){let n=0;for(let i=0;i<5;i++){const b=this.byte();if(i===4&&(b&240))throw new Error('Invalid Wasm integer');n+=(b&127)*2**(i*7);if(!(b&128))return n;}throw new Error('Invalid Wasm integer');},
    take(n){if(n>bytes.length-p)throw new Error('Truncated Wasm section');const v=bytes.subarray(p,p+n);p+=n;return v;},
    name(){return utf8.decode(this.take(this.u32()));}};
}
function leb(n){const out=[];do{let b=n&127;n=Math.floor(n/128);out.push(b|(n?128:0));}while(n);return out;}
function join(parts){const result=new Uint8Array(parts.reduce((n,p)=>n+p.length,0));let offset=0;for(const part of parts){result.set(part,offset);offset+=part.length;}return result;}
function section(id,data){return join([[id],leb(data.length),data]);}
function limits(r,ceiling){
  const flags=r.u32();if(flags>1)throw new Error('Shared/64-bit Wasm memory is unavailable');
  const minimum=r.u32(),maximum=flags?r.u32():ceiling;
  if(minimum>ceiling||maximum<minimum)throw new Error('Guest allocation exceeds browser budget');
  return [...leb(1),...leb(minimum),...leb(Math.min(maximum,ceiling))];
}
export function prepareGuestModule(binary){
  const bytes=new Uint8Array(binary);
  if(bytes.length>8*1024*1024||bytes.length<8||!bytes.subarray(0,8).every((v,i)=>v===[0,97,115,109,1,0,0,0][i]))
    throw new Error('Invalid Wasm application');
  const r=reader(bytes.subarray(8)),sections=[];let stackIndex,exportSection,memoryCount=0;
  while(r.remaining){
    const id=r.byte(),payload=r.take(r.u32());let data=payload;const s=reader(payload);
    if(id===0&&s.name()==='name'){
      while(s.remaining){const kind=s.byte(),sub=reader(s.take(s.u32()));
        if(kind===7){const count=sub.u32();if(count>65536)throw new Error('Too many global names');
          for(let i=0;i<count;i++){const index=sub.u32(),name=sub.name();if(name==='__stack_pointer')stackIndex=index;}}
      }
    }else if(id===5){
      if(++memoryCount!==1||s.u32()!==1)throw new Error('Exactly one Guest memory is required');
      data=Uint8Array.from([1,...limits(s,LIMIT_PAGES)]);
      if(s.remaining)throw new Error('Invalid memory declaration');
    }else if(id===4){
      const count=s.u32();if(count>4)throw new Error('Too many Guest tables');
      const parts=[leb(count)];
      for(let i=0;i<count;i++){const type=s.byte();if(type!==0x70)throw new Error('Unsupported Guest table');parts.push([type,...limits(s,LIMIT_TABLE)]);}
      if(s.remaining)throw new Error('Invalid table declaration');data=join(parts);
    }
    const item={id,data};sections.push(item);if(id===7)exportSection=item;
  }
  if(memoryCount!==1||!exportSection)throw new Error('Missing Guest memory or exports');
  if(stackIndex!==undefined){
    const s=reader(exportSection.data),count=s.u32(),entries=s.take(s.remaining);
    const scan=reader(entries);
    for(let i=0;i<count;i++){if(scan.name()==='__pxa_browser_stack')throw new Error('Reserved browser export');scan.byte();scan.u32();}
    if(scan.remaining)throw new Error('Invalid export section');
    const name=encoder.encode('__pxa_browser_stack');
    exportSection.data=join([leb(count+1),entries,leb(name.length),name,[3],leb(stackIndex)]);
  }
  return join([bytes.subarray(0,8),...sections.map(s=>section(s.id,s.data))]);
}

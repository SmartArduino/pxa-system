import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import {pathToFileURL} from 'node:url';
import {createRuntime} from './runtime.mjs';
import {createBorrowBridge,borrowedRanges} from './borrow.mjs';
const bundle=process.env.PXA_BROWSER_BUNDLE;
assert.ok(bundle);
const {default:createHost}=await import(pathToFileURL(`${bundle}/pxa-browser.mjs`));
const num=(value,size=4)=>{const b=Buffer.alloc(size);if(size===8)b.writeBigUInt64LE(BigInt(value));else b.writeUIntLE(value,0,size);return b;};
const tlv=(tag,value)=>Buffer.concat([num(tag,2),num(value.length,2),value]);
async function platform(options={}){
  const host=await createHost(),rt=await createRuntime(host,await fs.readFile(`${bundle}/counter.wasm`),options);
  const scratch=host._malloc(65536);let token=100n;
  const active=fn=>{assert.equal(host._browser_begin(1),0);try{return fn();}finally{assert.equal(host._browser_finish(1,0),0);}};
  const call=(service,opcode,payload,request=true)=>{
    host.HEAPU8.set(payload,scratch);
    const id=request?++token:0n;
    assert.equal(active(()=>host._browser_call(service|(opcode<<16),id,scratch,payload.length)),0,`service ${service} opcode ${opcode}`);
    if(!request)return;
    for(let i=0;i<128;i++){
      const n=host._browser_event(scratch,4096);assert.ok(n>0,'completion');
      const event=Buffer.from(host.HEAPU8.slice(scratch,scratch+n));
      if(event.readBigUInt64LE(4)===id){const out=event.subarray(20);assert.equal(out.readInt32LE(),0,`completion ${service}/${opcode}`);return out;}
    }
    throw new Error('Missing completion');
  };
  const io=(handle,op,data)=>{
    host.HEAPU8.set(data,scratch);const result=active(()=>host._browser_io(handle,op,scratch,data.length));
    return {result,data:Buffer.from(host.HEAPU8.slice(scratch,scratch+data.length))};
  };
  const permission=(name,scope)=>call(11,2,Buffer.concat([tlv(1,Buffer.from(name)),tlv(2,Buffer.from(scope))])).readBigUInt64LE(4);
  return {host,rt,call,io,active,permission};
}
test('Surface RGB565 composition and frame accounting',async()=>{
  const {host,rt,call,io}=await platform();
  const handle=call(16,1,Buffer.from([2,0,2,0,1,0,2,0])).readBigUInt64LE(4);
  const layer=Buffer.alloc(24);layer.writeBigUInt64LE(handle);layer.writeUInt16LE(2,16);layer.writeUInt16LE(2,18);layer[22]=1;
  call(16,2,layer);assert.equal(io(handle,2,Buffer.from([0,248,0,248,0,248,0,248])).result,8);
  const queue=Buffer.alloc(20);queue.writeBigUInt64LE(handle);queue.writeBigUInt64LE(1n,8);call(16,3,queue,false);
  host._browser_tick(performance.now()+40);host._browser_render();
  assert.deepEqual([...rt.pixels().slice(0,3)],[0,0,255]);
  const state=call(16,4,num(handle,8));assert.equal(state.readBigUInt64LE(4),1n);assert.equal(state.readBigUInt64LE(12),1n);
  await rt.stop();
});
test('Sparse borrow runs real raster and PCM kernels, preserves guard bytes, rejects aliasing',async()=>{
  const {host,rt,call,active,permission}=await platform();
  const handle=call(18,3,Buffer.concat([num(64),num(0)])).readBigUInt64LE(4);
  const memory=new WebAssembly.Memory({initial:16,maximum:32});let transferred=0;
  const borrow=createBorrowBridge(host,()=>memory,n=>transferred+=n);
  const u8=new Uint8Array(memory.buffer),v=new DataView(memory.buffer),p=128;
  u8.fill(0x55,6000,6020);
  for(const [offset,value] of [[0,1024],[4,4],[8,2048],[12,512],[16,4096],[20,8],[28,2],[36,4]])v.setUint32(p+offset,value,true);
  for(const offset of [24,26,32,34])v.setUint16(p+offset,2,true);
  u8.set([1,2,2,1],1024);v.setUint16(2050,0xf800,true);v.setUint16(2052,0x7e0,true);
  assert.equal(active(()=>borrow(handle,0x109,p,64,2)),64);
  assert.deepEqual([...u8.slice(4096,4104)],[0,248,224,7,224,7,0,248]);
  assert.deepEqual([...u8.slice(6000,6020)],Array(20).fill(0x55));
  v.setUint32(p+16,1024,true);const before=u8.slice(1024,1032);
  assert.ok(active(()=>borrow(handle,0x109,p,64,2))<0);assert.deepEqual(u8.slice(1024,1032),before);
  const audioPermission=permission('audio.playback','media');
  const audio=call(10,1,Buffer.concat([tlv(1,num(audioPermission,8)),tlv(2,num(1,2))])).readBigUInt64LE(8);
  u8.fill(0,p,p+64);u8.fill(0,4096,4112);u8.fill(0,3000,3032);
  v.setUint32(p,4096,true);v.setUint32(p+4,2,true);v.setUint32(p+8,3000,true);v.setUint16(p+12,1,true);
  v.setUint32(3000,1024,true);v.setUint32(3004,2,true);v.setUint32(3012,65536,true);v.setUint32(3020,2,true);
  v.setUint16(3024,256,true);v.setUint16(3026,256,true);u8[3028]=1;u8[3029]=1;u8.set([128,127],1024);
  assert.equal(active(()=>borrow(audio,0x109,p,16,1)),16);
  assert.deepEqual(Array.from(new Int32Array(memory.buffer,4096,4)),[-32768,-32768,32512,32512]);
  assert.ok(transferred<2000,`only ${transferred} bytes copied from a 1 MiB heap`);
  memory.grow(1);assert.equal(active(()=>borrow(audio,0x109,p,16,1)),16,'mirror handles memory.grow');
  assert.throws(()=>borrowedRanges(memory,2,0x109,memory.buffer.byteLength-8,64),RangeError);
  await rt.stop();
});
test('Fetch exchange enforces origins, streams beyond ring size and accepts streamed upload',async()=>{
  const body=Buffer.alloc(90000);for(let i=0;i<body.length;i++)body[i]=i%251;
  const server=http.createServer(async(req,res)=>{
    if(req.url==='/upload'){const chunks=[];for await(const c of req)chunks.push(c);res.end(Buffer.concat(chunks));}
    else if(req.url==='/redirect'){res.writeHead(302,{Location:'https://example.com/'});res.end();}
    else {res.setHeader('content-type','application/octet-stream');res.end(body);}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const origin=`http://127.0.0.1:${server.address().port}`;
  let rt;
  try{
    const p=await platform({networkOrigins:[origin]});rt=p.rt;
    const permission=p.permission('net.client',origin);
    const open=(path,method=1,upload=0)=>p.call(9,3,Buffer.concat([
      tlv(1,Buffer.from(origin+path)),tlv(2,num(method,2)),tlv(3,num(permission,8)),tlv(4,num(body.length,8)),
      tlv(8,num(5000)),tlv(14,num(512)),tlv(15,num(upload,8)),tlv(16,num(1024))])).readBigUInt64LE(4);
    const read=async handle=>{
      const result=[],deadline=Date.now()+5000;
      while(Date.now()<deadline){const r=p.io(handle,1,Buffer.alloc(1024));
        if(r.result===-7){await new Promise(r=>setTimeout(r,1));continue;}
        if(r.result<0)return {status:r.result};
        if(!r.result)return {status:0,body:Buffer.concat(result)};
        result.push(r.data.subarray(0,r.result));
      }
      throw new Error('Network read timed out');
    };
    assert.deepEqual((await read(open('/data'))).body,body);
    const upload=open('/upload',3,1000),data=body.subarray(0,1000);let sent=0;
    while(sent<data.length){const r=p.io(upload,2,data.subarray(sent,Math.min(data.length,sent+128)));
      if(r.result===-7){await new Promise(r=>setTimeout(r,1));continue;}assert.ok(r.result>0);sent+=r.result;}
    assert.equal(p.io(upload,6,Buffer.alloc(0)).result,0);
    assert.deepEqual((await read(upload)).body,data);
    assert.equal((await read(open('/redirect'))).status,-14,'redirect must not escape permission origin');
  }finally{if(rt)await rt.stop();await new Promise(resolve=>server.close(resolve));}
});

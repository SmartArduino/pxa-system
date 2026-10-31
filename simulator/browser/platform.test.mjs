import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import {pathToFileURL} from 'node:url';
import {createSurfaceBridge} from './surface.mjs';
import {importMediaFiles,installPackageFiles} from './package.mjs';
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
  const pixels=rt.pixels(),at=(x,y)=>[...pixels.slice((y*320+x)*4,(y*320+x)*4+3)];
  // A 2×2 RGB565 Surface fits at 4×, centered exactly like desktop/device.
  assert.deepEqual(at(156,116),[0,0,255]);assert.deepEqual(at(163,123),[0,0,255]);
  assert.notDeepEqual(at(155,116),[0,0,255]);assert.notDeepEqual(at(164,123),[0,0,255]);
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

test('Mapped Surface copies the submitted bank, survives memory growth and rejects stale presents',async()=>{
  const {host,rt,call,io,active}=await platform();
  const handle=call(16,1,Buffer.from([2,0,2,0,1,0,2,4])).readBigUInt64LE(4);
  const layer=Buffer.alloc(24);layer.writeBigUInt64LE(handle);layer.writeUInt16LE(2,16);layer.writeUInt16LE(2,18);layer[22]=1;call(16,2,layer);
  const memory=new WebAssembly.Memory({initial:2,maximum:4});let copied=0;
  const bridge=createSurfaceBridge(host,()=>memory,(h,op,p,n)=>{const scratch=host._malloc(Math.max(1,n));try{host.HEAPU8.set(new Uint8Array(memory.buffer,p,n),scratch);const result=host._browser_io(h,op,scratch,n);new Uint8Array(memory.buffer,p,n).set(host.HEAPU8.subarray(scratch,scratch+n));return result;}finally{host._free(scratch);}},n=>copied+=n);
  assert.equal(active(()=>bridge.call(handle,0x100,65,16)),-1);
  assert.equal(active(()=>bridge.call(handle,0x100,64,16)),16);
  assert.equal(active(()=>bridge.call(handle,0x101,128,4)),4);
  const bank=new Uint8Array(memory.buffer)[128];assert.equal(bank,0);
  memory.grow(1);
  new Uint8Array(memory.buffer,64,8).set([0,248,0,248,0,248,0,248]);
  const packet=new DataView(memory.buffer,128,16);new Uint8Array(memory.buffer,128,16).fill(0);packet.setBigUint64(8,1n,true);
  assert.equal(active(()=>bridge.call(handle,0x102,128,16)),16);
  host._browser_tick(performance.now()+40);host._browser_render();
  const at=(116*320+156)*4;assert.deepEqual([...rt.pixels().slice(at,at+3)],[0,0,255]);
  const total=copied;assert.ok(active(()=>bridge.call(handle,0x102,128,16))<0);assert.equal(copied,total);
  assert.equal(total,24,'registration + one 8-byte bank, no full heap mirror');
  await rt.stop();bridge.collect();bridge.close();
});
test('Verified resources stay read-only; imported media persists and cancels before publication',async()=>{
  const {host,rt}=await platform();let changed=0,flushed=0;
  const storage={changed(){changed++;},async flush(){flushed++;}};
  installPackageFiles(host,[{path:'media/demo.nes',binary:new Uint8Array([1,2])},{path:'assets/table.dat',binary:new Uint8Array([3])}]);
  assert.deepEqual([...host.FS.readFile('/media/demo.nes')],[1,2]);
  assert.equal(host.FS.analyzePath('/data/media/demo.nes').exists,false,'bundle is never persisted as user data');
  const file={name:'测试.nes',size:3,slice:()=>({async arrayBuffer(){return new Uint8Array([4,5,6]).buffer;}})};
  const result=await importMediaFiles(host,{files:[file]},storage,()=>{});
  assert.equal(result.imported,1);assert.deepEqual([...host.FS.readFile('/media/测试.nes')],[4,5,6]);
  assert.equal((await importMediaFiles(host,{files:[file]},storage,()=>{})).skipped,1);
  let valid=true;const cancelled={...file,name:'cancelled.nes',slice:()=>({async arrayBuffer(){valid=false;return new Uint8Array(3).buffer;}})};
  assert.equal((await importMediaFiles(host,{files:[cancelled]},storage,()=>{},()=>valid)).imported,0);
  assert.equal(host.FS.analyzePath('/data/media/cancelled.nes').exists,false);assert.equal(changed,1);assert.ok(flushed>=2);
  await rt.stop();
});

test('Verified asset views retain offsets and share the downloaded buffer through MEMFS',async()=>{
  const bundle=new Uint8Array([99,1,2,88,3,4,77]);
  const {host,rt}=await platform({assets:[
    {path:'assets/first.bin',binary:bundle.subarray(1,3)},
    {path:'media/second.bin',binary:bundle.subarray(4,6)}
  ]});
  try{
    for(const [path,offset,bytes] of [['/package/assets/first.bin',1,[1,2]],['/media/second.bin',4,[3,4]]]){
      assert.deepEqual([...host.FS.readFile(path)],bytes);
      const contents=host.FS.analyzePath(path).object.contents;
      assert.equal(contents.buffer,bundle.buffer);
      assert.equal(contents.byteOffset,bundle.byteOffset+offset);
      assert.equal(contents.byteLength,bytes.length);
    }
  }finally{await rt.stop();}
});

test('Socket relay preserves C ring backpressure and grants only declared outbound authority',async t=>{
  const sockets=[];
  class Socket{
    constructor(url){this.url=String(url);this.readyState=0;this.sent=[];sockets.push(this);}
    send(data){this.sent.push(data);}
    close(){this.readyState=3;}
  }
  t.mock.property(globalThis,'WebSocket',Socket);
  const {host,rt,call,io,permission}=await platform({permissions:[{name:'net.socket',scope:'outbound'}],networkProxy:{url:'https://store.test/fetch',socketUrl:'https://store.test/socket',csrf:'csrf-test'}});
  try{
    const auth=permission('net.socket','outbound');
    const handle=call(9,5,Buffer.concat([tlv(1,Buffer.from('tls://example.com:443')),tlv(3,num(auth,8)),tlv(8,num(5000)),tlv(14,num(512))])).readBigUInt64LE(4);
    const socket=sockets[0];assert.equal(socket.url,'wss://store.test/socket');
    socket.readyState=1;socket.onopen();assert.deepEqual(JSON.parse(socket.sent[0]),{csrf:'csrf-test',endpoint:'tls://example.com:443'});
    socket.onmessage({data:JSON.stringify({type:'connected'})});
    socket.onmessage({data:new Uint8Array(1024).fill(19).buffer});
    assert.equal(socket.sent.filter(m=>m==='ack').length,0,'no receiver credit while the PXA ring is full');
    let read=0;
    while(read<1024){const r=io(handle,1,Buffer.alloc(128));assert.equal(r.result,128);assert.ok(r.data.every(v=>v===19));read+=r.result;}
    assert.equal(socket.sent.filter(m=>m==='ack').length,1);
    assert.equal(io(handle,2,Buffer.from([1,2,3])).result,3);
    assert.deepEqual([...socket.sent.find(x=>x instanceof Uint8Array)],[1,2,3]);
    socket.onmessage({data:JSON.stringify({type:'sent'})});
    socket.onclose({code:1000});assert.equal(io(handle,1,Buffer.alloc(16)).result,0);
  }finally{await rt.stop();}
});

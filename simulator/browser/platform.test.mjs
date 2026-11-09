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
async function platform(options={},fixture='counter'){
  const host=await createHost(),rt=await createRuntime(host,await fs.readFile(`${bundle}/${fixture}.wasm`),options);
  const scratch=host._malloc(1024*1024);let token=100n;
  const active=fn=>{assert.equal(host._browser_begin(1),0);try{return fn();}finally{assert.equal(host._browser_finish(1,0),0);}};
  const call=(service,opcode,payload,request=true,success=true)=>{
    host.HEAPU8.set(payload,scratch);
    const id=request?++token:0n;
    assert.equal(active(()=>host._browser_call(service|(opcode<<16),id,scratch,payload.length)),0,`service ${service} opcode ${opcode}`);
    if(!request)return;
    for(let i=0;i<128;i++){
      const n=host._browser_event(scratch,4096);assert.ok(n>0,'completion');
      const event=Buffer.from(host.HEAPU8.slice(scratch,scratch+n));
      if(event.readBigUInt64LE(4)===id){const out=event.subarray(20);if(success)assert.equal(out.readInt32LE(),0,`completion ${service}/${opcode}`);return out;}
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
  call(2,1,tlv(9,num(7,1)),false);
  host._browser_tick(performance.now()+80);host._browser_render();
  const turned=rt.pixels(),point=(x,y)=>[...turned.slice((y*240+x)*4,(y*240+x)*4+3)];
  assert.deepEqual(point(116,156),[0,0,255]);assert.deepEqual(point(123,163),[0,0,255]);
  assert.notDeepEqual(point(115,156),[0,0,255]);
  const state=call(16,4,num(handle,8));assert.equal(state.readBigUInt64LE(4),1n);assert.equal(state.readBigUInt64LE(12),1n);
  await rt.stop();
});
test('Window preferences rotate tall/wide views, retain square shape, and expose native metrics',async()=>{
  for(const [width,height]of [[240,320],[320,240],[240,240]]){
    const {host,rt,call}=await platform({width,height});
    const snapshot=()=>call(2,5,Buffer.alloc(0)).subarray(4);
    let initial=snapshot();assert.equal(initial.length,120);
    assert.equal(initial.readUInt32LE(107),width);assert.equal(initial.readUInt32LE(111),height);
    assert.equal(initial[119],15);
    for(const preference of [1,2,3,4,6,7,8,9,10,0]){
      call(2,1,tlv(9,num(preference,1)),false);
      const value=snapshot(),r=value[102],w=value.readUInt32LE(16),h=value.readUInt32LE(20);
      assert.equal(w,(r&1)?height:width);assert.equal(h,(r&1)?width:height);
      assert.equal(value[92],width===height?3:w>h?2:1);
      if(width===height&&preference>=1&&preference<=4)assert.equal(r,0);
      if(preference===1&&width!==height)assert.ok(h>w);
      if(preference===2&&width!==height)assert.ok(w>h);
      host._browser_tick(performance.now()+40);host._browser_render();
      assert.equal(rt.pixels().length,width*height*4);
    }
    await rt.stop();
  }
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

test('direct browser mode reports raw sockets unsupported and ignores legacy tunnel config',async t=>{
  t.mock.property(globalThis,'WebSocket',class{constructor(){assert.fail('No server socket tunnel may open');}});
  const {rt,call,permission}=await platform({permissions:[{name:'net.socket',scope:'outbound'}],networkProxy:{url:'https://store.test/fetch',socketUrl:'https://store.test/socket',csrf:'legacy'}});
  try{
    const auth=permission('net.socket','outbound');
    const result=call(9,5,Buffer.concat([tlv(1,Buffer.from('tls://example.com:443')),tlv(3,num(auth,8)),tlv(8,num(5000)),tlv(14,num(512))]),true,false);
    assert.equal(result.readInt32LE(),-3,'operation support differs from signed permission');
  }finally{await rt.stop();}
});

test('Alpha Canvas stays above Surface; clearing and overlay-only updates do not leave ghosts',async()=>{
  const {host,rt,call,io}=await platform({},'alpha-overlay-test');
  const handle=call(16,1,Buffer.from([64,1,240,0,1,0,2,0])).readBigUInt64LE(4);
  const layer=Buffer.alloc(24);layer.writeBigUInt64LE(handle);layer.writeUInt16LE(320,16);layer.writeUInt16LE(240,18);layer.writeInt16LE(2,20);layer[22]=1;call(16,2,layer);
  let frame=0,time=performance.now();
  const render=()=>{host._browser_tick(time+=40);host._browser_render();return rt.pixels();};
  const present=color=>{
    const pixels=Buffer.alloc(320*240*2);for(let i=0;i<pixels.length;i+=2)pixels.writeUInt16LE(color,i);
    assert.equal(io(handle,2,pixels).result,pixels.length);
    const queue=Buffer.alloc(20);queue.writeBigUInt64LE(handle);queue.writeBigUInt64LE(BigInt(++frame),8);call(16,3,queue,false);return render();
  };
  const at=(x,y)=>[...rt.pixels().slice((y*320+x)*4,(y*320+x)*4+3)];
  present(0x001f);
  assert.deepEqual(at(15,15),[0,0,255],'opaque foreground over a positive-z Surface');
  const blended=at(45,15);assert.ok(blended[0]>=120&&blended[0]<=130&&blended[1]>=125&&blended[1]<=135&&blended[2]===0,JSON.stringify(blended));
  assert.deepEqual(at(70,15),[255,0,0],'zero alpha preserves the frame');
  assert.deepEqual(at(0,45),[255,255,255]);assert.deepEqual(at(319,45),[255,255,255]);
  const saved=Buffer.from(rt.pixels());for(let i=0;i<10;i++)render();assert.deepEqual(Buffer.from(rt.pixels()),saved,'idle rendering never reblends retained pixels');
  present(0xf800);assert.deepEqual(at(70,15),[0,0,255]);assert.deepEqual(at(15,15),[0,0,255]);
  rt.controller(32);rt.controller(0);render();assert.deepEqual(at(45,15),[0,0,255],'clearing only the overlay repaints its former bounds');
  rt.controller(16);rt.controller(0);render();assert.ok(at(45,15)[1]>120,'overlay-only updates appear without a Surface submission');
  present(0x001f);assert.deepEqual(at(45,15),blended,'later frames blend once against their new base');
  await rt.stop();
});

test('signed microphone permission survives absent encoder, capture open reports UNSUPPORTED',async()=>{
  const {rt,call,permission}=await platform({permissions:[{name:'audio.capture',scope:'microphone'}]});
  try{
    const p=permission('audio.capture','microphone');assert.ok(p);
    const result=call(10,6,num(p,8),true,false);
    assert.equal(result.readInt32LE(),-3,'unsupported capture is not an authorization denial');
  }finally{await rt.stop();}
});
test('real Sensor Core discovers signed channels, authorizes subscriptions and delivers native values',async()=>{
  const messages=[];
  const {host,rt,call,permission}=await platform({sensorMask:15,notify:m=>messages.push(m),permissions:[{name:'sensor.read',scope:'motion.acceleration'},{name:'sensor.read',scope:'motion.angular-velocity'},{name:'sensor.read',scope:'magnetic-field.0'},{name:'sensor.read',scope:'orientation.heading'}]});
  try{
    const list=call(8,1,Buffer.alloc(0));assert.ok(list.includes(Buffer.from('motion.acceleration')));assert.ok(list.includes(Buffer.from('motion.angular-velocity')));
    for(const [id,scope] of [[1,'motion.acceleration'],[2,'motion.angular-velocity'],[3,'magnetic-field.0'],[4,'orientation.heading']]){
      const p=permission('sensor.read',scope);
      const result=call(8,2,Buffer.concat([tlv(1,num(id,2)),tlv(2,num(id===3?100:id===4?50:20)),tlv(3,num(p,8))]));assert.ok(result.readBigUInt64LE(4));
    }
    const request=messages.filter(m=>m.type==='sensors-request').at(-1);assert.equal(request.mask,15);
    rt.sensorSample(request.id,[{id:1,values:[1,-2,9800000]},{id:2,values:[1000000,2000000,-3000000]},{id:3,values:[22500,-1750,42800]},{id:4,values:[42000000]}]);
    const scratch=host._malloc(4096);host._browser_tick(performance.now()+30);
    const samples=[];for(let i=0;i<30;i++){const n=host._browser_event(scratch,4096);if(!n)break;const b=Buffer.from(host.HEAPU8.slice(scratch,scratch+n));if(b.readUInt16LE(0)===8&&b.readUInt16LE(2)===0x8001)samples.push(b);}
    assert.equal(samples.length,4,'Core publishes all four actual sample events');
    assert.ok(list.includes(Buffer.from('magnetic-field.0')));
    assert.ok(list.includes(Buffer.from('orientation.heading')));
    assert.ok(samples.some(b=>b.includes(num(42000000))),'scalar heading reaches Core');
    assert.ok(samples.some(b=>b.includes(Buffer.concat([num(22500),num(0xfffff92a),num(42800)]))),'magnetic nT values reach the Core event queue unchanged');
    assert.ok(samples.some(b=>b.includes(Buffer.concat([num(1),num(0xfffffffe),num(9800000)]))));host._free(scratch);
    rt.pause(true);assert.equal(messages.at(-1).type,'sensors-stop');
  }finally{await rt.stop();}
});

test('unsigned microphone and sensor scopes cannot acquire authority or start native prompts',async()=>{
  const messages=[],{rt,call}=await platform({sensorMask:15,notify:m=>messages.push(m)});
  try{
    for(const [name,scope] of [['audio.capture','microphone'],['sensor.read','motion.acceleration'],['sensor.read','magnetic-field.0'],['sensor.read','magnetic-field.1'],['sensor.read','orientation.heading']]){
      const result=call(11,2,Buffer.concat([tlv(1,Buffer.from(name)),tlv(2,Buffer.from(scope))]),true,false);assert.equal(result.readInt32LE(),-4);
    }
    assert.equal(call(8,1,Buffer.alloc(0)).length,4,'no unsigned native inventory');
    assert.equal(messages.some(m=>m.type==='sensors-request'||m.type==='microphone-request'),false);
  }finally{await rt.stop();}
});

test('web authority fetches directly and never contacts a legacy proxy URL',async()=>{
  const paths=[];const server=http.createServer((req,res)=>{paths.push(req.url);res.end('native-direct');});
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));let rt;
  try{
    const origin=`http://127.0.0.1:${server.address().port}`;
    const p=await platform({networkOrigins:['web'],networkProxy:{url:origin+'/must-not-forward',csrf:'legacy'}});rt=p.rt;
    const permission=p.permission('net.client','web');
    const h=p.call(9,3,Buffer.concat([tlv(1,Buffer.from(origin+'/native')),tlv(2,num(1,2)),tlv(3,num(permission,8)),tlv(4,num(1024,8)),tlv(8,num(5000)),tlv(14,num(512)),tlv(15,num(0,8)),tlv(16,num(1024))])).readBigUInt64LE(4);
    const chunks=[];const deadline=Date.now()+5000;
    while(Date.now()<deadline){const r=p.io(h,1,Buffer.alloc(1024));if(r.result===-7){await new Promise(r=>setTimeout(r,1));continue;}assert.ok(r.result>=0);if(!r.result)break;chunks.push(r.data.subarray(0,r.result));}
    assert.equal(Buffer.concat(chunks).toString(),'native-direct');assert.deepEqual(paths,['/native']);
  }finally{await rt?.stop();await new Promise(resolve=>server.close(resolve));}
});

test('full network declaration inventory keeps the fourth sensor and last origin authorized',async()=>{
  const origins=Array.from({length:64},(_,i)=>`https://sensor-test-${i}.example.com`);
  const {rt,permission}=await platform({networkOrigins:origins,sensorMask:15,permissions:[
    {name:'net.socket',scope:'outbound'},{name:'audio.capture',scope:'microphone'},
    ...['motion.acceleration','motion.angular-velocity','magnetic-field.0','orientation.heading'].map(scope=>({name:'sensor.read',scope})),
  ]});
  try{assert.ok(permission('sensor.read','magnetic-field.0'));assert.ok(permission('sensor.read','orientation.heading'));assert.ok(permission('net.client',origins.at(-1)));}
  finally{await rt.stop();}
});

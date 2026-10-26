// Run after build.py: PXA_BROWSER_BUNDLE=/absolute/bundle node --test runtime.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {createRuntime,memoryRange} from './runtime.mjs';
const directory=process.env.PXA_BROWSER_BUNDLE;
assert.ok(directory,'Set PXA_BROWSER_BUNDLE to the compiled bundle');
const {default:createHost}=await import(pathToFileURL(path.join(directory,'pxa-browser.mjs')));
const hash=rt=>createHash('sha256').update(rt.pixels()).digest('hex');
async function app(name){return createRuntime(await createHost(),await fs.readFile(path.join(directory,`${name}.wasm`)));}
test('memory ranges reject overflow, unsigned negative pointers and stale views',()=>{
  const memory=new WebAssembly.Memory({initial:1,maximum:2});
  assert.equal(memoryRange(memory,65532,4).length,4);
  assert.throws(()=>memoryRange(memory,65532,5),RangeError);
  assert.throws(()=>memoryRange(memory,-1,1),RangeError);
  memory.grow(1);assert.equal(memoryRange(memory,65536,4).length,4);
});
test('unchanged C SDK counter: controller, release, pointer and static frame suppression',async()=>{
  const rt=await app('counter');let now=performance.now()+20;
  const first=rt.tick(now),before=hash(rt);
  assert.equal(rt.tick(now+=20),first,'static UI must not redraw each tick');
  rt.controller(16);rt.controller(0);rt.tick(now+=20);
  assert.notEqual(hash(rt),before,'A must increment the rendered counter');
  const one=hash(rt);
  rt.pointer(160,160,true);rt.pointer(160,160,false);rt.tick(now+=20);
  assert.notEqual(hash(rt),one,'native LVGL button must receive a click');
  assert.ok(rt.stats().callbacks>=4);
});
test('C++ SDK receives startup display environment and renders',async()=>{
  const rt=await app('cpp');rt.tick(performance.now()+20);
  assert.ok(new Set(rt.pixels()).size>10);
  assert.ok(rt.stats().callbacks>=1);
});
test('PXA clock drives animated Canvas with bounded control copies',async t=>{
  const rt=await app('paddle');const costs=[];let now=performance.now(),frames=0;
  for(let i=0;i<600;i++){
    const begin=performance.now();frames=rt.tick(now+=17);costs.push(performance.now()-begin);
  }
  assert.ok(frames>=590);assert.ok(rt.stats().callbacks>=590);
  assert.ok(rt.stats().copiedBytes<1024*1024,'commands, not full guest memory, cross the bridge');
  costs.sort((a,b)=>a-b);
  t.diagnostic(JSON.stringify({frames,p50Ms:costs[300],p95Ms:costs[570],...rt.stats()}));
});
test('ordinary SDK notes app: UTF-8 editor, KV, private file, wall clock and audible PCM',async()=>{
  const host=await createHost(),messages=[],logs=[];
  const rt=await createRuntime(host,await fs.readFile(path.join(directory,'notes.wasm')),
    {appId:'notes-test',notify:m=>messages.push(m),log:m=>logs.push(m)});
  rt.tick(performance.now()+30);
  assert.ok(logs.some(x=>x.includes('Audio ready: 1')),logs.join('\n'));
  assert.ok(logs.some(x=>x.includes('Wall clock status: 0')));
  rt.pointer(100,75,true);rt.pointer(100,75,false);
  const input=messages.find(m=>m.type==='ime'&&m.text!==null);
  assert.ok(input,'click must focus the native editor bridge');
  assert.equal(rt.edit({session:input.session,text:'中文一行 🎮',cursor:7}),0);
  assert.equal(rt.edit({session:input.session-1,text:'stale'}),-10,'stale editor reply must not edit another field');
  rt.controller(16);rt.controller(0);rt.tick(performance.now()+50);
  assert.equal(host.FS.readFile('/data/files/note.txt',{encoding:'utf8'}),'中文一行 🎮');
  assert.ok(logs.some(x=>x.includes('Storage status: 0')));
  const pcm=rt.audio();assert.equal(pcm.length,960);
  assert.ok(pcm.some(x=>Math.abs(x)>0.01),'Host tone must produce non-silent PCM');
  await rt.stop();
});

test('JSPI event loop: allocator-free startup, foreground, input, pause and stop',async()=>{
  const messages=[];
  const rt=await createRuntime(await createHost(),await fs.readFile(path.join(directory,'mainloop-test.wasm')),
    {mainLoop:true,log:value=>messages.push(value)});
  const turn=()=>new Promise(resolve=>setTimeout(resolve,20));
  await turn();rt.tick(performance.now());
  assert.ok(messages.some(s=>s.includes('focus:1')),'startup delivers foreground');
  const before=hash(rt);rt.controller(16);await turn();rt.controller(0);rt.tick(performance.now());
  assert.notEqual(hash(rt),before,'suspended Guest must receive controller events');
  rt.focus(false);await turn();rt.pause(true);
  assert.ok(messages.some(s=>s.includes('focus:0')));
  const paused=hash(rt);rt.controller(32);await turn();rt.tick(performance.now());
  assert.equal(hash(rt),paused,'paused Guest must not consume inputs');
  rt.controller(0);rt.focus(true);rt.pause(false);await turn();
  await rt.stop();assert.ok(messages.some(s=>s.includes('stopped')));
});

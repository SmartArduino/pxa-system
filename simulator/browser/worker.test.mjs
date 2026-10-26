import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {Worker} from 'node:worker_threads';
test('Worker schedules itself, bounds fallback presentation, pauses and resumes',async()=>{
  const bundle=process.env.PXA_BROWSER_BUNDLE;assert.ok(bundle);
  const worker=new Worker(new URL('./worker-node-fixture.mjs',import.meta.url),{
    workerData:{module:pathToFileURL(path.join(bundle,'worker.mjs')).href}});
  let frames=0,error;
  worker.on('error',e=>{error=e;});
  worker.on('message',m=>{
    if(m.type==='error')error=new Error(m.message);
    if(m.type==='frame'){frames++;worker.postMessage({type:'presented'});}
  });
  const until=async condition=>{
    const deadline=Date.now()+5000;
    while(!condition()){
      if(error)throw error;if(Date.now()>deadline)throw new Error('Worker timed out');
      await new Promise(r=>setTimeout(r,20));
    }
  };
  try{
    const file=await fs.readFile(path.join(bundle,'paddle.wasm'));
    const binary=file.buffer.slice(file.byteOffset,file.byteOffset+file.byteLength);
    worker.postMessage({type:'start',width:320,height:240,binary},[binary]);
    await until(()=>frames>=10);
    worker.postMessage({type:'pause',paused:true});
    await new Promise(r=>setTimeout(r,100));const pausedFrames=frames;
    await new Promise(r=>setTimeout(r,120));assert.equal(frames,pausedFrames);
    worker.postMessage({type:'pause',paused:false});await until(()=>frames>=pausedFrames+5);
    assert.equal(error,undefined);
  }finally{await worker.terminate();}
});

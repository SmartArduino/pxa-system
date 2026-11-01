// SPDX-License-Identifier: MIT
import test from 'node:test';
import assert from 'node:assert/strict';
import {installBrowserCapture} from './capture.mjs';
test('Opus capture preserves packets, bounds backpressure, closes frames and ignores late sessions',async()=>{
  const previous=[globalThis.AudioEncoder,globalThis.AudioData],errors=[],packets=[],messages=[];let encoder,closed=0;
  globalThis.AudioData=class{constructor(data){Object.assign(this,data);}close(){closed++;}};
  globalThis.AudioEncoder=class{
    static async isConfigSupported(){return {supported:true};}
    constructor(callbacks){encoder=this;this.callbacks=callbacks;this.encodeQueueSize=0;}
    configure(config){assert.equal(config.sampleRate,16000);}
    encode(data){assert.equal(data.numberOfFrames,320);this.callbacks.output({byteLength:3,copyTo(out){out.set([1,2,3]);}});}
    close(){this.closed=true;}
  };
  try{
    const host={HEAPU8:new Uint8Array(64),_malloc:()=>16,_free(){},_browser_capture_packet:(id,p,n)=>packets.push([id,[...host.HEAPU8.slice(p,p+n)]]),_browser_capture_error:(...e)=>errors.push(e)};
    const capture=await installBrowserCapture(host,m=>messages.push(m));
    assert.equal(host.browserCaptureAvailable,true);assert.equal(host.browserCaptureStart(1),0);
    assert.equal(messages.at(-1).type,'microphone-request');capture.pcm(1,new Float32Array(320));
    assert.deepEqual(packets,[[1,[1,2,3]]]);assert.equal(closed,1);
    encoder.encodeQueueSize=4;capture.pcm(1,new Float32Array(320));assert.equal(packets.length,1);
    capture.error(1,-13);assert.deepEqual(errors,[[1,-13]]);assert.equal(messages.at(-1).type,'microphone-stop');
    capture.pcm(1,new Float32Array(320));assert.equal(packets.length,1);
    globalThis.AudioEncoder=class{static async isConfigSupported(){return {supported:false};}};
    const unsupported={};await installBrowserCapture(unsupported,()=>{});assert.equal(unsupported.browserCaptureAvailable,false);
  }finally{[globalThis.AudioEncoder,globalThis.AudioData]=previous;}
});

// SPDX-License-Identifier: MIT
import createHost from './pxa-browser.mjs';
import {createRuntime} from './runtime.mjs';
let runtime,context,image,revision=-1,frames=0,started=false,paused=false;
let timer=0,nextDeadline=0,lastStats=0,costSum=0,costSamples=0,presentPending=false;
let audioPort;
let measuredGuestWork=0;
const PERIOD=1000/60;
function fail(error){clearTimeout(timer);paused=true;postMessage({type:'error',message:String(error?.message||error).slice(0,500)});}
function stats(){const values=runtime.stats();costSum+=Math.max(0,values.guestWorkMs-measuredGuestWork);measuredGuestWork=values.guestWorkMs;postMessage({type:'tick',frames,workMs:costSamples?costSum/costSamples:0,...values});costSum=costSamples=0;lastStats=performance.now();}
function tick(){
  if(paused||!runtime)return;
  try{
    const guestBefore=runtime.stats().guestWorkMs;
    const begin=performance.now();const next=runtime.tick(begin);
    // OffscreenCanvas presents without a page round trip. The fallback allows
    // only one transferred frame in flight; slow consumers skip presentation.
    if(next!==revision&&!presentPending){
      revision=next;
      const bgra=runtime.pixels(),rgba=image?.data||new Uint8ClampedArray(bgra.length);
      for(let i=0;i<bgra.length;i+=4){rgba[i]=bgra[i+2];rgba[i+1]=bgra[i+1];rgba[i+2]=bgra[i];rgba[i+3]=255;}
      if(context){context.putImageData(image,0,0);frames++;}
      else{presentPending=true;postMessage({type:'frame',pixels:rgba.buffer},[rgba.buffer]);}
    }
    costSum+=Math.max(0,performance.now()-begin-(runtime.stats().guestWorkMs-guestBefore));costSamples++;
    if(begin-lastStats>=1000)stats();
    nextDeadline+=PERIOD;
    const now=performance.now();if(nextDeadline<now)nextDeadline=now;
    timer=setTimeout(tick,Math.max(0,nextDeadline-now));
  }catch(error){fail(error);}
}
onmessage=async({data})=>{
  try{
    if(data.type==='start'){
      if(started)throw new Error('Worker already started');started=true;
      const {width,height}=data;
      if(!Number.isInteger(width)||!Number.isInteger(height)||width<64||height<64||width>1024||height>1024)throw new Error('Invalid display size');
      if(data.canvas){context=data.canvas.getContext('2d',{alpha:false});if(!context)throw new Error('Canvas initialization failed');image=new ImageData(width,height);}
      const host=await createHost({print:()=>{},printErr:()=>{}});
      runtime=await createRuntime(host,data.binary,{width,height,locale:data.locale,appId:data.appId,networkOrigins:data.networkOrigins,mainLoop:data.mainLoop,assets:data.assets,
        notify:message=>postMessage(message),log:message=>postMessage({type:'log',message})});
      if(data.audioPort){
        audioPort=data.audioPort;
        audioPort.onmessage=({data})=>{
          if(data.type!=='need'||paused)return;
          for(let i=0;i<Math.min(3,data.packets);i++){
            const samples=runtime.audio();audioPort.postMessage({type:'samples',samples,epoch:data.epoch},[samples.buffer]);
          }
        };
      }
      postMessage({type:'ready'});nextDeadline=lastStats=performance.now();tick();
    }else if(data.type==='pause'&&runtime){
      clearTimeout(timer);paused=!!data.paused;
      runtime.focus(!paused);runtime.pause(paused);
      audioPort?.postMessage({type:'pause',paused});
      if(paused)runtime.flush().catch(error=>postMessage({type:'storage-error',message:String(error)}));
      if(!paused){nextDeadline=lastStats=performance.now();costSum=costSamples=0;tick();}
      else stats();
    }else if(data.type==='presented'&&presentPending){presentPending=false;frames++;}
    else if(data.type==='pointer'&&runtime){runtime.pointer(data.x,data.y,data.down);}
    else if(data.type==='controller'&&runtime){runtime.controller(data.buttons);}
    else if(data.type==='back'&&runtime){runtime.back();}
    else if(data.type==='volume-key'&&runtime){runtime.volumeKey(data.key);}
    else if(data.type==='focus'&&runtime){runtime.focus(data.focused);}
    else if(data.type==='edit'&&runtime){runtime.edit(data);}
    else if(data.type==='stop'&&runtime){
      paused=true;clearTimeout(timer);audioPort?.close();await runtime.stop();postMessage({type:'stopped'});close();
    }
  }catch(error){fail(error);}
};

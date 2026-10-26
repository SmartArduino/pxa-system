// SPDX-License-Identifier: MIT
// Only bounded PCM queue/consumption runs on the real-time audio thread.
// PXA synthesis/mixing stays in the Host worker, outside the UI event loop.
class PxaAudioOutput extends AudioWorkletProcessor {
  constructor(){
    super();this.queue=[];this.offset=0;this.stopped=false;this.outstanding=3;this.epoch=0;
    this.port.onmessage=({data})=>{
      if(data.type==='samples'){
        if(data.epoch!==this.epoch)return;
        this.outstanding=Math.max(0,this.outstanding-1);
        if(!this.stopped&&data.samples instanceof Float32Array&&data.samples.length===960&&this.queue.length<4)
          this.queue.push(data.samples);
      }else if(data.type==='pause'){this.epoch++;this.stopped=!!data.paused;this.queue=[];this.offset=0;this.outstanding=0;}
    };
    this.port.postMessage({type:'need',packets:3,epoch:this.epoch});
  }
  process(inputs,outputs){
    const out=outputs[0]?.[0];if(!out)return true;
    let written=0;
    while(!this.stopped&&written<out.length&&this.queue.length){
      const source=this.queue[0],count=Math.min(out.length-written,source.length-this.offset);
      out.set(source.subarray(this.offset,this.offset+count),written);
      written+=count;this.offset+=count;
      if(this.offset===source.length){this.queue.shift();this.offset=0;}
    }
    // Browser clears the untouched portion to silence before each callback.
    const needed=3-this.queue.length-this.outstanding;
    if(!this.stopped&&needed>0){this.outstanding+=needed;this.port.postMessage({type:'need',packets:needed,epoch:this.epoch});}
    return true;
  }
}
registerProcessor('pxa-output',PxaAudioOutput);

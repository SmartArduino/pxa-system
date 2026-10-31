// SPDX-License-Identifier: MIT
class PxaMicrophone extends AudioWorkletProcessor{
  constructor(){super();this.phase=0;this.sum=0;this.count=0;this.frame=new Float32Array(320);this.offset=0;}
  process(inputs){
    const input=inputs[0]?.[0];if(!input)return true;
    for(const value of input){
      this.sum+=value;this.count++;this.phase+=16000;
      if(this.phase>=sampleRate){
        this.phase-=sampleRate;this.frame[this.offset++]=this.sum/this.count;this.sum=this.count=0;
        if(this.offset===320){this.port.postMessage(this.frame,[this.frame.buffer]);this.frame=new Float32Array(320);this.offset=0;}
      }
    }
    return true;
  }
}
registerProcessor('pxa-microphone',PxaMicrophone);

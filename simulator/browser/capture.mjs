// SPDX-License-Identifier: MIT
export async function installBrowserCapture(host,notify){
  const config={codec:'opus',sampleRate:16000,numberOfChannels:1,bitrate:24000,
    opus:{format:'opus',frameDuration:20000,application:'voip',signal:'voice',complexity:5}};
  host.browserCaptureAvailable=false;
  if(typeof AudioEncoder==='function'&&typeof AudioData==='function'){
    try{host.browserCaptureAvailable=(await AudioEncoder.isConfigSupported(config)).supported;}catch{}
  }
  let encoder=null,session=0,timestamp=0;
  const stop=id=>{
    if(id!==session)return;
    encoder?.close();encoder=null;session=0;
    notify({type:'microphone-stop',id});
  };
  host.browserCaptureStart=id=>{
    if(session)stop(session);
    session=id;timestamp=0;
    encoder=new AudioEncoder({output:chunk=>{
      if(session!==id)return;
      if(chunk.byteLength>1275){host._browser_capture_error(id,-16);return;}
      const bytes=new Uint8Array(chunk.byteLength);chunk.copyTo(bytes);
      const pointer=host._malloc(bytes.length);
      if(!pointer){host._browser_capture_error(id,-9);return;}
      try{host.HEAPU8.set(bytes,pointer);host._browser_capture_packet(id,pointer,bytes.length);}finally{host._free(pointer);}
    },error:()=>{if(session===id){host._browser_capture_error(id,-14);stop(id);}}});
    encoder.configure(config);notify({type:'microphone-request',id});
  };
  host.browserCaptureStop=stop;
  return {
    pcm(id,samples){
      if(id!==session||!encoder||!(samples instanceof Float32Array)||samples.length!==320||encoder.encodeQueueSize>=4)return;
      const data=new AudioData({format:'f32-planar',sampleRate:16000,numberOfFrames:320,numberOfChannels:1,timestamp,data:samples});
      timestamp+=20000;try{encoder.encode(data);}finally{data.close();}
    },
    error(id,status=-4){if(id===session){host._browser_capture_error(id,status);stop(id);}},
    close(){if(session)stop(session);},
  };
}

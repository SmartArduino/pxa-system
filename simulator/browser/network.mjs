// SPDX-License-Identifier: MIT
// Only the trusted host gets Fetch. Requests are checked by PXA permissions
// and again here. Native Fetch owns CORS, mixed content and redirects.
// There is no server relay or fallback transport.
export function browserRequestUrl(value,origins,secure=globalThis.isSecureContext===true){
  const url=new URL(value),web=origins.includes('web');
  if(!['http:','https:'].includes(url.protocol)||url.username||url.password||
     !(origins.includes(url.origin)||web))throw new Error('Origin denied');
  if(secure&&url.protocol==='http:'&&!['localhost','127.0.0.1','[::1]'].includes(url.hostname)){
    const https=new URL(url);https.protocol='https:';
    // A concrete HTTP authority must never silently gain an HTTPS authority.
    if(web||origins.includes(https.origin))return https;
  }
  return url;
}
const forbiddenHeader=name=>/^(?:host|connection|cookie|content-length|transfer-encoding|accept-encoding|user-agent|referer|origin|te|upgrade|proxy-.*|sec-.*)$/i.test(name);
export function installBrowserNetwork(host, origins=[]) {
  if(!Array.isArray(origins)||origins.length>64)throw new Error('Invalid network declarations');
  host.browserNetworkOrigins=origins.map(value=>{
    if(value==='web')return value;
    const url=new URL(value);
    if(url.origin!==value||!(url.protocol==='https:'||url.protocol==='http:'))
      throw new Error('Network permissions must be exact HTTP/HTTPS origins');
    return value;
  });
  host.browserStreamingUpload=false;
  try {
    let duplex=false;
    const request=new Request('https://localhost/',{method:'POST',body:new ReadableStream(),get duplex(){duplex=true;return 'half';}});
    host.browserStreamingUpload=duplex&&!request.headers.has('Content-Type');
  } catch {}
  const pending=new Map(),encoder=new TextEncoder();
  const bytes=(data,fn)=>{
    const p=host._malloc(Math.max(1,data.length));if(!p)throw new Error('Network allocation failed');
    try{host.HEAPU8.set(data,p);return fn(p,data.length);}finally{host._free(p);}
  };
  const valid=state=>pending.get(state.id)===state;
  const wake=state=>{state.wake?.();state.wake=null;};
  const wait=state=>new Promise(resolve=>{state.wake=resolve;});
  host.browserNetWake=id=>{const state=pending.get(id);if(state)wake(state);};
  host.browserNetClose=id=>{
    const state=pending.get(id);if(!state)return;
    pending.delete(id);clearTimeout(state.timer);state.abort.abort();wake(state);
  };
  host.browserNetStart=options=>{
    const state={...options,abort:new AbortController(),wake:null};
    pending.set(state.id,state);
    state.timer=setTimeout(()=>{
      if(valid(state)){host._browser_net_done(state.id,-12);state.abort.abort();wake(state);}
    },options.timeout);
    (async()=>{
      const url=browserRequestUrl(options.url,host.browserNetworkOrigins);
      let body=options.body?.length?options.body:undefined;
      if(options.upload){
        body=new ReadableStream({async pull(controller){
          while(valid(state)&&!state.abort.signal.aborted){
            const p=host._malloc(16384);if(!p)throw new Error('Network allocation failed');
            let n;
            try{n=host._browser_net_upload(state.id,p,16384);if(n>0)controller.enqueue(host.HEAPU8.slice(p,p+n));}
            finally{host._free(p);}
            if(n>0)return;
            if(n===0){controller.close();return;}
            if(n!==-7)throw new Error('Upload failed');
            await wait(state);
          }
          controller.error(new Error('Upload cancelled'));
        }},{highWaterMark:0});
      }
      const response=await fetch(url,{method:options.method,
        headers:options.headers.filter(([name])=>!forbiddenHeader(name)),body,
        ...(options.upload?{duplex:'half'}:{}),credentials:'omit',
        redirect:host.browserNetworkOrigins.includes('web')?'follow':'error',
        cache:'no-store',signal:state.abort.signal});
      if(!valid(state)||state.abort.signal.aborted)return;
      // Unknown length is deliberate: Fetch can decompress a response without
      // rewriting Content-Length. The byte budget counts decoded body bytes.
      const names=new Set(['content-type',...options.wanted]);
      for(const name of names){
        const value=response.headers.get(name);if(value==null)continue;
        const n=encoder.encode(name),v=encoder.encode(value);
        // Buffered Net replies retain their legacy field limits. HTTP_OPEN
        // delegates the total encoded head budget to the bounded C writer.
        if(n.length>64||(!options.streaming&&(v.length>256||(name==='content-type'&&v.length>96))))throw new Error('Response header exceeds PXA limits');
        bytes(n,p=>bytes(v,(q,size)=>{if(host._browser_net_header(state.id,p,n.length,q,size)!==1)throw new Error('Response header rejected');}));
      }
      host._browser_net_headers(state.id,response.status);
      const reader=response.body?.getReader();
      if(reader)while(valid(state)&&!state.abort.signal.aborted){
        const {value,done}=await reader.read();if(done)break;
        let offset=0;
        while(offset<value.length&&valid(state)&&!state.abort.signal.aborted){
          const chunk=value.subarray(offset,offset+16384);
          const n=bytes(chunk,(p,size)=>host._browser_net_receive(state.id,p,size));
          if(n<0)throw new Error('Response body exceeds budget');
          if(n)offset+=n;else await wait(state);
        }
      }
      if(valid(state)&&!state.abort.signal.aborted)host._browser_net_done(state.id,0);
    })().catch(error=>{
      if(valid(state)&&!state.abort.signal.aborted){
        host._browser_net_done(state.id,-14);state.abort.abort();wake(state);
        let origin='invalid';try{origin=new URL(options.url).origin;}catch{}
        host.browserNotify?.({type:'log',level:2,message:`Browser direct network failed: origin=${origin} reason=${error?.name||'Error'}; browser CORS/HTTPS rules apply`});
      }
    }).finally(()=>clearTimeout(state.timer));
  };
  return {close(){for(const id of pending.keys())host.browserNetClose(id);}};
}

// SPDX-License-Identifier: MIT
// Two independent credit windows keep WebSocket and PXA queues bounded.
export function installBrowserSockets(host,proxy,permissions=[]){
  const enabled=!!proxy?.socketUrl&&permissions.some(p=>p.name==='net.socket'&&p.scope==='outbound');
  host.browserSocketAvailable=enabled;
  const pending=new Map();
  const bytes=(data,fn)=>{const p=host._malloc(Math.max(1,data.length));if(!p)throw new Error('Socket allocation failed');try{host.HEAPU8.set(data,p);return fn(p,data.length);}finally{host._free(p);}};
  host.browserSocketStart=({id,endpoint,timeout})=>{
    if(!enabled){host._browser_socket_state(id,2,-2);return;}
    const url=new URL(proxy.socketUrl);url.protocol=url.protocol==='https:'?'wss:':'ws:';
    const socket=new WebSocket(url);socket.binaryType='arraybuffer';
    const state={socket,incoming:null,offset:0,sending:false,connected:false};pending.set(id,state);
    const valid=()=>pending.get(id)===state;
    const fail=()=>{if(valid()){clearTimeout(state.timer);host._browser_socket_state(id,2,-14);socket.close();pending.delete(id);}};
    state.timer=setTimeout(fail,timeout);
    socket.onopen=()=>socket.send(JSON.stringify({csrf:proxy.csrf,endpoint}));
    socket.onmessage=({data})=>{
      if(!valid())return;
      try{
        if(typeof data==='string'){
          const message=JSON.parse(data);
          if(message.type==='connected'){clearTimeout(state.timer);state.connected=true;host._browser_socket_state(id,1,0);}
          else if(message.type==='sent')state.sending=false;else throw Error('Invalid socket state');
        }else{
          if(state.incoming||data.byteLength>16384)throw Error('Socket credit exceeded');
          state.incoming=new Uint8Array(data);state.offset=0;
        }
        host.browserSocketWake(id);
      }catch{fail();}
    };
    socket.onclose=event=>{if(valid()){clearTimeout(state.timer);host._browser_socket_state(id,2,event.code===1000?0:-14);pending.delete(id);}};
    socket.onerror=fail;
  };
  host.browserSocketWake=id=>{
    const state=pending.get(id);if(!state||!state.connected||state.socket.readyState!==1)return;
    if(state.incoming){
      const n=bytes(state.incoming.subarray(state.offset),(p,n)=>host._browser_socket_receive(id,p,n));
      if(n<0){host.browserSocketClose(id);return;}
      state.offset+=n;
      if(state.offset===state.incoming.length){state.incoming=null;state.socket.send('ack');}
    }
    if(!state.sending){
      const p=host._malloc(16384);if(!p)return;
      try{const n=host._browser_socket_upload(id,p,16384);if(n>0){state.sending=true;state.socket.send(host.HEAPU8.slice(p,p+n));}}
      finally{host._free(p);}
    }
  };
  host.browserSocketClose=id=>{const state=pending.get(id);if(!state)return;pending.delete(id);clearTimeout(state.timer);state.socket.close();};
  return {close(){for(const id of pending.keys())host.browserSocketClose(id);}};
}

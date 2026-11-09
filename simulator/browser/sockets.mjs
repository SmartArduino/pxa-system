// SPDX-License-Identifier: MIT
// Native message WebSockets. Raw TCP/TLS stays unavailable; no server tunnel.
export function installBrowserSockets(host,permissions=[],Socket=globalThis.WebSocket){
  host.browserSocketAvailable=false;
  host.browserSocketDeclared=permissions.some(p=>p.name==='net.socket'&&p.scope==='outbound');
  const grants=permissions.filter(p=>p.name==='net.socket').map(p=>p.scope);
  if(grants.length>64||grants.some(s=>typeof s!=='string'||!s.length||s.length>512))throw new Error('Invalid socket declarations');
  host.browserSocketScopes=grants;
  const sockets=new Map(),decoder=new TextDecoder('utf-8',{fatal:true}),encoder=new TextEncoder();
  let changed=false;
  const writable=s=>s.state===1&&s.socket.bufferedAmount+s.limit+15<=s.budget;
  const signal=()=>{changed=true;host.browserNetworkWake?.();};
  const dispose=s=>{clearTimeout(s.timer);s.socket.onopen=s.socket.onmessage=s.socket.onerror=s.socket.onclose=null;try{s.socket.close();}catch{}};
  host.browserWebSocketOpen=(id,value,budget,limit,timeout)=>{
    if(typeof Socket!=='function')return -3;
    if(sockets.size>=8||budget<512||budget>65536||limit+3>budget/2)return -16;
    let url;try{url=new URL(value);}catch{return -1;}
    if(!['ws:','wss:'].includes(url.protocol)||url.username||url.password||url.hash||
       !(grants.includes('outbound')||grants.includes(value)))return -4;
    let socket;try{socket=new Socket(url.href);}catch{return -13;}
    const s={id,socket,budget:budget/2,limit,queue:[],bytes:0,state:0,error:0};
    sockets.set(id,s);socket.binaryType='arraybuffer';
    const fail=error=>{if(sockets.get(id)!==s)return;s.error=error;s.state=2;dispose(s);signal();};
    s.timer=setTimeout(()=>fail(-12),timeout);
    socket.onopen=()=>{if(sockets.get(id)!==s)return;clearTimeout(s.timer);s.state=1;signal();};
    socket.onerror=()=>fail(-13);
    socket.onclose=event=>{if(sockets.get(id)!==s)return;clearTimeout(s.timer);s.state=2;if(event.code!==1000&&event.code!==1001)s.error=-13;signal();};
    socket.onmessage=event=>{
      if(sockets.get(id)!==s||s.state!==1)return;
      const opcode=typeof event.data==='string'?1:2;
      const data=opcode===1?encoder.encode(event.data):new Uint8Array(event.data);
      if(data.length>limit||s.bytes+data.length+3>s.budget||s.queue.length>=64){fail(-16);return;}
      s.queue.push({opcode,data});s.bytes+=data.length+3;signal();
    };
    return 0;
  };
  host.browserWebSocketIO=(id,op,p,n)=>{
    const s=sockets.get(id);if(!s)return -2;
    if(op===3)return (s.queue.length?1:0)|(writable(s)?2:0)|(s.state===2?8:0);
    if(op===4)return s.state===1?1:s.state===2?s.error||0:-7;
    if(op===1){
      const front=s.queue[0];
      if(!front)return s.state===2?s.error||0:-7;
      if(n<front.data.length+1)return -16;
      host.HEAPU8[p]=front.opcode;host.HEAPU8.set(front.data,p+1);
      s.queue.shift();s.bytes-=front.data.length+3;
      return front.data.length+1;
    }
    if(op===2){
      if(s.state!==1)return s.state===2?s.error||-2:-7;
      if(n<1||n>s.limit+1)return -1;
      const opcode=host.HEAPU8[p];if(opcode!==1&&opcode!==2)return -1;
      if(s.socket.bufferedAmount+n+14>s.budget)return -7;
      const data=host.HEAPU8.slice(p+1,p+n);
      try{s.socket.send(opcode===1?decoder.decode(data):data);}catch{return -15;}
      return n;
    }
    return -3;
  };
  host.browserWebSocketClose=id=>{const s=sockets.get(id);if(!s)return;sockets.delete(id);dispose(s);signal();};
  host.browserWebSocketPoll=()=>{
    for(const s of sockets.values()){
      const current=writable(s);if(s.writable!==current){s.writable=current;changed=true;}
    }
    const value=changed;changed=false;return value?1:0;
  };
  return {close(){for(const id of [...sockets.keys()])host.browserWebSocketClose(id);}};
}

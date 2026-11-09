// SPDX-License-Identifier: MIT
import test from 'node:test';
import assert from 'node:assert/strict';
import {installBrowserSockets} from './sockets.mjs';
class Socket {
  static instances=[];
  bufferedAmount=0;sent=[];closed=false;
  constructor(url){this.url=url;Socket.instances.push(this);}
  send(value){this.sent.push(value);}
  close(){this.closed=true;}
}
const make=(permissions=[{name:'net.socket',scope:'outbound'}])=>{
  const host={HEAPU8:new Uint8Array(4096)};
  const session=installBrowserSockets(host,permissions,Socket);
  return {host,session};
};
test('browser uses native WebSocket only and enforces the declared socket grant',()=>{
  const {host,session}=make([{name:'net.client',scope:'web'}]);
  assert.equal(host.browserSocketAvailable,false);
  assert.equal(host.browserWebSocketOpen(1,'ws://127.0.0.1:31889/dewfield',512,64,1000),-4);
  assert.equal(host.browserWebSocketOpen(1,'tcp://127.0.0.1:31889',512,64,1000),-4);
  session.close();
});
test('exact URL grants do not authorize another path or port',()=>{
  const url='ws://127.0.0.1:31889/dewfield';
  const {host,session}=make([{name:'net.socket',scope:url}]);
  assert.equal(host.browserWebSocketOpen(1,url,512,64,1000),0);
  assert.equal(host.browserWebSocketOpen(2,url+'/other',512,64,1000),-4);
  assert.equal(host.browserWebSocketOpen(3,'ws://127.0.0.1:31890/dewfield',512,64,1000),-4);
  session.close();
});
test('message boundaries, short reads, empty binary messages and backpressure',()=>{
  const {host,session}=make();
  assert.equal(host.browserWebSocketOpen(2,'ws://127.0.0.1:31889/dewfield',512,64,1000),0);
  const socket=Socket.instances.at(-1);assert.equal(socket.binaryType,'arraybuffer');
  assert.equal(host.browserWebSocketIO(2,4,0,0),-7);socket.onopen();
  assert.equal(host.browserWebSocketIO(2,4,0,0),1);
  socket.onmessage({data:new Uint8Array([4,5,6]).buffer});
  assert.equal(host.browserWebSocketIO(2,1,100,3),-16);
  assert.equal(host.browserWebSocketIO(2,1,100,4),4);
  assert.deepEqual([...host.HEAPU8.slice(100,104)],[2,4,5,6]);
  socket.onmessage({data:new ArrayBuffer(0)});
  assert.equal(host.browserWebSocketIO(2,1,100,1),1);
  assert.equal(host.HEAPU8[100],2);
  host.HEAPU8.set([2,7,8],100);
  assert.equal(host.browserWebSocketIO(2,2,100,3),3);
  assert.deepEqual([...socket.sent[0]],[7,8]);
  host.browserWebSocketPoll();socket.bufferedAmount=250;
  assert.equal(host.browserWebSocketIO(2,2,100,3),-7);
  assert.equal(socket.sent.length,1);
  assert.equal(host.browserWebSocketPoll(),1);
  assert.equal(host.browserWebSocketIO(2,3,0,0)&2,0);
  socket.bufferedAmount=0;
  assert.equal(host.browserWebSocketPoll(),1,'drained native buffers must wake blocked Guest writes');
  assert.equal(host.browserWebSocketIO(2,3,0,0)&2,2);
  session.close();assert.equal(socket.closed,true);
});
test('receive flooding closes the socket instead of growing the queue',()=>{
  const {host,session}=make();host.browserWebSocketOpen(3,'ws://127.0.0.1:31889/dewfield',512,64,1000);
  const socket=Socket.instances.at(-1);socket.onopen();
  for(let i=0;i<4;i++)socket.onmessage({data:new Uint8Array(60).buffer});
  socket.onmessage({data:new Uint8Array(60).buffer});assert.equal(socket.closed,true);
  for(let i=0;i<4;i++)assert.equal(host.browserWebSocketIO(3,1,0,100),61);
  assert.equal(host.browserWebSocketIO(3,1,0,100),-16);
  session.close();
});
test('stale browser callbacks cannot recreate a closed resource',()=>{
  const {host,session}=make();host.browserWebSocketOpen(4,'wss://example.test/dewfield',512,64,1000);
  const socket=Socket.instances.at(-1),late=socket.onopen;
  host.browserWebSocketClose(4);late();assert.equal(host.browserWebSocketIO(4,4,0,0),-2);
  session.close();
});

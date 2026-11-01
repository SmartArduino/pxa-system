// SPDX-License-Identifier: MIT
import test from 'node:test';
import assert from 'node:assert/strict';
import {browserRequestUrl,installBrowserNetwork} from './network.mjs';
test('native URL policy upgrades only within signed authority and preserves loopback tests',()=>{
  assert.equal(browserRequestUrl('http://music.163.com/api/test',['web'],true).href,'https://music.163.com/api/test');
  assert.equal(browserRequestUrl('http://provider.test/api',['http://provider.test'],true).protocol,'http:');
  assert.equal(browserRequestUrl('http://provider.test/api',['http://provider.test','https://provider.test'],true).protocol,'https:');
  assert.equal(browserRequestUrl('http://127.0.0.1/test',['web'],true).protocol,'http:');
  for(const url of ['file:///etc/passwd','https://user:password@example.com/','https://foreign.test/'])assert.throws(()=>browserRequestUrl(url,['https://example.com'],true));
});
test('HTTP transport fetches upstream locally, omits cookies/forbidden headers and ignores legacy proxy',async()=>{
  const previous=globalThis.fetch,requests=[],headers=[],received=[];let resolve,heapAt=128;
  const finished=new Promise(r=>resolve=r),host={HEAPU8:new Uint8Array(65536),_malloc:n=>{const p=heapAt;heapAt+=Math.max(8,n);return p;},_free(){},
    _browser_net_header:(id,p,n,q,s)=>{headers.push([new TextDecoder().decode(host.HEAPU8.slice(p,p+n)),new TextDecoder().decode(host.HEAPU8.slice(q,q+s))]);return 1;},
    _browser_net_headers:(id,status)=>assert.equal(status,206),_browser_net_receive:(id,p,n)=>{received.push([...host.HEAPU8.slice(p,p+n)]);return n;},_browser_net_done:(id,status)=>resolve(status)};
  globalThis.fetch=async(url,options)=>{requests.push({url:String(url),options});return new Response(new Uint8Array([1,2,3]),{status:206,headers:{'content-type':'audio/mpeg'}});};
  const net=installBrowserNetwork(host,['web'],{url:'https://store.test/forbidden-relay',csrf:'must-not-leave'});
  try{
    host.browserNetStart({id:1,url:'https://provider.test/audio',method:'GET',headers:[['User-Agent','app-UA'],['Cookie','secret'],['Referer','https://other.test/'],['Range','bytes=0-2']],wanted:[],timeout:1000});
    assert.equal(await finished,0);assert.equal(requests.length,1);assert.equal(requests[0].url,'https://provider.test/audio');
    assert.equal(requests[0].options.credentials,'omit');assert.equal(requests[0].options.redirect,'follow');assert.deepEqual(requests[0].options.headers,[['Range','bytes=0-2']]);
    assert.deepEqual(headers,[['content-type','audio/mpeg']]);assert.deepEqual(received,[[1,2,3]]);
  }finally{net.close();globalThis.fetch=previous;}
});
test('a browser CORS failure is reported once with no proxy retry or URL query disclosure',async()=>{
  const previous=globalThis.fetch,notices=[];let resolve,calls=0;
  const finished=new Promise(r=>resolve=r),host={browserNotify:m=>notices.push(m),_browser_net_done:(id,status)=>resolve(status)};
  globalThis.fetch=async()=>{calls++;throw new TypeError('CORS blocked');};
  const net=installBrowserNetwork(host,['web'],{url:'https://store.test/relay'});
  try{
    host.browserNetStart({id:1,url:'https://provider.test/path?token=secret',method:'GET',headers:[],wanted:[],timeout:1000});
    assert.equal(await finished,-14);assert.equal(calls,1);assert.equal(notices.length,1);assert.ok(!notices[0].message.includes('secret'));assert.match(notices[0].message,/direct network failed/);
  }finally{net.close();globalThis.fetch=previous;}
});

// SPDX-License-Identifier: MIT
import test from 'node:test';
import assert from 'node:assert/strict';
import {installBrowserNetwork} from './network.mjs';
async function request({origins=['web'],proxy={url:'https://store.test/play.fetch',csrf:'test-csrf'},url='http://music.163.com/api/personalized/newsong?limit=12',method='GET',body=new Uint8Array()}={}){
  const previous=globalThis.fetch,requests=[],received=[],headers=[];let finish,heapAt=128;
  const completed=new Promise(resolve=>finish=resolve);
  const host={HEAPU8:new Uint8Array(65536),_malloc:n=>{const p=heapAt;heapAt+=Math.max(8,n);return p;},_free(){},
    _browser_net_header:(id,p,n,q,s)=>{headers.push([new TextDecoder().decode(host.HEAPU8.slice(p,p+n)),new TextDecoder().decode(host.HEAPU8.slice(q,q+s))]);return 1;},
    _browser_net_headers:(id,status)=>assert.equal(status,206),
    _browser_net_receive:(id,p,n)=>{received.push(host.HEAPU8.slice(p,p+n));return n;},
    _browser_net_done:(id,status)=>finish(status)};
  globalThis.fetch=async(url,options)=>{requests.push({url:String(url),options});return new Response(new Uint8Array([1,2,3,4]),{status:200,headers:{'x-pxa-status':'206','x-pxa-headers':btoa(JSON.stringify([['content-type','audio/mpeg'],['content-range','bytes 0-3/4']]))}});};
  const net=installBrowserNetwork(host,origins,proxy);
  try{
    host.browserNetStart({id:1,url,method,headers:[['range','bytes=0-3']],wanted:['content-range'],body,timeout:1000,upload:false});
    const status=await completed;return {status,requests,received,headers};
  }finally{net.close();globalThis.fetch=previous;}
}
test('signed web authority carries music HTTP URLs through the authenticated HTTPS relay',async()=>{
  const result=await request();assert.equal(result.status,0);assert.equal(result.requests.length,1);
  const {url,options}=result.requests[0];assert.equal(url,'https://store.test/play.fetch');assert.equal(options.credentials,'same-origin');
  assert.equal(options.headers['X-CSRF-Token'],'test-csrf');assert.equal(JSON.parse(options.body).url,'http://music.163.com/api/personalized/newsong?limit=12');
  assert.deepEqual(result.headers,[['content-type','audio/mpeg'],['content-range','bytes 0-3/4']]);assert.deepEqual([...result.received[0]],[1,2,3,4]);
});
test('HTTP relay preserves POST body, selected headers and explicit HTTP origin authority',async()=>{
  const result=await request({origins:['http://music.163.com'],method:'POST',body:new Uint8Array([0,127,255])});
  assert.equal(result.status,0);const sent=JSON.parse(result.requests[0].options.body);assert.equal(sent.method,'POST');assert.equal(sent.body,'AH//');assert.deepEqual(sent.headers,[['range','bytes=0-3']]);
});
test('HTTP support does not broaden exact-origin scopes or allow direct mixed-content access',async()=>{
  for(const options of [{origins:['https://music.163.com']},{proxy:null},{url:'file:///etc/passwd'},{origins:[]}]){
    const result=await request(options);assert.equal(result.status,-14);assert.equal(result.requests.length,0);
  }
});

import test from 'node:test';
import assert from 'node:assert/strict';
import {prepareGuestModule} from './wasm-policy.mjs';
const text=s=>[s.length,...new TextEncoder().encode(s)];
const section=(id,bytes)=>[id,bytes.length,...bytes];
function fixture(memory=[1,0,1]){
  const names=[1,0,...text('__stack_pointer')];
  return new Uint8Array([0,97,115,109,1,0,0,0,
    ...section(4,[1,0x70,0,1]),...section(5,memory),
    ...section(6,[1,0x7f,1,0x41,0x80,0x80,4,0x0b]),
    ...section(7,[2,...text('memory'),2,0,...text('table'),1,0]),
    ...section(0,[...text('name'),...section(7,names)])]);
}
test('allocation limits apply before instantiate and preserve the named stack',async()=>{
  const original=fixture(),copy=original.slice();
  const {instance}=await WebAssembly.instantiate(prepareGuestModule(original));
  assert.deepEqual(original,copy);
  const e=instance.exports;
  assert.equal(e.__pxa_browser_stack.value,65536);
  e.__pxa_browser_stack.value-=4096;assert.equal(e.__pxa_browser_stack.value,61440);
  assert.throws(()=>e.memory.grow(1024),RangeError);
  assert.throws(()=>e.table.grow(65536),RangeError);
  assert.equal(e.memory.grow(1),1);
});
test('oversized, shared, truncated and duplicate-memory declarations fail closed',()=>{
  for(const memory of [[1,0,0x81,8],[1,3,1,2],[2,0,1,0,1]])
    assert.throws(()=>prepareGuestModule(fixture(memory)));
  assert.throws(()=>prepareGuestModule(fixture().subarray(0,15)));
});

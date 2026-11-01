// SPDX-License-Identifier: MIT
import test from 'node:test';
import assert from 'node:assert/strict';
import {installBrowserSensors} from './sensors.mjs';
test('motion bridge uses signed scopes, bounded int32 samples, freshness and subscription epochs',()=>{
  const messages=[],host={HEAP32:new Int32Array(16)};let now=100;
  const sensors=installBrowserSensors(host,m=>messages.push(m),[
    {name:'sensor.read',scope:'motion.acceleration'},
    {name:'sensor.read',scope:'motion.angular-velocity'},
  ],3,()=>now);
  assert.equal(host.browserSensorMask,3);assert.equal(host.browserSensorSubscribe(1),0);
  const epoch=messages.at(-1).id;
  assert.equal(host.browserSensorRead(1,0),-7,'no synthetic flat reading');
  sensors.sample(epoch,[{id:1,values:[10,20,9800000]}]);
  assert.equal(host.browserSensorRead(1,0),0);assert.deepEqual([...host.HEAP32.slice(0,3)],[10,20,9800000]);
  now+=251;assert.equal(host.browserSensorRead(1,0),-7,'stale hardware must not continue steering');
  sensors.sample(epoch,[{id:1,values:[NaN,0,0]}]);assert.equal(host.browserSensorRead(1,0),-7);
  sensors.sample(epoch,[{id:1,values:[1,2,3]}]);
  host.browserSensorSubscribe(2);const combined=messages.at(-1);assert.equal(combined.mask,3);assert.equal(combined.id,epoch);
  assert.equal(messages.some(m=>m.type==='sensors-stop'),false,'adding gyro never stops acceleration');
  assert.equal(host.browserSensorRead(1,0),0,'acceleration survives adding gyro');
  sensors.sample(epoch,[{id:1,values:[4,5,6]}]);assert.equal(host.browserSensorRead(1,0),0);
  sensors.sample(combined.id,[{id:2,values:[1000000,-2000000,3]}]);assert.equal(host.browserSensorRead(2,0),0);
  sensors.pause(true);assert.equal(messages.at(-1).type,'sensors-stop');assert.equal(host.browserSensorRead(2,0),-7);
  sensors.pause(false);assert.equal(messages.at(-1).mask,3);
  assert.notEqual(messages.at(-1).id,epoch);sensors.sample(epoch,[{id:2,values:[1,2,3]}]);assert.equal(host.browserSensorRead(2,0),-7,'old paused session rejected');
  host.browserSensorUnsubscribe(1);assert.equal(messages.at(-1).mask,2);
  host.browserSensorUnsubscribe(2);assert.equal(messages.at(-1).type,'sensors-stop');sensors.close();
  const denied={};installBrowserSensors(denied,()=>{},[],3);assert.equal(denied.browserSensorMask,0);assert.equal(denied.browserSensorSubscribe(1),-3);
});
test('magnetic-field.0 requires its own signed scope and preserves other active channels',()=>{
  const host={HEAP32:new Int32Array(16)},messages=[];let now=0;
  const bridge=installBrowserSensors(host,m=>messages.push(m),[
    {name:'sensor.read',scope:'motion.acceleration'},{name:'sensor.read',scope:'magnetic-field.0'},
  ],7,()=>now);
  assert.equal(host.browserSensorMask,5);assert.equal(host.browserSensorSubscribe(2),-3);
  assert.equal(host.browserSensorSubscribe(1.5),-3);assert.equal(host.browserSensorSubscribe(4),-3);
  host.browserSensorSubscribe(1);host.browserSensorSubscribe(3);
  const id=messages.at(-1).id;assert.equal(messages.at(-1).mask,5);
  bridge.sample(id,[{id:1,values:[0,0,9800000]},{id:3,values:[25000,-1500,42000]}]);
  assert.equal(host.browserSensorRead(3,0),0);assert.deepEqual([...host.HEAP32.slice(0,3)],[25000,-1500,42000]);
  host.browserSensorUnsubscribe(1);assert.equal(host.browserSensorRead(3,0),0);
  now=251;bridge.sample(id,[{id:3,values:[2147483648,0,0]},{id:3,values:[1.1,0,0]}]);
  assert.equal(host.browserSensorRead(3,0),-7);
  bridge.pause(true);bridge.pause(false);bridge.sample(id,[{id:3,values:[1,2,3]}]);assert.equal(host.browserSensorRead(3,0),-7);
  bridge.sample(messages.at(-1).id,[{id:3,values:[1,2,3]}]);assert.equal(host.browserSensorRead(3,0),0);
  host.browserSensorUnsubscribe(3);host.browserSensorSubscribe(3);assert.equal(host.browserSensorRead(3,0),-7);
  bridge.close();assert.equal(messages.at(-1).type,'sensors-stop');
  const other={};installBrowserSensors(other,()=>{},[{name:'sensor.read',scope:'magnetic-field.1'}],7);
  assert.equal(other.browserSensorMask,0,'an uncalibrated API must not masquerade as a second physical sensor');
});
test('sample age survives delivery; stale, future and reordered timestamps are rejected',()=>{
  let now=500;const host={HEAP32:new Int32Array(8)},messages=[];
  const s=installBrowserSensors(host,m=>messages.push(m),[{name:'sensor.read',scope:'magnetic-field.0'}],4,()=>now);
  host.browserSensorSubscribe(3);const id=messages.at(-1).id;
  s.sample(id,[{id:3,values:[1,2,3],ageMs:120}]);assert.equal(host.browserSensorTimestamp(3),380);
  for(const ageMs of [251,-1,NaN,Infinity,121])s.sample(id,[{id:3,values:[4,5,6],ageMs}]);
  assert.equal(host.browserSensorTimestamp(3),380);host.browserSensorRead(3,0);assert.equal(host.HEAP32[0],1);
  now=631;assert.equal(host.browserSensorRead(3,0),-7);
  s.close();
});
test('cross-realm acquisition time includes time spent queued before Worker delivery',()=>{
  let now=500;const host={HEAP32:new Int32Array(8)},messages=[];
  const s=installBrowserSensors(host,m=>messages.push(m),[{name:'sensor.read',scope:'magnetic-field.0'}],4,()=>now,10000);
  host.browserSensorSubscribe(3);const id=messages.at(-1).id;
  s.sample(id,[{id:3,values:[1,2,3],timestampMs:10380}]);assert.equal(host.browserSensorTimestamp(3),380);
  now=700;s.sample(id,[{id:3,values:[4,5,6],timestampMs:10400}]);assert.equal(host.browserSensorRead(3,0),-7,'delayed delivery is not a fresh sample');
  s.sample(id,[{id:3,values:[4,5,6],timestampMs:10701}]);assert.equal(host.browserSensorRead(3,0),-7,'future time rejected');
  s.close();
});

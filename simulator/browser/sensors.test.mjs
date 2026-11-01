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
  host.browserSensorSubscribe(2);const combined=messages.at(-1);assert.equal(combined.mask,3);
  sensors.sample(epoch,[{id:1,values:[1,2,3]}]);assert.equal(host.browserSensorRead(1,0),-7,'late old epoch rejected');
  sensors.sample(combined.id,[{id:2,values:[1000000,-2000000,3]}]);assert.equal(host.browserSensorRead(2,0),0);
  sensors.pause(true);assert.equal(messages.at(-1).type,'sensors-stop');assert.equal(host.browserSensorRead(2,0),-7);
  sensors.pause(false);assert.equal(messages.at(-1).mask,3);
  host.browserSensorUnsubscribe(1);assert.equal(messages.at(-1).mask,2);
  host.browserSensorUnsubscribe(2);assert.equal(messages.at(-1).type,'sensors-stop');sensors.close();
  const denied={};installBrowserSensors(denied,()=>{},[],3);assert.equal(denied.browserSensorMask,0);assert.equal(denied.browserSensorSubscribe(1),-3);
});

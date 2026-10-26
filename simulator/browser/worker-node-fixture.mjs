// Test adapter for the standard Worker message API; never shipped to the site.
import {parentPort,workerData} from 'node:worker_threads';
globalThis.onmessage=null;
globalThis.postMessage=(message,transfer)=>parentPort.postMessage(message,transfer);
await import(workerData.module);
parentPort.on('message',data=>globalThis.onmessage({data}));

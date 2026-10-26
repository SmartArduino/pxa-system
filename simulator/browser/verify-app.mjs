// Run publisher Wasm artifacts with the real Host without putting game code
// into the test repository. Output is suitable for local performance records.
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const bundle=process.env.PXA_BROWSER_BUNDLE;
if(!bundle||process.argv.length<3)throw new Error('PXA_BROWSER_BUNDLE and one or more .wasm paths are required');
const {default:createHost}=await import(pathToFileURL(path.join(bundle,'pxa-browser.mjs')));
const {createRuntime}=await import(pathToFileURL(path.join(bundle,'runtime.mjs')));
const mainLoop=process.argv.includes('--main-loop');
for(const file of process.argv.slice(2).filter(a=>!a.startsWith('--'))){
  let runtime;
  try{
    const host=await createHost({print:()=>{},printErr:()=>{}});
    runtime=await createRuntime(host,await fs.readFile(file),{appId:'validation',mainLoop,assets:await fs.readFile(file+'.assets.json','utf8').then(s=>JSON.parse(s).map(a=>({path:a.path,binary:Buffer.from(a.base64,'base64')}))).catch(e=>{if(e.code==='ENOENT')return [];throw e;}),notify:()=>{},log:()=>{}});
    const revisions=new Set(),hashes=new Set(),costs=[],start=performance.now();
    for(let i=0;i<600;i++){
      if(i===30||i===300)runtime.controller(16);
      if(i===31||i===301)runtime.controller(0);
      if(i>60&&i%60===0)runtime.controller([1,8,2,4][(i/60)%4]);
      if(i>60&&i%60===30)runtime.controller(0);
      const begin=performance.now();revisions.add(runtime.tick(performance.now()));
      costs.push(performance.now()-begin);
      if(i%30===0)hashes.add(createHash('sha256').update(runtime.pixels()).digest('hex'));
      runtime.audio();
      await new Promise(resolve=>setTimeout(resolve,17));
    }
    runtime.controller(0);await runtime.stop();runtime=null;
    costs.sort((a,b)=>a-b);
    console.log(JSON.stringify({file,frames:600,revisions:revisions.size,images:hashes.size,
      meanMs:costs.reduce((a,b)=>a+b,0)/costs.length,p95Ms:costs[Math.floor(costs.length*.95)]}));
    if(hashes.size<2)process.exitCode=1;
  }catch(error){console.error(file,error);process.exitCode=1;}
}

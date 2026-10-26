// SPDX-License-Identifier: MIT
// Each app gets a separate IndexedDB-backed mount and an exclusive writer
// lock. Save before graceful stop; never silently fall back after a DB error.
export async function prepareStorage(host, appId, notify=()=>{}) {
  if (!/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$/.test(appId)) throw new Error('Invalid app storage identity');
  const sync = populate => new Promise((resolve,reject)=>host.FS.syncfs(populate,e=>e?reject(e):resolve()));
  let release=()=>{}, timer=0, pending=Promise.resolve(), dirty=false;
  if (!globalThis.indexedDB || !globalThis.navigator?.locks) {
    notify({type:'storage',persistent:false,message:'此浏览器不支持安全的持久化存储，当前数据只保留到停止运行。'});
    return {changed(){},flush:async()=>{},close:async()=>{}};
  }
  await new Promise((resolve,reject)=>{
    navigator.locks.request(`pxa-browser:${appId}`,{ifAvailable:true},async lock=>{
      if(!lock){reject(new Error('此应用已在另一页面运行，请先停止再打开'));return;}
      await new Promise(done=>{release=done;resolve();});
    }).catch(reject);
  });
  try {
    const mount=`/pxa-${appId}`;
    host.FS.mkdir(mount);host.FS.mount(host.IDBFS,{},mount);host.FS.symlink(mount,'/data');
    await sync(true);
  } catch(error) {release();throw new Error(`无法读取应用存档：${error.message||error}`);}
  notify({type:'storage',persistent:true});
  async function flush() {
    clearTimeout(timer);
    // Writes that arrive during a sync receive a separate subsequent commit.
    pending=pending.catch(()=>{}).then(async()=>{
      if(!dirty)return;dirty=false;
      try {await sync(false);notify({type:'saved'});}
      catch(error){dirty=true;notify({type:'storage-error',message:'保存失败，请保持页面打开并检查浏览器存储空间。'});throw error;}
    });
    return pending;
  }
  return {
    changed(){dirty=true;clearTimeout(timer);timer=setTimeout(()=>flush().catch(()=>{}),150);},
    flush,
    async close(){try{await flush();}finally{release();}},
  };
}

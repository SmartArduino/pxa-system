// SPDX-License-Identifier: MIT
// Only verified data reaches these private MEMFS mounts. No file is evaluated.
const MAX_FILES=4096,MAX_BYTES=256*1024*1024;
export function packagePath(path){
  return typeof path==='string'&&path.length<=(path.startsWith('media/')?261:255)&&/^[A-Za-z0-9_.-]+(?:\/[A-Za-z0-9_.-]+)*$/.test(path)&&
    !path.split('/').some(p=>p==='.'||p==='..');
}
export function installPackageFiles(host,assets=[]){
  if(!Array.isArray(assets)||assets.length>MAX_FILES)throw new Error('Too many package resources');
  const fs=host.FS,paths=new Set();let total=0,count=0;
  fs.mkdirTree('/package');fs.mkdirTree('/media');fs.mkdirTree('/data/media');
  const write=(path,data)=>{fs.mkdirTree(path.slice(0,path.lastIndexOf('/')));fs.writeFile(path,data,{canOwn:true});};
  for(const asset of assets){
    if(!packagePath(asset.path)||paths.has(asset.path))throw new Error('Invalid package resource path');
    const data=new Uint8Array(asset.binary);total+=data.byteLength;
    if(total>MAX_BYTES)throw new Error('Package resource budget exceeded');
    paths.add(asset.path);
    write(asset.path.startsWith('media/')?'/'+asset.path:'/package/'+asset.path,data);
  }
  // Imported user files override bundled examples. Bundled resources stay on
  // volatile /media; saving progress never writes a large game pack to IDB.
  const restore=(source,target,depth=0)=>{
    if(depth>32)throw new Error('Media directory depth exceeded');
    fs.mkdirTree(target);
    for(const name of fs.readdir(source)){
      if(name==='.'||name==='..')continue;
      if(++count>MAX_FILES)throw new Error('Too many imported files');
      const from=source+'/'+name,to=target+'/'+name,stat=fs.lstat(from);
      if(fs.isDir(stat.mode))restore(from,to,depth+1);
      else if(fs.isFile(stat.mode)){total+=stat.size;if(total>MAX_BYTES)throw new Error('Media budget exceeded');write(to,fs.readFile(from));}
      else throw new Error('Unsupported imported file type');
    }
  };
  restore('/data/media','/media');
  host.browserPackageBytes=total;
  return {bytes:total};
}
export async function importMediaFiles(host,{id=0,files=[]},storage,notify,valid=()=>true){
  const fs=host.FS,summary={imported:0,failed:0,skipped:0};let total=0;
  if(!Array.isArray(files)||files.length>64)throw new Error('一次最多导入64个文件');
  let stored=0,entries=0;
  const inventory=(path,depth=0)=>{
    if(depth>32)throw new Error('Media directory depth exceeded');
    for(const name of fs.readdir(path)){
      if(name==='.'||name==='..')continue;
      const next=path+'/'+name,stat=fs.lstat(next);entries++;
      if(fs.isDir(stat.mode))inventory(next,depth+1);else if(fs.isFile(stat.mode))stored+=stat.size;else throw new Error('Invalid media file');
      if(entries>MAX_FILES||stored>MAX_BYTES)throw new Error('Media budget exceeded');
    }
  };
  inventory('/data/media');
  const active=()=>valid()&&(!id||host._browser_import_active(id));
  for(const file of files){
    if(!active())break;
    const name=file.name;
    if(typeof name!=='string'||!name||new TextEncoder().encode(name).length>255||/[\/\\\x00-\x1f\x7f]/.test(name)||name==='.'||name==='..'||name.startsWith('.pxa-')||
       !Number.isSafeInteger(file.size)||file.size>64*1024*1024||(total+=file.size)>MAX_BYTES){summary.failed++;continue;}
    const destination='/data/media/'+name;
    if(fs.analyzePath(destination).exists){summary.skipped++;continue;}
    if(entries>=MAX_FILES||stored+file.size>MAX_BYTES||(host.browserPackageBytes??0)+file.size>MAX_BYTES){summary.failed++;continue;}
    const temporary=destination+'.pxa-part';let stream;
    try{
      stream=fs.open(temporary,193) /* O_WRONLY | O_CREAT | O_EXCL (MEMFS flags). */;
      for(let offset=0;offset<file.size;offset+=1024*1024){
        if(!active())throw new Error('导入已取消');
        const block=new Uint8Array(await file.slice(offset,offset+1024*1024).arrayBuffer());
        fs.write(stream,block,0,block.length);
      }
      fs.close(stream);stream=null;
      if(!active())throw new Error('导入已取消');
      fs.rename(temporary,destination);fs.writeFile('/media/'+name,fs.readFile(destination),{canOwn:true});
      stored+=file.size;entries++;host.browserPackageBytes=(host.browserPackageBytes??0)+file.size;
      summary.imported++;storage.changed();
    }catch(error){
      if(stream)fs.close(stream);
      try{fs.unlink(temporary);}catch{}
      summary.failed++;
    }
  }
  if(id&&active())host._browser_import_finish(id,0,summary.imported,summary.failed,summary.skipped,summary.failed?-14:0);
  await storage.flush();if(valid())notify({type:'file-import-done',id,...summary});
  return summary;
}

"""ASan/UBSan regressions for SDK service lifetimes and byte encoding."""
import argparse,concurrent.futures,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,default=Path(__file__).resolve().parents[3]);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
sdk=a.sdk.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
flags=[os.environ.get('CXX','clang++'),'-std=c++2c','-O1','-g','-Wall','-Wextra','-Werror','-Wno-attributes','-fno-exceptions','-fno-rtti','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(sdk/'include')]
def compile_source(name):
 obj=out/(name+'.o')
 with (out/(name+'-build.log')).open('w')as log:subprocess.run([*flags,'-c',str(sdk/'src'/f'{name}.cpp'),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True)
 return obj
with concurrent.futures.ThreadPoolExecutor(max_workers=3)as pool:objects=list(pool.map(compile_source,('runtime','net','ipc','work','game','surface')))
env=dict(os.environ,ASAN_OPTIONS='detect_stack_use_after_return=1:detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1')
results={}
for name in ('features','binary','task','service_lifetime','encoding_alias','net','ipc','ipc_contract'):
 binary=out/name
 with (out/(name+'-build.log')).open('w')as log:subprocess.run([*flags,str(sdk/'tests'/f'{name}_test.cpp'),*map(str,objects),'-o',str(binary)],stdout=log,stderr=subprocess.STDOUT,check=True)
 with (out/(name+'-run.log')).open('w')as log:r=subprocess.run([str(binary)],env=env,stdout=log,stderr=subprocess.STDOUT)
 results[name]=r.returncode;print(name,r.returncode,flush=True)
 (out/'results.json').write_text(json.dumps(results,indent=2)+'\n');r.check_returncode()
